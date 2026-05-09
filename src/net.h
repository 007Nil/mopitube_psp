#pragma once

/* Load net modules and initialise the PSP TCP/IP stack. */
int  net_init(void);

/* Connect to saved Wi-Fi profile (1-indexed). Blocks until IP is assigned
   or timeout (~10 s). Returns 0 on success. */
int  net_wifi_connect(int profile);

/* Drop the current Wi-Fi association without tearing down the net stack.
   Use this after a PSP suspend to force a fresh associate on resume. */
void net_wifi_disconnect(void);

void net_shutdown(void);

/* Open a blocking TCP connection to host_ip:port.
   host_ip must be a dotted-decimal string ("192.168.1.1").
   Returns socket fd >= 0 on success, -1 on error. */
int  net_tcp_connect(const char *host_ip, int port);
void net_tcp_disconnect(int fd);

int  net_tcp_send(int fd, const char *data, int len);

/* Read bytes until '\n'. Strips '\r'. Null-terminates buf.
   Returns number of bytes stored (excluding '\0'), -1 on error/timeout. */
int  net_tcp_recv_line(int fd, char *buf, int maxlen);

/* Single raw recv() call. Returns whatever recv() returns. */
int  net_tcp_recv_raw (int fd, void *buf, int maxlen);

/* Read exactly N bytes (uses any leftover line-buffered data first).
   Returns N on success, -1 on error/short. */
int  net_tcp_recv_bytes(int fd, void *buf, int n);

/* Last PSP inet errno (sceNetInetGetErrno wrapper). */
int  net_get_errno    (void);

/* After net_wifi_connect, fetch the PSP's IP / gateway as dotted-decimal
   strings. Buffers should be at least 16 bytes. Returns 0 on success. */
int  net_get_local_ip (char *buf, int buflen);
int  net_get_gateway  (char *buf, int buflen);
