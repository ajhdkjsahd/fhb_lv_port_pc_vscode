/*
 * http_client.c — 轻量 HTTP/1.1 POST 客户端（纯 POSIX socket，零依赖）
 */

#define _GNU_SOURCE
#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

/* 读一行（以 \n 结尾），去掉 \r，返回字节数，出错返回 -1 */
static ssize_t readline(int fd, char *buf, size_t max) {
    size_t i = 0;
    while (i < max - 1) {
        char c;
        if (recv(fd, &c, 1, 0) <= 0) return -1;
        if (c == '\n') { if (i && buf[i-1] == '\r') i--; buf[i] = 0; return (ssize_t)i; }
        buf[i++] = c;
    }
    return -1;
}

/* 动态缓冲追加 */
static int buf_add(char **p, size_t *cap, size_t *len, const char *src, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t newc = (*cap) * 2;
        if (newc < *len + n + 1) newc = *len + n + 1;
        if (newc > 1048576) return -1;  /* 1 MB 上限 */
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
    HttpResponse *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->status_code = -1;
    int fd = -1;

    /* 解析域名 */
    struct hostent *he = gethostbyname(host);
    if (!he) { snprintf(r->errmsg, sizeof(r->errmsg), "DNS: %s", host); return r; }

    /* 建 socket */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(r->errmsg, sizeof(r->errmsg), "socket"); return r; }

    /* 超时 */
    if (timeout_s > 0) {
        struct timeval tv = { .tv_sec = timeout_s, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    /* 连接 */
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((unsigned short)port) };
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        snprintf(r->errmsg, sizeof(r->errmsg), "connect %s:%d: %s", host, port, strerror(errno));
        close(fd); return r;
    }

    /* 发请求 */
    char req[16384];
    int n = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
        path, host, port, strlen(body), body);
    if (n < 0 || (size_t)n >= sizeof(req)) { snprintf(r->errmsg, sizeof(r->errmsg), "req too big"); close(fd); return r; }

    size_t sent = 0;
    while (sent < (size_t)n) {
        ssize_t w = send(fd, req + sent, (size_t)n - sent, 0);
        if (w <= 0) { snprintf(r->errmsg, sizeof(r->errmsg), "send"); close(fd); return r; }
        sent += (size_t)w;
    }

    /* 状态行 */
    char line[4096];
    if (readline(fd, line, sizeof(line)) < 0) {
        snprintf(r->errmsg, sizeof(r->errmsg), "no response"); close(fd); return r;
    }
    char *sp = strchr(line, ' ');
    r->status_code = sp ? atoi(sp + 1) : -1;

    /* 响应头（拼接成 \n 分隔的字符串，方便 strcasestr） */
    char hdr[4096] = "";
    while (1) {
        ssize_t ln = readline(fd, line, sizeof(line));
        if (ln < 0) { snprintf(r->errmsg, sizeof(r->errmsg), "headers"); close(fd); return r; }
        if (ln == 0) break;
        if (strlen(hdr) + (size_t)ln + 3 < sizeof(hdr)) { strcat(hdr, line); strcat(hdr, "\n"); }
    }

    int chunked = strcasestr(hdr, "transfer-encoding: chunked") != NULL;
    char *cl_hdr = strcasestr(hdr, "content-length:");
    size_t expected = cl_hdr ? (size_t)atol(cl_hdr + 15) : 0;

    /* 读正文 */
    size_t cap = (expected > 16384) ? expected + 1 : 16384;
    char *data = malloc(cap);
    size_t total = 0;
    if (!data) { snprintf(r->errmsg, sizeof(r->errmsg), "oom"); close(fd); return r; }

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
            readline(fd, line, sizeof(line));  /* 块尾 \r\n */
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
    data[total] = 0;
    r->body = data;
    r->body_len = total;
    return r;

body_err:
    free(data);
    close(fd);
    snprintf(r->errmsg, sizeof(r->errmsg), "body read error");
    return r;
}

void http_response_free(HttpResponse *r) { if (r) { free(r->body); free(r); } }
