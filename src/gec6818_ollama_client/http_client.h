#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

/* HTTP 响应结构体。调用者必须通过 http_response_free() 释放。 */
typedef struct {
    int    status_code;  /* HTTP 状态码，如 200、404、500 */
    char  *body;         /* 响应正文（以 '\0' 结尾） */
    size_t body_len;     /* 正文长度（不含结尾 '\0'） */
    char   errmsg[256];  /* 网络错误时的错误描述 */
} HttpResponse;

/**
 * 发送 HTTP/1.1 POST 请求，携带 JSON 正文。
 *
 * @param host       服务器主机名或 IPv4 地址（如 "192.168.1.100"）
 * @param port       TCP 端口（Ollama 默认 11434）
 * @param path       请求路径（如 "/api/generate"）
 * @param json_body  要 POST 的 JSON 字符串
 * @param timeout_s  套接字超时秒数（0 = 不设超时）
 * @return           成功返回 HttpResponse*；网络错误时 status_code == -1 且 errmsg 有内容。
 *                   调用者必须用 http_response_free() 释放。
 */
HttpResponse* http_post(const char *host, int port, const char *path,
                        const char *json_body, int timeout_s);

void http_response_free(HttpResponse *resp);

#endif /* HTTP_CLIENT_H */
