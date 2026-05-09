#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <intraFont.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "artwork.h"
#include "mpd.h"

/* PSP native screen */
#define SCR_W       480
#define SCR_H       272
#define BUF_W       512

/* Colours: 0xAABBGGRR (PSP GU expects ABGR) */
#define COL_BG          0xFF1A1A1A
#define COL_HEADER      0xFF252525
#define COL_DIVIDER     0xFF3A3A3A
#define COL_BAR_BG      0xFF505050   /* clearly lighter than BG */
#define COL_BAR_BORDER  0xFF707070
#define COL_PROGRESS    0xFF44CC88
#define COL_ACCENT      0xFF44CC88
#define COL_WHITE       0xFFFFFFFF
#define COL_GRAY        0xFFB0B0B0
#define COL_DIM         0xFF707070
#define COL_PAUSE       0xFFAA88FF
#define COL_PLAY        0xFF44CC88
#define COL_STOP        0xFFAAAAAA

/* GU display list (must be 16-byte aligned) */
static unsigned int __attribute__((aligned(16))) g_list[262144];

/* Fonts */
static intraFont *g_font_title;
static intraFont *g_font_body;
static intraFont *g_font_small;

/* Vertex used by draw_rect (sprite primitive: 2 verts) */
typedef struct { unsigned int color; float x, y, z; } RectVertex;

/* ── GU helpers ─────────────────────────────────────────────────────────── */

static void frame_begin(void) {
    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(COL_BG);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

static void frame_end(void) {
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void draw_rect(float x, float y, float w, float h, unsigned int color) {
    /* Aggressive state reset — intraFont leaves the pipeline in a textured-
       text mode (8-bit palette texture, GU_TFX_MODULATE, custom blend func,
       maybe depth test). If we don't undo all of it, plain coloured rects
       drawn after a font call render as nothing. */
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    RectVertex *v = sceGuGetMemory(2 * sizeof(RectVertex));
    v[0].color = color; v[0].x = x;     v[0].y = y;     v[0].z = 0;
    v[1].color = color; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES,
                   GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   2, 0, v);
}

/* ── Public ─────────────────────────────────────────────────────────────── */

void ui_init(void) {
    /* GU init */
    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0,        BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)0x88000, BUF_W);
    sceGuDepthBuffer((void *)0x110000, BUF_W);
    sceGuOffset (2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    /* intraFont init — load Latin sans-serif at three sizes */
    intraFontInit();
    g_font_title = intraFontLoad("flash0:/font/ltn0.pgf", INTRAFONT_CACHE_ALL);
    g_font_body  = intraFontLoad("flash0:/font/ltn0.pgf", INTRAFONT_CACHE_ALL);
    g_font_small = intraFontLoad("flash0:/font/ltn0.pgf", INTRAFONT_CACHE_ALL);
}

static const char *state_label(MpdState s) {
    switch (s) {
        case MPD_STATE_PLAY:  return "PLAYING";
        case MPD_STATE_PAUSE: return "PAUSED";
        case MPD_STATE_STOP:  return "STOPPED";
        default:              return "---";
    }
}

static unsigned int state_colour(MpdState s) {
    switch (s) {
        case MPD_STATE_PLAY:  return COL_PLAY;
        case MPD_STATE_PAUSE: return COL_PAUSE;
        case MPD_STATE_STOP:  return COL_STOP;
        default:              return COL_DIM;
    }
}

/* Print possibly multi-line text. Returns the y after the last line. */
static float print_multiline(intraFont *f, float x, float y, float lh, const char *text) {
    if (!f || !text) return y;
    char buf[512];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *line = buf;
    while (line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        intraFontPrint(f, x, y, line);
        y += lh;
        if (!nl) break;
        line = nl + 1;
    }
    return y;
}

void ui_draw_status(const char *msg) {
    frame_begin();

    /* Header bar */
    draw_rect(0, 0, SCR_W, 32, COL_HEADER);
    draw_rect(0, 32, SCR_W, 1, COL_DIVIDER);
    if (g_font_title) {
        intraFontSetStyle(g_font_title, 0.9f, COL_ACCENT, 0, 0.0f, 0);
        intraFontPrint(g_font_title, 14, 22, "MopiTube");
    }

    /* Body */
    if (g_font_body) {
        intraFontSetStyle(g_font_body, 0.7f, COL_WHITE, 0, 0.0f, 0);
        print_multiline(g_font_body, 24, 70, 18, msg);
    }

    frame_end();
}

void ui_draw_now_playing(const MpdSong *song, const MpdStatus *status) {
    frame_begin();

    /* ── Header ────────────────────────────────────────────────────────── */
    draw_rect(0, 0, SCR_W, 32, COL_HEADER);
    draw_rect(0, 32, SCR_W, 1, COL_DIVIDER);

    if (g_font_title) {
        intraFontSetStyle(g_font_title, 0.9f, COL_ACCENT, 0, 0.0f, 0);
        intraFontPrint(g_font_title, 14, 22, "MopiTube");
    }
    if (g_font_small) {
        const char *lbl = state_label(status->state);
        intraFontSetStyle(g_font_small, 0.7f, state_colour(status->state), 0, 0.0f, 0);
        float w = intraFontMeasureText(g_font_small, lbl);
        intraFontPrint(g_font_small, SCR_W - 14 - w, 21, lbl);
    }

    /* ── Album art (always reserved on the left) ───────────────────────── */
    const float art_x = 18;
    const float art_y = 50;
    const float art_size = 112;
    int has_art = artwork_is_valid();

    if (has_art) {
        artwork_draw(art_x, art_y, art_size);
    } else {
        /* Visible placeholder so the user sees the slot. */
        draw_rect(art_x, art_y, art_size, art_size, COL_BAR_BG);
        if (g_font_title) {
            intraFontSetStyle(g_font_title, 1.6f, COL_DIM, 0, 0.0f, 0);
            float w = intraFontMeasureText(g_font_title, "MOPI");
            intraFontPrint(g_font_title,
                           art_x + (art_size - w) / 2,
                           art_y + art_size / 2 + 8,
                           "MOPI");
        }
    }
    /* Border (drawn over both states) */
    draw_rect(art_x - 1, art_y - 1, art_size + 2, 1,           COL_BAR_BORDER);
    draw_rect(art_x - 1, art_y + art_size, art_size + 2, 1,    COL_BAR_BORDER);
    draw_rect(art_x - 1, art_y - 1, 1,           art_size + 2, COL_BAR_BORDER);
    draw_rect(art_x + art_size, art_y - 1, 1,    art_size + 2, COL_BAR_BORDER);

    /* ── Song info (always to the right of art) ────────────────────────── */
    const float text_x = art_x + art_size + 16;
    const float text_w = SCR_W - text_x - 18;

    if (g_font_title) {
        intraFontSetStyle(g_font_title, 1.0f, COL_WHITE, 0, 0.0f, 0);
        intraFontPrintColumn(g_font_title, text_x, 70, text_w,
                             song->title[0] ? song->title : "(nothing playing)");
    }
    if (g_font_body) {
        intraFontSetStyle(g_font_body, 0.75f, COL_GRAY, 0, 0.0f, 0);
        if (song->artist[0])
            intraFontPrintColumn(g_font_body, text_x, 100, text_w, song->artist);
        if (song->album[0])
            intraFontPrintColumn(g_font_body, text_x, 124, text_w, song->album);
    }

    /* ── Progress bar (extra-prominent for now) ────────────────────────── */
    const int bar_x = 24;
    const int bar_y = 178;
    const int bar_w = SCR_W - 48;
    const int bar_h = 14;

    /* Reset any GU state intraFont might have left enabled. */
    sceGuDisable(GU_TEXTURE_2D);

    /* Outer rim — bright so it's unmistakable while debugging */
    draw_rect(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, 0xFFFFFFFF);
    /* Track */
    draw_rect(bar_x, bar_y, bar_w, bar_h, 0xFF404040);

    /* Fill */
    if (status->duration > 0) {
        int filled = (status->elapsed * bar_w) / status->duration;
        if (filled < 0)     filled = 0;
        if (filled > bar_w) filled = bar_w;
        draw_rect(bar_x, bar_y, filled, bar_h, COL_PROGRESS);
    }

    /* Time text under the bar */
    if (g_font_small) {
        char buf[32];
        if (status->duration > 0) {
            snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d",
                     status->elapsed / 60, status->elapsed % 60,
                     status->duration / 60, status->duration % 60);
        } else {
            snprintf(buf, sizeof(buf), "--:-- / --:--");
        }
        intraFontSetStyle(g_font_small, 0.7f, COL_GRAY, 0, 0.0f, 0);
        intraFontPrint(g_font_small, bar_x, bar_y + bar_h + 18, buf);
    }

    /* ── Bottom legend + artwork status (debug) ────────────────────────── */
    sceGuDisable(GU_TEXTURE_2D);
    draw_rect(0, SCR_H - 26, SCR_W, 1, COL_DIVIDER);
    if (g_font_small) {
        intraFontSetStyle(g_font_small, 0.7f, COL_DIM, 0, 0.0f, 0);
        intraFontPrint(g_font_small, 14, SCR_H - 8,
                       "[X] Play/Pause   [L] Prev   [R] Next");

        const char *art_lbl = "art:?";
        switch (artwork_status()) {
            case AW_STATUS_NONE:        art_lbl = "art:none"; break;
            case AW_STATUS_OK:          art_lbl = "art:ok";   break;
            case AW_STATUS_NO_DATA:     art_lbl = "art:0B";   break;
            case AW_STATUS_NOT_JPEG:    art_lbl = "art:!jpg"; break;
            case AW_STATUS_DECODE_FAIL: art_lbl = "art:fail"; break;
        }
        char buf[80];
        snprintf(buf, sizeof(buf), "%s %dB px=%08X",
                 art_lbl, artwork_last_bytes(), artwork_first_pixel());
        float w = intraFontMeasureText(g_font_small, buf);
        intraFontPrint(g_font_small, SCR_W - 14 - w, SCR_H - 8, buf);

        /* Mopidy's ACK reason in the title-bar area (debug) */
        const char *ack = mpd_last_ack();
        if (ack && ack[0]) {
            intraFontSetStyle(g_font_small, 0.6f, COL_PAUSE, 0, 0.0f, 0);
            intraFontPrintColumn(g_font_small, 14, SCR_H - 28, SCR_W - 28, ack);
        }
    }

    frame_end();
}
