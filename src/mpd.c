#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include "config.h"
#include "mpd.h"
#include "net.h"

#define LINE 256  /* max line length for MPD responses */

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Read lines until "OK" or "ACK". Returns 0 on OK, -1 on error. */
static int drain_ok(int fd) {
    char line[LINE];
    for (int i = 0; i < 1000; i++) {
        if (net_tcp_recv_line(fd, line, LINE) < 0) return -1;
        if (strcmp(line, "OK") == 0)               return  0;
        if (strncmp(line, "ACK", 3) == 0)          return -1;
    }
    return -1;
}

/* Case-insensitive key: value split. Writes val, returns 1 on match. */
static int kv(const char *line, const char *key, char *val, int vlen) {
    int klen = (int)strlen(key);
    if (strncasecmp(line, key, klen) != 0 || line[klen] != ':') return 0;
    const char *v = line + klen + 1;
    while (*v == ' ') v++;
    strncpy(val, v, vlen - 1);
    val[vlen - 1] = '\0';
    return 1;
}

static int simple_cmd(int fd, const char *cmd) {
    net_tcp_send(fd, cmd, (int)strlen(cmd));
    return drain_ok(fd);
}

/* Copy at most destsz-1 bytes from src into dest, always null-terminating. */
static void copy_field(char *dest, size_t destsz, const char *src) {
    size_t n = 0;
    while (n < destsz - 1 && src[n]) n++;
    memcpy(dest, src, n);
    dest[n] = '\0';
}

/* ── public API ──────────────────────────────────────────────────────────── */

int mpd_connect(const char *host, int port, char *banner_buf, int banner_len) {
    if (banner_buf && banner_len > 0) banner_buf[0] = '\0';

    int fd = net_tcp_connect(host, port);
    if (fd < 0) return MPD_ERR_TCP;

    /* Raw single recv() with sentinel fill. If recv() doesn't write to the
       buffer (broken stub), we'll see 0xCC bytes instead of the server data.
       Small (60 byte) buffer is plenty for "OK MPD x.y.z\n" (about 14 bytes). */
    unsigned char raw[60];
    memset(raw, 0xCC, sizeof(raw));
    int n = net_tcp_recv_raw(fd, raw, sizeof(raw));

    if (banner_buf && banner_len > 0) {
        /* Format: "rx=N AA BB CC DD EE FF GG HH" */
        int w = snprintf(banner_buf, banner_len, "rx=%d", n);
        int show = (n > 0 && n < 8) ? n : 8;
        for (int i = 0; i < show && w + 4 < banner_len; i++) {
            w += snprintf(banner_buf + w, banner_len - w, " %02X", raw[i]);
        }
    }

    if (n < 6 || memcmp(raw, "OK MPD", 6) != 0) {
        net_tcp_disconnect(fd);
        return MPD_ERR_BANNER;
    }
    return fd;
}

void mpd_disconnect(int fd) {
    if (fd < 0) return;
    net_tcp_send(fd, "close\n", 6);
    net_tcp_disconnect(fd);
}

int mpd_password(int fd, const char *pw) {
    char cmd[MAX_PASS_LEN + 16];
    snprintf(cmd, sizeof(cmd), "password %s\n", pw);
    return simple_cmd(fd, cmd);
}

int mpd_currentsong(int fd, MpdSong *song) {
    memset(song, 0, sizeof(*song));
    net_tcp_send(fd, "currentsong\n", 12);

    char line[LINE], val[LINE];
    while (1) {
        if (net_tcp_recv_line(fd, line, LINE) < 0) return -1;
        if (strcmp(line, "OK") == 0)               break;
        if (strncmp(line, "ACK", 3) == 0)          return -1;

        if (kv(line, "file",   val, LINE)) copy_field(song->file,   sizeof(song->file),   val);
        if (kv(line, "Title",  val, LINE)) copy_field(song->title,  sizeof(song->title),  val);
        if (kv(line, "Artist", val, LINE)) copy_field(song->artist, sizeof(song->artist), val);
        if (kv(line, "Album",  val, LINE)) copy_field(song->album,  sizeof(song->album),  val);
    }

    /* Fall back to filename when tag is absent */
    if (song->title[0] == '\0' && song->file[0] != '\0') {
        const char *slash = strrchr(song->file, '/');
        copy_field(song->title, sizeof(song->title), slash ? slash + 1 : song->file);
    }
    return 0;
}

int mpd_status(int fd, MpdStatus *status) {
    memset(status, 0, sizeof(*status));
    net_tcp_send(fd, "status\n", 7);

    char line[LINE], val[LINE];
    while (1) {
        if (net_tcp_recv_line(fd, line, LINE) < 0) return -1;
        if (strcmp(line, "OK") == 0)               break;
        if (strncmp(line, "ACK", 3) == 0)          return -1;

        if (kv(line, "state", val, LINE)) {
            if      (strcmp(val, "play")  == 0) status->state = MPD_STATE_PLAY;
            else if (strcmp(val, "pause") == 0) status->state = MPD_STATE_PAUSE;
            else                                status->state = MPD_STATE_STOP;
        }
        if (kv(line, "elapsed",  val, LINE)) status->elapsed  = (int)atof(val);
        if (kv(line, "duration", val, LINE)) status->duration = (int)atof(val);
        if (kv(line, "volume",   val, LINE)) status->volume   = atoi(val);
        /* "time: elapsed:total" integer fallback for older MPD */
        if (kv(line, "time", val, LINE) && status->elapsed == 0 && status->duration == 0) {
            int e = 0, d = 0;
            sscanf(val, "%d:%d", &e, &d);
            status->elapsed  = e;
            status->duration = d;
        }
    }
    return 0;
}

int mpd_play        (int fd) { return simple_cmd(fd, "play\n");     }
int mpd_toggle_pause(int fd) { return simple_cmd(fd, "pause\n");    }
int mpd_next        (int fd) { return simple_cmd(fd, "next\n");     }
int mpd_prev        (int fd) { return simple_cmd(fd, "previous\n"); }

static char s_last_ack[128] = "";

const char *mpd_last_ack(void) { return s_last_ack; }

/* Try one binary-fetching MPD command (albumart or readpicture).
   Returns total bytes on success, -1 on ACK/error. */
static int try_binary_fetch(int fd, const char *cmd_name, const char *uri,
                            unsigned char *out, int max_len) {
    int offset = 0;
    int total  = -1;

    while (1) {
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "%s \"%s\" %d\n", cmd_name, uri, offset);
        if (net_tcp_send(fd, cmd, (int)strlen(cmd)) < 0) return -1;

        char line[LINE];
        int chunk = 0;

        /* Parse header lines until we hit "binary: N", "OK", or "ACK". */
        for (;;) {
            int n = net_tcp_recv_line(fd, line, LINE);
            if (n < 0) return -1;
            if (strncmp(line, "ACK", 3) == 0) {
                snprintf(s_last_ack, sizeof(s_last_ack), "%s: %s", cmd_name, line);
                return -1;
            }
            if (strcmp(line, "OK") == 0)      return offset > 0 ? offset : -1;
            if (strncmp(line, "size: ", 6) == 0)   total = atoi(line + 6);
            else if (strncmp(line, "binary: ", 8) == 0) { chunk = atoi(line + 8); break; }
        }

        if (chunk <= 0)                         return -1;
        if (offset + chunk > max_len)           return -1;   /* won't fit */

        /* Read the binary payload. */
        if (net_tcp_recv_bytes(fd, out + offset, chunk) < 0) return -1;
        offset += chunk;

        /* MPD sends a trailing newline after the binary, then "OK". */
        if (net_tcp_recv_line(fd, line, LINE) < 0) return -1;     /* empty */
        if (strcmp(line, "OK") != 0) {
            /* The blank line may have been consumed; the actual OK is next. */
            if (net_tcp_recv_line(fd, line, LINE) < 0) return -1;
            if (strcmp(line, "OK") != 0) return -1;
        }

        if (total > 0 && offset >= total) return offset;
    }
}

int mpd_fetch_albumart(int fd, const char *uri, void *out_buf, int max_len) {
    if (!uri || !uri[0]) return -1;
    int n = try_binary_fetch(fd, "albumart", uri, out_buf, max_len);
    if (n > 0) return n;
    return try_binary_fetch(fd, "readpicture", uri, out_buf, max_len);
}
