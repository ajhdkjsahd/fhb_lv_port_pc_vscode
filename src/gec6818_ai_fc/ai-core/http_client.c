/*
 * http_client.c - POSIX socket HTTP/1.1 客户端
 *
 * WSL2 Ubuntu / GEC6818 (Linux) 通用
 * 不支持 chunked transfer-encoding, Ollama stream:false 模式下没问题
 */
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>

void http_response_free(HttpResponse *r)
{
    if (!r) return;
    free(r->body);
    free(r);
}

static int connect_host(const char *host, int port, int timeout_s)
{
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0) {
        return -1;
    }

    int fd = -1;
    for (p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv = { .tv_sec = timeout_s, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

HttpResponse* http_post(const char *host, int port,
                        const char *path, const char *body,
                        int timeout_s)
{
    int fd = connect_host(host, port, timeout_s);
    if (fd < 0) {
        HttpResponse *r = calloc(1, sizeof(*r));
        r->status_code = -1;
        snprintf(r->errmsg, sizeof(r->errmsg),
                 "connect %s:%d failed: %s", host, port, strerror(errno));
        return r;
    }

    /* 拼请求行 + 头 + body */
    size_t body_len = strlen(body);
    int req_len = 0;
    char *req = malloc(body_len + 512);
    if (!req) { close(fd); return NULL; }

    req_len = snprintf(req, body_len + 512,
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path, host, port, body_len, body);

    ssize_t sent = 0;
    while (sent < req_len) {
        ssize_t n = write(fd, req + sent, req_len - sent);
        if (n <= 0) {
            free(req); close(fd);
            HttpResponse *r = calloc(1, sizeof(*r));
            r->status_code = -1;
            snprintf(r->errmsg, sizeof(r->errmsg), "write: %s", strerror(errno));
            return r;
        }
        sent += n;
    }
    free(req);

    /* 接收完整响应 */
    size_t cap = 32768, len = 0;
    char *buf = malloc(cap);
    if (!buf) { close(fd); return NULL; }

    ssize_t n;
    while ((n = read(fd, buf + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *t = realloc(buf, cap);
            if (!t) { free(buf); close(fd); return NULL; }
            buf = t;
        }
    }
    buf[len] = '\0';
    close(fd);

    /* 解析 status_code */
    int status = 0;
    sscanf(buf, "HTTP/%*s %d", &status);

    /* 找 body (\r\n\r\n 之后) */
    char *body_start = strstr(buf, "\r\n\r\n");
    HttpResponse *r = calloc(1, sizeof(*r));
    r->status_code = status;
    if (body_start) {
        body_start += 4;
        size_t blen = len - (size_t)(body_start - buf);
        r->body = malloc(blen + 1);
        memcpy(r->body, body_start, blen);
        r->body[blen] = '\0';
        r->body_len = blen;
    } else {
        r->body = strdup("");
        r->body_len = 0;
    }

    free(buf);
    return r;
}
