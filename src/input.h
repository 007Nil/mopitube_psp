#pragma once

typedef enum {
    BTN_PLAY_PAUSE = 0,
    BTN_NEXT,
    BTN_PREV,
    BTN_COUNT,
} Button;

void input_init(void);

/* Call once per frame before querying buttons. */
void input_update(void);

/* Returns 1 on the first frame a button is pressed (edge-triggered). */
int input_pressed(Button btn);
