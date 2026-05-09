#include <pspctrl.h>
#include "input.h"

static SceCtrlData g_prev;
static SceCtrlData g_curr;

static const unsigned int BTN_MASKS[BTN_COUNT] = {
    [BTN_PLAY_PAUSE] = PSP_CTRL_CROSS,
    [BTN_NEXT]       = PSP_CTRL_RTRIGGER,
    [BTN_PREV]       = PSP_CTRL_LTRIGGER,
};

void input_init(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    sceCtrlReadBufferPositive(&g_prev, 1);
    g_curr = g_prev;
}

void input_update(void) {
    g_prev = g_curr;
    sceCtrlReadBufferPositive(&g_curr, 1);
}

int input_pressed(Button btn) {
    unsigned int mask = BTN_MASKS[btn];
    return (g_curr.Buttons & mask) != 0 && (g_prev.Buttons & mask) == 0;
}
