#include <pspkernel.h>
#include <pspgu.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <jpeglib.h>
#include "artwork.h"
#include "mpd.h"
#include "http.h"
#include "config.h"

/* ── Static buffers ─────────────────────────────────────────────────────── */
/* Keep memory predictable: no malloc/free per song. */

#define MAX_FETCH       (256 * 1024)   /* max album art file size we'll accept */
#define MAX_DECODE_W    512            /* libjpeg max scaled width we handle */
#define MAX_DECODE_H    512

static unsigned char s_fetch_buf[MAX_FETCH];
static unsigned char s_decode_buf[MAX_DECODE_W * MAX_DECODE_H * 3];

/* The actual GU texture: 128x128 RGBA8888, 16-byte aligned for the GPU. */
static __attribute__((aligned(16))) unsigned int s_tex[ARTWORK_W * ARTWORK_H];

static int       s_tex_valid = 0;
static char      s_current_uri[256] = "";
static ArtStatus s_status = AW_STATUS_NONE;
static int       s_last_bytes = 0;

ArtStatus artwork_status(void)     { return s_status; }
int       artwork_last_bytes(void) { return s_last_bytes; }
unsigned int artwork_first_pixel(void) { return s_tex_valid ? s_tex[0] : 0; }

/* ── Init / state ───────────────────────────────────────────────────────── */

void artwork_init(void) {
    s_tex_valid = 0;
    s_current_uri[0] = '\0';
}

void artwork_clear(void) {
    s_tex_valid = 0;
    s_current_uri[0] = '\0';
}

int artwork_is_valid(void) { return s_tex_valid; }

/* ── JPEG decode ────────────────────────────────────────────────────────── */

static int decode_jpeg_to_tex(const unsigned char *data, int len) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, (unsigned char *)data, len);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    /* Pick scale_denom (1, 2, 4, 8) so the output stays under MAX_DECODE_*. */
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1;
    while (cinfo.scale_denom < 8 &&
           ((int)cinfo.image_width  / cinfo.scale_denom > MAX_DECODE_W ||
            (int)cinfo.image_height / cinfo.scale_denom > MAX_DECODE_H)) {
        cinfo.scale_denom *= 2;
    }
    cinfo.out_color_space = JCS_RGB;

    jpeg_start_decompress(&cinfo);
    int dw = cinfo.output_width;
    int dh = cinfo.output_height;
    int comps = cinfo.output_components;

    if (dw * dh * comps > (int)sizeof(s_decode_buf) || comps != 3) {
        jpeg_abort_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    int row_stride = dw * comps;
    while ((int)cinfo.output_scanline < dh) {
        unsigned char *rowptr = s_decode_buf + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &rowptr, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    /* Nearest-neighbour resize from dwxdh RGB into ARTWORK_W x ARTWORK_H ABGR. */
    for (int y = 0; y < ARTWORK_H; y++) {
        int sy = (y * dh) / ARTWORK_H;
        const unsigned char *srow = s_decode_buf + sy * row_stride;
        unsigned int *drow = &s_tex[y * ARTWORK_W];
        for (int x = 0; x < ARTWORK_W; x++) {
            int sx = (x * dw) / ARTWORK_W;
            const unsigned char *p = srow + sx * 3;
            /* PSP GU GU_PSM_8888 is ABGR: A=FF, B=p[2], G=p[1], R=p[0]. */
            drow[x] = 0xFF000000u | ((unsigned)p[2] << 16) | ((unsigned)p[1] << 8) | p[0];
        }
    }

    sceKernelDcacheWritebackInvalidateRange(s_tex, sizeof(s_tex));
    return 0;
}

/* ── Format detection ───────────────────────────────────────────────────── */

static int is_jpeg(const unsigned char *d, int n) {
    return n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}

/* ── Mopidy HTTP API fallback ───────────────────────────────────────────── */

/* Find the first "uri":"VALUE" inside the JSON, write VALUE into out. */
static int find_first_image_uri(const char *json, int len, char *out, int max) {
    const char *end = json + len;
    for (const char *p = json; p < end - 6; p++) {
        if (memcmp(p, "\"uri\":", 6) != 0) continue;
        const char *q = p + 6;
        while (q < end && (*q == ' ' || *q == '\t')) q++;
        if (q >= end || *q != '"') continue;
        q++;
        int i = 0;
        while (q < end && *q != '"' && i < max - 1) out[i++] = *q++;
        out[i] = '\0';
        return i > 0;
    }
    return 0;
}

/* Append c to dst[*idx], guarding against overflow. */
static void append(char *dst, int dst_max, int *idx, char c) {
    if (*idx < dst_max - 1) dst[(*idx)++] = c;
}

/* Try fetching album art via Mopidy's HTTP JSON-RPC API. */
static int try_http_fetch(const char *uri, unsigned char *out, int max_len) {
    /* Build JSON body with the URI escaped for JSON. */
    char body[1024];
    char esc[512];
    int  ei = 0;
    for (int i = 0; uri[i] && ei < (int)sizeof(esc) - 2; i++) {
        if (uri[i] == '"' || uri[i] == '\\') append(esc, sizeof(esc), &ei, '\\');
        append(esc, sizeof(esc), &ei, uri[i]);
    }
    esc[ei] = '\0';

    int n = snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"id\":1,"
        "\"method\":\"core.library.get_images\","
        "\"params\":{\"uris\":[\"%s\"]}}",
        esc);
    if (n >= (int)sizeof(body)) return -1;

    /* POST the request and read the JSON response. */
    static char rpc[4096];
    int rl = http_post_json(g_config.host, g_config.http_port, "/mopidy/rpc",
                            body, rpc, sizeof(rpc) - 1);
    if (rl <= 0) return -1;
    rpc[rl] = '\0';

    /* Extract image URI. */
    char img_uri[512];
    if (!find_first_image_uri(rpc, rl, img_uri, sizeof(img_uri))) return -1;

    /* Resolve the image URI into an HTTP path. */
    const char *path = img_uri;
    if (strncmp(img_uri, "http://", 7) == 0) {
        const char *slash = strchr(img_uri + 7, '/');
        path = slash ? slash : "/";
    } else if (img_uri[0] != '/') {
        return -1;   /* unknown scheme (https/file/etc.) */
    }

    /* GET the image bytes. */
    return http_get(g_config.host, g_config.http_port, path, out, max_len);
}

/* ── Public load ────────────────────────────────────────────────────────── */

int artwork_load(int mpd_fd, const char *uri) {
    if (!uri || !uri[0]) {
        artwork_clear();
        return 0;
    }
    /* Cache hit — keep existing texture. */
    if (s_tex_valid && strcmp(s_current_uri, uri) == 0) return 1;

    artwork_clear();

    /* Try MPD's binary art commands first. */
    int n = mpd_fetch_albumart(mpd_fd, uri, s_fetch_buf, sizeof(s_fetch_buf));
    /* Fall back to Mopidy's HTTP JSON-RPC API. */
    if (n <= 0) n = try_http_fetch(uri, s_fetch_buf, sizeof(s_fetch_buf));
    s_last_bytes = n;
    if (n <= 0) { s_status = AW_STATUS_NO_DATA; return 0; }

    if (!is_jpeg(s_fetch_buf, n))                  { s_status = AW_STATUS_NOT_JPEG;    return 0; }
    if (decode_jpeg_to_tex(s_fetch_buf, n) != 0)   { s_status = AW_STATUS_DECODE_FAIL; return 0; }

    strncpy(s_current_uri, uri, sizeof(s_current_uri) - 1);
    s_current_uri[sizeof(s_current_uri) - 1] = '\0';
    s_tex_valid = 1;
    s_status = AW_STATUS_OK;
    return 1;
}

/* ── Render ─────────────────────────────────────────────────────────────── */

typedef struct {
    float        u, v;
    unsigned int color;
    float        x, y, z;
} TexVertex;

void artwork_draw(float x, float y, float size) {
    if (!s_tex_valid) return;

    /* Aggressive state reset (intraFont leaves CLUT/T8/blend state behind). */
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, ARTWORK_W, ARTWORK_H, ARTWORK_W, s_tex);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f / ARTWORK_W, 1.0f / ARTWORK_H);  /* normalise UVs */
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexFlush();   /* invalidate the GPU's texture cache */

    TexVertex *v = sceGuGetMemory(2 * sizeof(TexVertex));
    v[0].u = 0;          v[0].v = 0;
    v[0].color = 0xFFFFFFFF;
    v[0].x = x;          v[0].y = y;          v[0].z = 0;
    v[1].u = ARTWORK_W;  v[1].v = ARTWORK_H;
    v[1].color = 0xFFFFFFFF;
    v[1].x = x + size;   v[1].y = y + size;   v[1].z = 0;

    sceGuDrawArray(GU_SPRITES,
        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        2, 0, v);

    /* Restore identity texture transform so subsequent textured draws aren't broken. */
    sceGuTexScale(1.0f, 1.0f);
    sceGuDisable(GU_TEXTURE_2D);
}
