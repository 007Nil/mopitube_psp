#pragma once

#define ARTWORK_W 128
#define ARTWORK_H 128

/* Set up internal state. Call once at startup. */
void artwork_init(void);

/* Fetch + decode artwork for the given URI.
   Skips if the URI matches what we already have loaded.
   Returns 1 on success (or already-loaded), 0 on no-art / failure. */
int  artwork_load(int mpd_fd, const char *uri);

/* Drop the current artwork (e.g. when the song clears). */
void artwork_clear(void);

/* Whether a valid texture is currently in memory. */
int  artwork_is_valid(void);

/* Render the cached texture as a `size`x`size` quad. Must be called inside
   sceGuStart/sceGuFinish. No-op if no artwork is loaded. */
void artwork_draw(float x, float y, float size);

/* Status of the most recent fetch attempt (for diagnostic display). */
typedef enum {
    AW_STATUS_NONE = 0,    /* no attempt yet */
    AW_STATUS_OK,          /* loaded and decoded */
    AW_STATUS_NO_DATA,     /* mpd_fetch_albumart returned <= 0 */
    AW_STATUS_NOT_JPEG,    /* received bytes weren't JPEG */
    AW_STATUS_DECODE_FAIL, /* libjpeg failed */
} ArtStatus;

ArtStatus artwork_status(void);
int       artwork_last_bytes(void);   /* bytes received from MPD */
unsigned int artwork_first_pixel(void); /* first pixel of decoded texture (debug) */
