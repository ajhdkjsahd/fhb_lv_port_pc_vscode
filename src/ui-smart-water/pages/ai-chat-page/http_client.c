// ========== http_client.c ==========
// Lightweight HTTP/1.1 POST client — pure POSIX sockets.
// Uses select() for portable timeout (SO_RCVTIMEO not reliable on all ARM kernels).
#ifdef __linux__

#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

/* Global timeout for select() calls — set once per request */
static int g_http_timeout_s = 120;

/* Read one byte with select() timeout. Returns 1 on success, 0 on timeout, -1 on error. */
static int recv_byte(int fd, char *c, int timeout_s)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    struct timeval tv;
    tv.tv_sec  = timeout_s;
    tv.tv_usec = 0;

    int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) return ret;  /* 0 = timeout, -1 = error */

    return recv(fd, c, 1, 0) > 0 ? 1 : -1;
}

/* Read one line (up to \n), strip \r, NUL-terminate.
 * Uses select() timeout per byte. Returns byte count or -1. */
static ssize_t readline(int fd, char *buf, size_t max)
{
    size_t i = 0;
    while (i < max - 1) {
        char c;
        int rc = recv_byte(fd, &c, g_http_timeout_s);
        if (rc <= 0) return -1;
        if (c == '\n') {
            if (i && buf[i - 1] == '\r') i--;
            buf[i] = '\0';
            return (ssize_t)i;
        }
        buf[i++] = c;
    }
    return -1;
}

/* Dynamic buffer append. Returns 0 on success, -1 on overflow. */
static int buf_add(char **p, size_t *cap, size_t *len, const char *src, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t newc = (*cap) * 2;
        if (newc < *len + n + 1) newc = *len + n + 1;
        if (newc > 1048576) return -1;  /* 1 MB cap */
        char *t = realloc(*p, newc);
        if (!t) return -1;
        *p = t; *cap = newc;
    }
    memcpy(*p + *len, src, n);
    *len += n;
    return 0;
}

HttpResponse* http_post(const char *host, int port, const char *path,
                        const char *body, int timeout_s)
{
    g_http_timeout_s = (timeout_s > 0) ? timeout_s : 120;

    HttpResponse *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->status_code = -1;
    int fd = -1;

    /* DNS */
    struct hostent *he = gethostbyname(host);
    if (!he) {
        snprintf(r->errmsg, sizeof(r->errmsg), "DNS: %s", host);
        return r;
    }

    /* Socket */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(r->errmsg, sizeof(r->errmsg), "socket()");
        return r;
    }

    /* Set send timeout via setsockopt (best effort, may fail on some kernels) */
    struct timeval tv = { .tv_sec = timeout_s > 0 ? timeout_s : 120, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Connect with select-based timeout */
    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_port   = htons((unsigned short)port) };
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    /* Set non-blocking for connect */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int conn_ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (conn_ret < 0 && errno == EINPROGRESS) {
        /* Wait for connect to complete */
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval ct;
        ct.tv_sec  = (timeout_s > 0) ? timeout_s : 120;
        ct.tv_usec = 0;
        int sel = select(fd + 1, NULL, &wfds, NULL, &ct);
        if (sel <= 0) {
            snprintf(r->errmsg, sizeof(r->errmsg), "connect %s:%d: timeout",
                     host, port);
            close(fd); return r;
        }
    } else if (conn_ret < 0) {
        snprintf(r->errmsg, sizeof(r->errmsg), "connect %s:%d: %s",
                 host, port, strerror(errno));
        close(fd); return r;
    }

    /* Restore blocking mode */
    fcntl(fd, F_SETFL, flags);

    /* Send request */
    char req[32768];
    int n = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s",
        path, host, port, strlen(body), body);
    if (n < 0 || (size_t)n >= sizeof(req)) {
        snprintf(r->errmsg, sizeof(r->errmsg), "request too large");
        close(fd); return r;
    }

    size_t sent = 0;
    while (sent < (size_t)n) {
        ssize_t w = send(fd, req + sent, (size_t)n - sent, 0);
        if (w <= 0) {
            snprintf(r->errmsg, sizeof(r->errmsg), "send()");
            close(fd); return r;
        }
        sent += (size_t)w;
    }

    /* Status line */
    char line[4096];
    if (readline(fd, line, sizeof(line)) < 0) {
        snprintf(r->errmsg, sizeof(r->errmsg), "no response (timeout=%ds)",
                 g_http_timeout_s);
        close(fd); return r;
    }
    char *sp = strchr(line, ' ');
    r->status_code = sp ? atoi(sp + 1) : -1;

    /* Response headers */
    char hdr[4096] = "";
    while (1) {
        ssize_t ln = readline(fd, line, sizeof(line));
        if (ln < 0) {
            snprintf(r->errmsg, sizeof(r->errmsg), "headers");
            close(fd); return r;
        }
        if (ln == 0) break;  /* empty line = end of headers */
        if (strlen(hdr) + (size_t)ln + 3 < sizeof(hdr)) {
            strcat(hdr, line); strcat(hdr, "\n");
        }
    }

    /* Check transfer encoding */
    int chunked = (strstr(hdr, "Transfer-Encoding: chunked") != NULL ||
                   strstr(hdr, "transfer-encoding: chunked") != NULL);
    char *cl_hdr = strstr(hdr, "Content-Length:");
    if (!cl_hdr) cl_hdr = strstr(hdr, "content-length:");
    size_t expected = cl_hdr ? (size_t)atol(cl_hdr + 15) : 0;

    /* Read body */
    size_t cap = (expected > 16384) ? expected + 1 : 16384;
    char *data = malloc(cap);
    size_t total = 0;
    if (!data) {
        snprintf(r->errmsg, sizeof(r->errmsg), "oom");
        close(fd); return r;
    }

    if (chunked) {
        while (1) {
            if (readline(fd, line, sizeof(line)) < 0) goto body_err;
            size_t cs = (size_t)strtol(line, NULL, 16);
            if (cs == 0) break;
            while (cs > 0) {
                char tmp[4096];
                size_t rd = cs < sizeof(tmp) ? cs : sizeof(tmp);
                ssize_t got = recv(fd, tmp, rd, 0);
                if (got <= 0) goto body_err;
                if (buf_add(&data, &cap, &total, tmp, (size_t)got) < 0) goto body_err;
                cs -= (size_t)got;
            }
            readline(fd, line, sizeof(line));  /* chunk trailing \r\n */
        }
    } else if (cl_hdr) {
        while (total < expected) {
            char tmp[4096];
            size_t want = expected - total;
            if (want > sizeof(tmp)) want = sizeof(tmp);
            ssize_t got = recv(fd, data + total, want, 0);
            if (got <= 0) break;
            total += (size_t)got;
        }
    } else {
        while (1) {
            char tmp[4096];
            ssize_t got = recv(fd, tmp, sizeof(tmp), 0);
            if (got <= 0) break;
            if (buf_add(&data, &cap, &total, tmp, (size_t)got) < 0) goto body_err;
        }
    }

    close(fd);
    data[total] = '\0';
    r->body = data;
    r->body_len = total;
    return r;

body_err:
    free(data);
    close(fd);
    snprintf(r->errmsg, sizeof(r->errmsg), "body read error");
    return r;
}

void http_response_free(HttpResponse *r)
{
    if (r) { free(r->body); free(r); }
}

#endif /* __linux__ */
