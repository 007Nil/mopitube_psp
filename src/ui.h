#pragma once
#include "mpd.h"

void ui_init(void);

/* Show a plain status/error message (startup, errors). */
void ui_draw_status(const char *msg);

/* Render the Now Playing screen. Called every frame. */
void ui_draw_now_playing(const MpdSong *song, const MpdStatus *status);
