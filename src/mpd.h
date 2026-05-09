#pragma once

typedef struct {
    char file[256];
    char title[128];
    char artist[128];
    char album[128];
} MpdSong;

typedef enum {
    MPD_STATE_UNKNOWN = 0,
    MPD_STATE_STOP,
    MPD_STATE_PLAY,
    MPD_STATE_PAUSE,
} MpdState;

typedef struct {
    MpdState state;
    int      elapsed;   /* seconds */
    int      duration;  /* seconds */
    int      volume;
} MpdStatus;

/* mpd_connect return codes (negative = failure). */
#define MPD_ERR_TCP     (-1)   /* TCP socket/connect failed */
#define MPD_ERR_BANNER  (-2)   /* Connected but no/bad MPD banner */

/* Connect to MPD, consume the OK banner.
   Returns fd >= 0 on success, MPD_ERR_* on failure.
   On MPD_ERR_BANNER, banner_buf (if non-NULL) is filled with a diagnostic
   string showing what the server actually sent. */
int  mpd_connect(const char *host, int port, char *banner_buf, int banner_len);
void mpd_disconnect(int fd);

/* Send PASSWORD command. Returns 0 on OK. */
int  mpd_password(int fd, const char *pw);

/* Fetch current song / server status into the provided structs.
   Returns 0 on success, -1 on socket/protocol error. */
int  mpd_currentsong(int fd, MpdSong   *song);
int  mpd_status     (int fd, MpdStatus *status);

/* Transport commands. Return 0 on OK. */
int  mpd_play        (int fd);
int  mpd_toggle_pause(int fd);  /* pause (toggles) */
int  mpd_next        (int fd);
int  mpd_prev        (int fd);

/* Fetch album art for the given URI into out_buf.
   Tries `albumart` first, falls back to `readpicture`.
   Returns total bytes received on success, -1 on error/no art. */
int  mpd_fetch_albumart(int fd, const char *uri, void *out_buf, int max_len);

/* Last ACK string captured during an MPD command (for diagnostics). */
const char *mpd_last_ack(void);
