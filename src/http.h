#pragma once

/* Tiny HTTP/1.0 client for Mopidy's JSON-RPC + image-fetch use cases only.
   Not a general-purpose client; assumes:
     - HTTP/1.0 (no chunked encoding)
     - Content-Length header always present on responses we care about
     - Same connect/disconnect cost is acceptable per request */

/* POST a JSON body to the given host:port + path. Reads response body into
   out_buf (max out_len). Returns body length on success, -1 on failure. */
int http_post_json(const char *host_ip, int port, const char *path,
                   const char *json_body,
                   void *out_buf, int out_len);

/* GET binary content from host:port + path. Returns length, -1 on failure. */
int http_get(const char *host_ip, int port, const char *path,
             void *out_buf, int out_len);
