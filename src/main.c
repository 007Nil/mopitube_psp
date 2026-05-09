#include <pspkernel.h>
#include <psprtc.h>
#include <psppower.h>
#include <stdio.h>

#include "config.h"
#include "net.h"
#include "mpd.h"
#include "input.h"
#include "ui.h"
#include "artwork.h"

PSP_MODULE_INFO("MopiTube", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(20480);

static volatile int g_running       = 1;
static volatile int g_resume_pending = 0;  /* set by power cb on resume */
static volatile int g_was_suspended  = 0;  /* sticky: skip net teardown on exit */

/* ── PSP exit callback ───────────────────────────────────────────────────── */

static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    g_running = 0;
    return 0;
}

/* PSP power callback. Flags are a bitfield (PSP_POWER_CB_*).
   We treat suspend as a destructive event: the Wi-Fi link goes down and
   any open sockets become invalid. On resume we set g_resume_pending so
   the main loop can drop its MPD socket and reconnect. */
static int power_callback(int unknown, int pwrflags, void *common) {
    (void)unknown; (void)common;
    if (pwrflags & PSP_POWER_CB_SUSPENDING)       g_was_suspended = 1;
    if (pwrflags & PSP_POWER_CB_RESUME_COMPLETE)  g_resume_pending = 1;
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int exit_cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(exit_cbid);

    int pwr_cbid = sceKernelCreateCallback("Power Callback", power_callback, NULL);
    scePowerRegisterCallback(0, pwr_cbid);

    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("callback_thread", callback_thread,
                                     0x11, 0xFA0, PSP_THREAD_ATTR_USER, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
}

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Skip net_shutdown() if a suspend was ever observed: the PSP net stack
   is left in a state where sceUtilityUnloadNetModule() can deadlock the
   kernel, hanging the whole device. sceKernelExitGame() reclaims the
   resources anyway. */
static void safe_shutdown(void) {
    if (!g_was_suspended) net_shutdown();
    sceKernelExitGame();
}

/* Block with a message until HOME is pressed, then clean up. */
static void fatal(const char *msg) {
    ui_draw_status(msg);
    while (g_running) sceKernelDelayThread(100 * 1000);
    safe_shutdown();
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void) {
    setup_callbacks();
    ui_init();
    artwork_init();

    /* Config */
    ui_draw_status("Loading config...");
    {
        int cfg = config_load();
        if (cfg == CONFIG_ERR_MISSING)
            fatal("ERROR: config not found.\n\nCreate config.txt at:\n  ms0:/PSP/GAME/MopiTube/config.txt\n\nSee config.example.txt for format.\n\nPress HOME to quit.");
        else if (cfg == CONFIG_ERR_INVALID)
            fatal("ERROR: invalid value in config.txt\n\nCheck: host is non-empty, port 1-65535,\nwifi_profile 1-9.\n\nSee config.example.txt for format.\n\nPress HOME to quit.");
    }

    /* Wi-Fi */
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "Connecting to Wi-Fi\n(profile %d) ...", g_config.wifi_profile);
        ui_draw_status(msg);
    }
    if (net_init() != 0)
        fatal("ERROR: net_init() failed.\n\nPress HOME to quit.");
    if (net_wifi_connect(g_config.wifi_profile) != 0)
        fatal("ERROR: Wi-Fi connection failed.\n\nCheck wifi_profile in config.txt\nand PSP Network Settings.\n\nPress HOME to quit.");

    /* MPD */
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "Connecting to MPD\n%s:%d ...", g_config.host, g_config.port);
        ui_draw_status(msg);
    }
    char banner[64] = "";
    int mpd_fd = mpd_connect(g_config.host, g_config.port, banner, sizeof(banner));
    if (mpd_fd < 0) {
        char local_ip[16] = "?", gateway[16] = "?";
        net_get_local_ip(local_ip, sizeof(local_ip));
        net_get_gateway (gateway,  sizeof(gateway));
        int err = net_get_errno();

        const char *reason = (mpd_fd == MPD_ERR_BANNER)
            ? "TCP ok, bad/no banner"
            : "TCP connect failed";

        char msg[480];
        snprintf(msg, sizeof(msg),
            "ERROR: %s\n\n"
            "Server : %s:%d\n"
            "PSP IP : %s\n"
            "Gateway: %s\n"
            "Errno  : %d (0x%X)\n"
            "Got    : %s\n\n"
            "Press HOME to quit.",
            reason, g_config.host, g_config.port, local_ip, gateway,
            err, (unsigned)err, banner);
        fatal(msg);
    }
    if (g_config.password[0] != '\0')
        mpd_password(mpd_fd, g_config.password);

    input_init();

    MpdSong   song   = {0};
    MpdStatus status = {0};

    u64 tick_res      = sceRtcGetTickResolution();      /* ticks per second */
    u64 poll_interval = tick_res / 2;                   /* 500 ms */
    u64 last_poll     = 0;
    int dirty         = 1;                              /* force initial draw */

    /* ── main loop ───────────────────────────────────────────────────────── */
    while (g_running) {
        /* Resume from suspend: the Wi-Fi link is gone and the MPD socket
           is dead. Drop everything and reconnect before doing anything
           else this iteration. */
        if (g_resume_pending) {
            g_resume_pending = 0;
            ui_draw_status("Resumed. Reconnecting Wi-Fi...");

            mpd_disconnect(mpd_fd);
            mpd_fd = -1;
            artwork_clear();

            /* Force apctl back to disconnected so net_wifi_connect() does
               a fresh associate. The state after resume is unreliable. */
            net_wifi_disconnect();
            sceKernelDelayThread(500 * 1000);

            if (net_wifi_connect(g_config.wifi_profile) != 0) {
                ui_draw_status("Wi-Fi reconnect failed.\nWaiting...");
                sceKernelDelayThread(3000 * 1000);
                continue;   /* try again next loop */
            }

            ui_draw_status("Wi-Fi back. Reconnecting MPD...");
            mpd_fd = mpd_connect(g_config.host, g_config.port, NULL, 0);
            if (mpd_fd < 0) {
                ui_draw_status("MPD reconnect failed.\nRetrying...");
                sceKernelDelayThread(3000 * 1000);
                continue;
            }
            if (g_config.password[0] != '\0')
                mpd_password(mpd_fd, g_config.password);

            last_poll = 0;
            dirty     = 1;
            continue;
        }

        input_update();

        /* Transport commands */
        if (input_pressed(BTN_PLAY_PAUSE)) {
            if (status.state == MPD_STATE_STOP)
                mpd_play(mpd_fd);
            else
                mpd_toggle_pause(mpd_fd);
            last_poll = 0;  /* refresh immediately */
        }
        if (input_pressed(BTN_NEXT)) {
            mpd_next(mpd_fd);
            last_poll = 0;
        }
        if (input_pressed(BTN_PREV)) {
            mpd_prev(mpd_fd);
            last_poll = 0;
        }

        /* Poll MPD state at 2 Hz */
        u64 now;
        sceRtcGetCurrentTick(&now);
        if (last_poll == 0 || (now - last_poll) >= poll_interval) {
            if (mpd_currentsong(mpd_fd, &song) < 0 ||
                mpd_status(mpd_fd, &status)    < 0) {
                /* Lost connection — attempt one reconnect */
                mpd_disconnect(mpd_fd);
                sceKernelDelayThread(2000 * 1000);
                mpd_fd = mpd_connect(g_config.host, g_config.port, NULL, 0);
                if (mpd_fd < 0) {
                    ui_draw_status("MPD disconnected. Retrying...");
                    sceKernelDelayThread(3000 * 1000);
                    continue;
                }
                if (g_config.password[0] != '\0')
                    mpd_password(mpd_fd, g_config.password);
                last_poll = 0;
                continue;   /* poll fresh state next iteration */
            }
            sceRtcGetCurrentTick(&last_poll);
            /* Fetch art for the current song (no-op if URI unchanged). */
            if (song.file[0]) artwork_load(mpd_fd, song.file);
            else              artwork_clear();
            dirty = 1;
        }

        if (dirty) {
            ui_draw_now_playing(&song, &status);
            dirty = 0;
        }
        sceKernelDelayThread(16 * 1000);  /* input poll cadence */
    }

    /* Cleanup */
    if (mpd_fd >= 0) mpd_disconnect(mpd_fd);
    safe_shutdown();
    return 0;
}
