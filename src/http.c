#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "http.h"
#include "net.h"

/* Read response: status line + headers + body.
   Returns body length on 2xx success, -1 otherwise.
   Caller must have already sent the request. */
static int read_response(int fd, void *out_buf, int out_len) {
    char line[1024];

    /* Status line: "HTTP/1.x CODE REASON" */
    int n = net_tcp_recv_line(fd, line, sizeof(line));
    if (n < 12) return -1;
    if (strncmp(line, "HTTP/1.", 7) != 0) return -1;
    int code = atoi(line + 9);
    if (code < 200 || code >= 300) return -1;

    /* Headers — we just want Content-Length. */
    int content_length = -1;
    while (1) {
        n = net_tcp_recv_line(fd, line, sizeof(line));
        if (n < 0) return -1;
        if (n == 0) break;          /* blank line = end of headers */
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            const char *v = line + 15;
            while (*v == ' ') v++;
            content_length = atoi(v);
        }
    }
    if (content_length < 0) return -1;
    if (content_length > out_len) return -1;

    if (net_tcp_recv_bytes(fd, out_buf, content_length) < 0) return -1;
    return content_length;
}

int http_post_json(const char *host_ip, int port, const char *path,
                   const char *json_body,
                   void *out_buf, int out_len) {
    int fd = net_tcp_connect(host_ip, port);
    if (fd < 0) return -1;

    int body_len = (int)strlen(json_body);
    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "POST %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host_ip, port, body_len);
    if (hdr_len >= (int)sizeof(hdr)) { net_tcp_disconnect(fd); return -1; }

    if (net_tcp_send(fd, hdr, hdr_len) < 0 ||
        net_tcp_send(fd, json_body, body_len) < 0) {
        net_tcp_disconnect(fd);
        return -1;
    }

    int rc = read_response(fd, out_buf, out_len);
    net_tcp_disconnect(fd);
    return rc;
}

int http_get(const char *host_ip, int port, const char *path,
             void *out_buf, int out_len) {
    int fd = net_tcp_connect(host_ip, port);
    if (fd < 0) return -1;

    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "GET %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host_ip, port);
    if (hdr_len >= (int)sizeof(hdr)) { net_tcp_disconnect(fd); return -1; }

    if (net_tcp_send(fd, hdr, hdr_len) < 0) {
        net_tcp_disconnect(fd);
        return -1;
    }

    int rc = read_response(fd, out_buf, out_len);
    net_tcp_disconnect(fd);
    return rc;
}
