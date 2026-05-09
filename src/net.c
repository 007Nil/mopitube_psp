#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspnet_resolver.h>
#include <psputility.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include "net.h"

/* Retry budget for sceNetApctlGetState polling: 50 ms * 200 = 10 s */
#define APCTL_POLL_INTERVAL_US  50000
#define APCTL_MAX_POLLS         200

/* ── recv line buffer (single connection assumed) ────────────────────────── */
static char s_rx[512];
static int  s_rxn = 0;   /* valid bytes in buffer */
static int  s_rxi = 0;   /* read position */

static void rx_reset(void) { s_rxn = s_rxi = 0; }

/* ── public API ──────────────────────────────────────────────────────────── */

int net_init(void) {
    int ret;
    if ((ret = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON)) < 0) return ret;
    if ((ret = sceUtilityLoadNetModule(PSP_NET_MODULE_INET))   < 0) return ret;

    if ((ret = sceNetInit(128 * 1024, 42, 4096, 42, 4096)) < 0) return ret;
    if ((ret = sceNetInetInit())     < 0) return ret;
    if ((ret = sceNetResolverInit()) < 0) return ret;
    if ((ret = sceNetApctlInit(0x8000, 48)) < 0) return ret;
    return 0;
}

void net_shutdown(void) {
    sceNetApctlTerm();
    sceNetResolverTerm();
    sceNetInetTerm();
    sceNetTerm();
    sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
    sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
}

void net_wifi_disconnect(void) {
    sceNetApctlDisconnect();
}

int net_wifi_connect(int profile) {
    int state = 0;

    /* If a previous run left us already associated, reuse it. */
    sceNetApctlGetState(&state);
    if (state == PSP_NET_APCTL_STATE_GOT_IP) return 0;

    /* Tear down any half-initialised state from a prior crash. */
    if (state != PSP_NET_APCTL_STATE_DISCONNECTED) {
        sceNetApctlDisconnect();
        sceKernelDelayThread(500 * 1000);
    }

    if (sceNetApctlConnect(profile) < 0) return -1;

    for (int i = 0; i < APCTL_MAX_POLLS; i++) {
        sceNetApctlGetState(&state);
        if (state == PSP_NET_APCTL_STATE_GOT_IP) return 0;
        sceKernelDelayThread(APCTL_POLL_INTERVAL_US);
    }
    return -1;  /* timeout */
}

int net_tcp_connect(const char *host_ip, int port) {
    int sock = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    /* 2 s — short enough that we recover quickly when the link goes down
       (e.g. after a PSP suspend), long enough for normal MPD round-trips. */
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    sceNetInetSetsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sceNetInetSetsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr(host_ip);

    if (sceNetInetConnect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        sceNetInetClose(sock);
        return -1;
    }
    return sock;
}

void net_tcp_disconnect(int fd) {
    if (fd >= 0) sceNetInetClose(fd);
    rx_reset();
}

void net_tcp_shutdown(int fd) {
    /* SHUT_RDWR (=2) — fail any in-flight or future I/O on this fd
       without releasing it. Used from the power callback to break out
       of a recv() that would otherwise block forever after resume. */
    if (fd >= 0) sceNetInetShutdown(fd, 2);
}

int net_tcp_send(int fd, const char *data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = sceNetInetSend(fd, data + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return sent;
}

static int apctl_str(int code, char *buf, int buflen) {
    union SceNetApctlInfo info;
    if (sceNetApctlGetInfo(code, &info) < 0) { buf[0] = '\0'; return -1; }
    /* The string fields (ip, gateway, etc.) live in the first 16 bytes. */
    int i = 0;
    while (i < buflen - 1 && info.ip[i]) { buf[i] = info.ip[i]; i++; }
    buf[i] = '\0';
    return 0;
}

int net_get_local_ip(char *buf, int buflen) {
    return apctl_str(PSP_NET_APCTL_INFO_IP, buf, buflen);
}

int net_get_gateway(char *buf, int buflen) {
    return apctl_str(PSP_NET_APCTL_INFO_GATEWAY, buf, buflen);
}

int net_tcp_recv_raw(int fd, void *buf, int maxlen) {
    /* Use the PSP-native call directly. The POSIX recv() wrapper on this
       toolchain has been observed returning bogus counts without writing
       to the buffer; sceNetInetRecv goes straight to the inet stack. */
    return sceNetInetRecv(fd, buf, maxlen, 0);
}

int net_tcp_recv_bytes(int fd, void *buf, int n) {
    char *out = (char *)buf;
    int got = 0;
    /* Drain anything sitting in the line buffer first */
    while (got < n && s_rxi < s_rxn) {
        out[got++] = s_rx[s_rxi++];
    }
    while (got < n) {
        int r = sceNetInetRecv(fd, out + got, n - got, 0);
        if (r <= 0) return -1;
        got += r;
    }
    return got;
}

int net_get_errno(void) {
    return sceNetInetGetErrno();
}

int net_tcp_recv_line(int fd, char *buf, int maxlen) {
    int n = 0;
    while (1) {
        if (s_rxi >= s_rxn) {
            s_rxn = sceNetInetRecv(fd, s_rx, (int)sizeof(s_rx), 0);
            if (s_rxn <= 0) { rx_reset(); return -1; }
            s_rxi = 0;
        }
        char c = s_rx[s_rxi++];
        if (c == '\n') break;
        if (c == '\r') continue;
        if (n < maxlen - 1) buf[n++] = c;
        /* else: line too long — keep consuming until '\n' without storing */
    }
    buf[n] = '\0';
    return n;
}
