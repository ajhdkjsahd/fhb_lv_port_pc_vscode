/*
 * ai_agent.c - AI Agent 实现
 *
 * 数据流 (一次完整 tool calling 闭环):
 *   1. 接收 user 消息 → 拼历史 → POST /api/chat (带 tools schema)
 *   2. 解析 response
 *      a. 有 tool_calls → 遍历, ai_tools_dispatch 执行硬件控制
 *                       → 把工具结果作为 role=tool 追加历史
 *                       → 回到第 1 步再请求模型 (最多循环 3 次)
 *      b. 没有 tool_calls → content 即最终回复, 触发 AI_EVT_ANSWER
 *
 * 不依赖 LVGL: 所有 UI 通知走回调, UI 层 (adapter) 负责线程安全
 *
 * 依赖:
 *   - http_client.h (你已有的 HTTP 封装: http_post / HttpResponse)
 *   - cJSON (单文件库)
 *   - pthread
 */
#include "ai_agent.h"
#include "ai_tools.h"
#include "http_client.h"
#include "cJSON.h"        /* 单文件库 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIST_MAX        100
#define TOOL_LOOP_MAX   3
#define HTTP_TIMEOUT_S  300

/* Qwen2.5 / DeepSeek-R1 思考块边界 (拆开写避免被某些解析器误处理) */
static const char THINK_OPEN[]  = { '<', 't', 'h', 'i', 'n', 'k', '>', 0 };
static const char THINK_CLOSE[] = { '<', '/', 't', 'h', 'i', 'n', 'k', '>', 0 };

/* ── 内部状态 ── */
static char   g_host[64]   = "localhost";
static int     g_port       = 11434;
static char   g_model[32]  = "qwen2.5:7b";
static char  *g_system_msg = NULL;
static ai_event_cb_t g_cb  = NULL;
static void  *g_user_data  = NULL;

static pthread_t g_thread;
static bool  g_running = false;
static bool  g_stop    = false;

/* 对话历史 (环形缓冲) */
static char *g_hist[HIST_MAX];
static int   g_hist_n    = 0;
static int   g_hist_head = 0;

/* ── 工具: 触发回调 ── */
static void emit_event(const ai_event_t *evt)
{
    if (g_cb) g_cb(evt, g_user_data);
}

/* ── 工具: JSON 字符串转义 ── */
static char* json_escape(const char *s)
{
    if (!s) return strdup("");
    size_t cap = strlen(s) * 2 + 8;
    char *out = malloc(cap), *d = out;
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  *d++ = '\\'; *d++ = '"';  break;
            case '\\': *d++ = '\\'; *d++ = '\\'; break;
            case '\n': *d++ = '\\'; *d++ = 'n';  break;
            case '\r': *d++ = '\\'; *d++ = 'r';  break;
            case '\t': *d++ = '\\'; *d++ = 't';  break;
            default:   *d++ = *p;
        }
    }
    *d = '\0';
    return out;
}

/* ── 工具: 历史记录管理 ── */
static void hist_add(const char *role, const char *content)
{
    char *esc = json_escape(content);
    size_t cap = strlen(role) + strlen(esc) + 32;
    char *frag = malloc(cap);
    snprintf(frag, cap, "{\"role\":\"%s\",\"content\":\"%s\"}", role, esc);
    free(esc);

    if (g_hist_n >= HIST_MAX) {
        free(g_hist[g_hist_head]);
        g_hist_head = (g_hist_head + 1) % HIST_MAX;
        g_hist_n--;
    }
    g_hist[(g_hist_head + g_hist_n) % HIST_MAX] = frag;
    g_hist_n++;
}

static void hist_clear(void)
{
    for (int i = 0; i < HIST_MAX; i++) {
        free(g_hist[i]); g_hist[i] = NULL;
    }
    g_hist_n = g_hist_head = 0;
}

/* ── 工具: 构建请求 body (含 tools schema) ── */
static char* build_body(const char *user_msg, const char *tools_json)
{
    hist_add("user", user_msg);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", g_model);
    cJSON_AddBoolToObject(body, "stream", 0);

    cJSON *msgs = cJSON_CreateArray();
    if (g_system_msg) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", g_system_msg);
        cJSON_AddItemToArray(msgs, sys);
    }
    for (int i = 0; i < g_hist_n; i++) {
        cJSON *m = cJSON_Parse(g_hist[(g_hist_head + i) % HIST_MAX]);
        if (m) cJSON_AddItemToArray(msgs, m);
    }
    cJSON_AddItemToObject(body, "messages", msgs);

    if (tools_json) {
        cJSON_AddItemToObject(body, "tools", cJSON_Parse(tools_json));
    }

    char *s = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    return s;
}

/* ── 工具: HTTP POST ── */
static char* http_chat(const char *body)
{
    HttpResponse *r = http_post(g_host, g_port, "/api/chat", body, HTTP_TIMEOUT_S);
    if (!r || r->status_code != 200) {
        char err[256];
        snprintf(err, sizeof(err), "HTTP error: %s",
                 r ? (r->errmsg[0] ? r->errmsg : "unknown") : "network unreachable");
        if (r) http_response_free(r);
        return strdup(err);
    }
    char *resp = strdup(r->body ? r->body : "");
    http_response_free(r);
    return resp;
}

/* ── 工具: 处理 tool_calls (核心) ── */
static void handle_tool_calls(cJSON *tool_calls)
{
    cJSON *tc;
    cJSON_ArrayForEach(tc, tool_calls) {
        cJSON *fn   = cJSON_GetObjectItem(tc, "function");
        const char *name = cJSON_GetObjectItem(fn, "name")->valuestring;
        cJSON *args = cJSON_GetObjectItem(fn, "arguments");

        cJSON *args_obj = args;
        if (cJSON_IsString(args)) {
            args_obj = cJSON_Parse(args->valuestring);
        }

        char *result = ai_tools_dispatch(name, args_obj);

        char *args_str = cJSON_PrintUnformatted(args_obj);
        ai_event_t evt = {
            .type   = AI_EVT_ACTION,
            .tool   = name,
            .args   = args_str ? args_str : "",
            .result = result ? result : "(tool not impl)"
        };
        emit_event(&evt);
        free(args_str);

        if (result) {
            hist_add("tool", result);
        }

        if (args_obj != args) cJSON_Delete(args_obj);
        free(result);
    }
}

/* ── 工具: 从 content 中拆出思考块 ── */
static void emit_thinking_if_any(const char *content)
{
    const char *open = strstr(content, THINK_OPEN);
    if (!open) return;
    open += strlen(THINK_OPEN);
    const char *close = strstr(open, THINK_CLOSE);
    if (!close) return;

    size_t len = (size_t)(close - open);
    char *think = malloc(len + 1);
    memcpy(think, open, len);
    think[len] = '\0';

    ai_event_t te = {.type = AI_EVT_THINKING, .text = think};
    emit_event(&te);
    free(think);
}

/* ── Worker 线程 ── */
static void* worker_thread(void *arg)
{
    char *user_msg = (char *)arg;
    char *tools_json = ai_tools_build_schema_json();

    for (int loop = 0; loop < TOOL_LOOP_MAX; loop++) {
        if (g_stop) {
            ai_event_t e = {.type = AI_EVT_ERROR, .text = "stopped"};
            emit_event(&e);
            break;
        }

        char *body = build_body(user_msg, tools_json);
        free(user_msg);
        user_msg = NULL;

        char *resp = http_chat(body);
        free(body);

        if (g_stop) { free(resp); break; }

        cJSON *root = cJSON_Parse(resp);
        if (!root) {
            ai_event_t e = {.type = AI_EVT_ERROR, .text = "JSON parse failed"};
            emit_event(&e);
            free(resp);
            break;
        }

        cJSON *msg        = cJSON_GetObjectItem(root, "message");
        cJSON *content    = cJSON_GetObjectItem(msg, "content");
        cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");

        /* 1. 如果有 tool_calls → 执行工具 → 回到循环再请求模型 */
        if (tool_calls && cJSON_GetArraySize(tool_calls) > 0) {
            handle_tool_calls(tool_calls);
            cJSON_Delete(root);
            free(resp);
            continue;
        }

        /* 2. 没有 tool_calls → content 是最终回复 */
        if (content && content->valuestring && content->valuestring[0]) {
            emit_thinking_if_any(content->valuestring);
            ai_event_t ae = {.type = AI_EVT_ANSWER,
                            .text = content->valuestring};
            emit_event(&ae);
            hist_add("assistant", content->valuestring);
        } else {
            ai_event_t e = {.type = AI_EVT_ERROR, .text = "empty response"};
            emit_event(&e);
        }

        cJSON_Delete(root);
        free(resp);
        break;
    }

    free(tools_json);
    free(user_msg);

    ai_event_t done = {.type = AI_EVT_DONE};
    emit_event(&done);

    g_running = false;
    g_stop    = false;
    return NULL;
}

/* ============================================================
 * 公开 API
 * ============================================================ */

void ai_agent_init(const char *host, int port, const char *model)
{
    snprintf(g_host, sizeof(g_host), "%s", host);
    g_port = port;
    snprintf(g_model, sizeof(g_model), "%s", model);
}

void ai_agent_set_callback(ai_event_cb_t cb, void *user_data)
{
    g_cb = cb;
    g_user_data = user_data;
}

void ai_agent_set_system(const char *system_prompt)
{
    free(g_system_msg);
    g_system_msg = system_prompt ? strdup(system_prompt) : NULL;
}

bool ai_agent_send(const char *user_msg)
{
    if (g_running || !user_msg || !user_msg[0]) return false;
    g_running = true;
    g_stop    = false;

    char *msg_copy = strdup(user_msg);
    if (pthread_create(&g_thread, NULL, worker_thread, msg_copy) != 0) {
        free(msg_copy);
        g_running = false;
        return false;
    }
    pthread_detach(g_thread);
    return true;
}

void ai_agent_stop(void)
{
    if (g_running) g_stop = true;
}

void ai_agent_clear_history(void)
{
    hist_clear();
}

bool ai_agent_is_running(void)
{
    return g_running;
}
