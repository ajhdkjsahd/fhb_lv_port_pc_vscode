/*
 * main.c — GEC6818 Ollama 客户端（零依赖）
 *
 * 用法:
 *   交互模式:  ./ollama_client <IP> [端口]
 *   单次模式:  ./ollama_client <IP> [端口] "问题"
 */

#define _GNU_SOURCE
#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== 迷你 JSON 取值 + 转义 ========== */

/* hex 字符 → 值，非法返回 -1 */
static int xd(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 从扁平 JSON 中提取 key 对应的字符串值。返回 malloc 内存，调用者 free。 */
static char* json_val(const char *s, const char *key) {
    char pat[256];
    int pl = snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    if (pl < 1 || pl >= (int)sizeof(pat)) return NULL;

    const char *p = strstr(s, pat);
    if (!p) return NULL;
    p += pl;
    const char *end = p;
    while (*end && *end != '"') { if (*end == '\\' && end[1]) end++; end++; }
    if (!*end) return NULL;

    char *out = malloc((size_t)(end - p) + 1);
    if (!out) return NULL;

    char *d = out;
    while (p < end) {
        if (*p != '\\' || p + 1 >= end) { *d++ = *p++; continue; }
        p++;
        if (*p == 'u' && p + 4 < end) {     /* \uXXXX —— 只翻译 < > 两个 */
            p++;
            int hi = xd(p[0]), lo = (hi >= 0) ? xd(p[1]) : -1;
            int cp = (hi >= 0 && lo >= 0) ? (hi << 12) | (lo << 8) | (xd(p[2]) << 4) | xd(p[3]) : -1;
            if (cp == 0x3C)      { *d++ = '<';  p += 4; continue; }
            if (cp == 0x3E)      { *d++ = '>';  p += 4; continue; }
            /* 其它 \u 原样保留 */
            *d++ = '\\'; *d++ = 'u';
            continue;
        }
        switch (*p) {
            case 'n':  *d++ = '\n'; break;
            case 't':  *d++ = '\t'; break;
            case 'r':  *d++ = '\r'; break;
            case '"':  *d++ = '"';  break;
            case '\\': *d++ = '\\'; break;
            default:   *d++ = '\\'; *d++ = *p; break;
        }
        p++;
    }
    *d = '\0';
    return out;
}

/* JSON 转义，返回 malloc 内存 */
static char* json_esc(const char *src) {
    if (!src) return NULL;
    size_t cap = strlen(src) + 128;
    char *buf = malloc(cap), *d = buf;
    if (!buf) return NULL;
    while (*src) {
        if ((size_t)(d - buf) + 3 > cap) {
            size_t off = (size_t)(d - buf);
            cap *= 2;
            char *t = realloc(buf, cap);
            if (!t) { free(buf); return NULL; }
            buf = t; d = buf + off;
        }
        switch (*src) {
            case '"': *d++ = '\\'; *d++ = '"';  break;
            case '\\':*d++ = '\\'; *d++ = '\\'; break;
            case '\n':*d++ = '\\'; *d++ = 'n';  break;
            case '\r':*d++ = '\\'; *d++ = 'r';  break;
            case '\t':*d++ = '\\'; *d++ = 't';  break;
            default:  *d++ = *src; break;
        }
        src++;
    }
    *d = '\0';
    return buf;
}

/* ========== 去掉 <think>...</think> 思考块 ========== */

static char* no_think(char *text) {
    if (!text) return NULL;
    char *s = text, *d = text;
    while (*s) {
        if (strncasecmp(s, "<think>", 7) == 0) {
            s += 7;
            char *e = strcasestr(s, "</think>");
            s = e ? e + 8 : s + strlen(s);
            continue;
        }
        *d++ = *s++;
    }
    *d = '\0';
    return text;
}

/* ========== 聊天历史（环形缓冲区） ========== */

#define HMAX 100        // 100轮对话

typedef struct {
    char *items[HMAX];   /* 预构建的 JSON 片段: {"role":"x","content":"y"} */
    int   n, head;
} History;

/* 追加一条消息到历史。content 会做 JSON 转义。 */
static void hist_add(History *h, const char *role, const char *msg) {
    char *safe = json_esc(msg);
    if (!safe) return;

    /* 拼 JSON 片段 */
    char frag[8192];
    snprintf(frag, sizeof(frag), "{\"role\":\"%s\",\"content\":\"%s\"}", role, safe);
    free(safe);

    if (h->n >= HMAX) { free(h->items[h->head]); h->head = (h->head + 1) % HMAX; h->n--; }
    h->items[(h->head + h->n) % HMAX] = strdup(frag);
    h->n++;
}

static void hist_clr(History *h) {
    int i;
    for (i = 0; i < HMAX; i++) { free(h->items[i]); h->items[i] = NULL; }
    h->n = h->head = 0;
}

/* ========== Ollama /api/chat ========== */

#define PORT   11434
#define TMO    120
#define MODEL  "deepseek-r1:7b"

/* 发送聊天请求，返回回答文本（已去掉 think 块），调用者 free。失败返回 NULL。 */
static char* chat(const char *host, int port, const char *model, History *h) {
    char body[32768];
    int i, pos = snprintf(body, sizeof(body), "{\"model\":\"%s\",\"messages\":[", model);

    for (i = 0; i < h->n; i++) {
        pos += snprintf(body + pos, sizeof(body) - (size_t)pos,
                        "%s%s", h->items[(h->head + i) % HMAX],
                        i < h->n - 1 ? "," : "");
    }
    pos += snprintf(body + pos, sizeof(body) - (size_t)pos, "],\"stream\":false}");

    HttpResponse *r = http_post(host, port, "/api/chat", body, TMO);
    if (!r) return NULL;

    if (r->status_code != 200) {
        fprintf(stderr, "[%s] %d\n", r->status_code < 0 ? r->errmsg : "HTTP",
                r->status_code);
        http_response_free(r);
        return NULL;
    }

    char *text = json_val(r->body, "content");
    http_response_free(r);
    return no_think(text);
}

/* ========== 交互 ========== */

static void banner(const char *host, int port, const char *model) {
    printf("\n  === GEC6818 Ollama 客户端 (带记忆) ===\n"
           "  服务器 %s:%d  模型 %s\n"
           "  /quit  /clear  /model <名称>  /help\n\n", host, port, model);
}

static void loop(const char *host, int port, const char *model) {
    char line[4096], *mdl = strdup(model);
    if (!mdl) return;
    History h = {0};
    banner(host, port, mdl);

    while (printf("\033[32mYou> \033[0m"), fflush(stdout),
           fgets(line, sizeof(line), stdin))
    {
        size_t len = strlen(line);
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (!len) continue;

        if (line[0] == '/') {
            if (!strcmp(line, "/quit") || !strcmp(line, "/q")) break;
            if (!strcmp(line, "/clear")) { hist_clr(&h); printf("[已清除]\n"); continue; }
            if (!strncmp(line, "/model ", 7)) {
                free(mdl); mdl = strdup(line + 7); hist_clr(&h);
                printf("[切换: %s]\n", mdl); continue;
            }
            if (!strcmp(line, "/help")) { banner(host, port, mdl); continue; }
            printf("[?] %s\n", line); continue;
        }

        hist_add(&h, "user", line);
        printf("\033[33mAI> \033[0m"); fflush(stdout);

        char *re = chat(host, port, mdl, &h);
        printf("%s\n", re ? re : "(错误)");
        if (re) { hist_add(&h, "assistant", re); free(re); }
        putchar('\n');
    }
    printf("再见!\n");
    hist_clr(&h); free(mdl);
}

/* ========== main ========== */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <IP> [端口] [\"问题\"]\n", argv[0]);
        return 1;
    }

    int port = PORT, ai = 2;
    if (argc > 2) {
        char *e; long p = strtol(argv[2], &e, 10);
        if (!*e && p > 0 && p < 65536) { port = (int)p; ai = 3; }
    }

    /* 单次模式 */
    if (ai < argc) {
        size_t n = 0; int i;
        for (i = ai; i < argc; i++) n += strlen(argv[i]) + 1;
        char *prompt = malloc(n + 1);
        if (!prompt) return 1;
        prompt[0] = 0;
        for (i = ai; i < argc; i++) {
            if (i > ai) strcat(prompt, " ");
            strcat(prompt, argv[i]);
        }

        History h = {0};
        hist_add(&h, "user", prompt);
        char *re = chat(argv[1], port, MODEL, &h);
        printf("%s\n", re ? re : "(错误)");
        free(re); hist_clr(&h); free(prompt);
        return re ? 0 : 1;
    }

    loop(argv[1], port, MODEL);
    return 0;
}
