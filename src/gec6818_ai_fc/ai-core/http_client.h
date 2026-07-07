/*
 * http_client.h - 极简 HTTP/1.1 客户端
 *
 * 只为 AI Agent 服务: POST JSON, 接收完整响应, 不支持 chunked
 * WSL2 / GEC6818 通用 (纯 POSIX socket)
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

typedef struct HttpResponse {
    int    status_code;   /* HTTP 状态码, -1 = 网络错误 */
    char  *body;         /* malloc'd, NUL 终止 */
    size_t body_len;
    char   errmsg[128];  /* status_code == -1 时填 */
} HttpResponse;

/* 同步 POST (阻塞 timeout_s 秒). 调用方负责 http_response_free */
HttpResponse* http_post(const char *host, int port,
                        const char *path, const char *body,
                        int timeout_s);

void http_response_free(HttpResponse *r);

#endif
