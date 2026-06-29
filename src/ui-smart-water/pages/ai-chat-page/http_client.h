// ========== http_client.h ==========
// Lightweight HTTP/1.1 POST — pure POSIX sockets, zero dependencies.
// Works on both PC (MinGW/MSYS2) and ARM Linux (GEC6818).
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H
#include <stddef.h>

typedef struct {
    int    status_code;  /* HTTP status code (200, 404, …), -1 on network error */
    char  *body;         /* Response body (NUL-terminated) */
    size_t body_len;     /* Body length (excluding NUL) */
    char   errmsg[256];  /* Error description when status_code == -1 */
} HttpResponse;

/** POST JSON to an HTTP/1.1 server.
 *  Returns malloc'd HttpResponse* — caller must http_response_free().
 *  On network error, status_code == -1 and errmsg is set. */
HttpResponse* http_post(const char *host, int port, const char *path,
                        const char *json_body, int timeout_s);

void http_response_free(HttpResponse *resp);

#endif
