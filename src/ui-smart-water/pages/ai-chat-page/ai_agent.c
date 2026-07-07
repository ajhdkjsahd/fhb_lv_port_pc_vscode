// ========== ai_agent.c ==========
// AI Agent 实现 — Qwen2.5:7b 原生 Function Calling
//
//   改编自 gec6818_ai_fc/ai-core/ai_agent.c, 适配本项目:
//     - 复用 ai-chat-page/http_client.c (select 版, ARM 板子更稳)
//     - cJSON 来自 libs/cjson (CMake 无条件编译)
//     - 平台分流: __linux__ 走真实 Ollama + tool_calls 闭环;
//                 否则 PC 模拟 (发 thinking+answer 事件供 UI 调试)
//
//   不直接碰 LVGL: 所有 UI 通知走事件回调, app_actions.c 里的
//   on_event() 负责线程安全 (lv_async_call)。
#include "ai_agent.h"
#include "ai_tools.h"
#include "http_client.h"
#include "cJSON.h"

#include "lvgl/lvgl.h"   /* LV_LOG_USER */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIST_MAX        100
#define TOOL_LOOP_MAX   3     /* 防止 tool_calls 死循环 */
#define HTTP_TIMEOUT_S  300   /* 5 min — 模型加载/长 prompt 可能慢 */

/* ── Ollama 服务器配置 ── */
#ifdef __linux__
#define OLLAMA_HOST  "192.168.137.1"   /* 板子访问 Windows 宿主 Ollama */
#else
#define OLLAMA_HOST  "localhost"        /* PC 本地 Ollama (仅模拟分支不连) */
#endif
#define OLLAMA_PORT  11434
#define OLLAMA_MODEL "qwen2.5:7b"

/* Qwen2.5 / DeepSeek-R1 思考块边界 (拆开写避免被某些解析器误处理) */
static const char THINK_OPEN[]  = { '<', 't', 'h', 'i', 'n', 'k', '>', 0 };
static const char THINK_CLOSE[] = { '<', '/', 't', 'h', 'i', 'n', 'k', '>', 0 };

/* ── 内部状态 ── */
static char   g_host[64]  = OLLAMA_HOST;
static int    g_port       = OLLAMA_PORT;
static char   g_model[32] = OLLAMA_MODEL;
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

/* ── 触发回调 ── */
static void emit_event(const ai_event_t *evt)
{
    if (g_cb) g_cb(evt, g_user_data);
}

/* ── JSON 字符串转义 ── */
static char* json_escape(const char *s)
{
    if (!s) return strdup("");
    size_t cap = strlen(s) * 2 + 8;
    char *out = malloc(cap), *d = out;
    if (!out) return NULL;
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

/* ── 历史记录管理 ── */

/* 追加到环形缓冲 (内部, 直接接管 frag 所有权) */
static void hist_append(char *frag)
{
    if (!frag) return;
    if (g_hist_n >= HIST_MAX) {
        free(g_hist[g_hist_head]);
        g_hist_head = (g_hist_head + 1) % HIST_MAX;
        g_hist_n--;
    }
    g_hist[(g_hist_head + g_hist_n) % HIST_MAX] = frag;
    g_hist_n++;
}

/* 添加普通 {"role":"...","content":"..."} 消息 */
static void hist_add(const char *role, const char *content)
{
    char *esc = json_escape(content);
    if (!esc) return;
    size_t cap = strlen(role) + strlen(esc) + 32;
    char *frag = malloc(cap);
    if (!frag) { free(esc); return; }
    snprintf(frag, cap, "{\"role\":\"%s\",\"content\":\"%s\"}", role, esc);
    free(esc);
    hist_append(frag);
}

/* 添加工具结果 {"role":"tool","name":"...","content":"..."} */
static void hist_add_tool(const char *tool_name, const char *result)
{
    char *esc = json_escape(result ? result : "");
    size_t cap = strlen(tool_name) + strlen(esc) + 64;
    char *frag = malloc(cap);
    if (!frag) { free(esc); return; }
    snprintf(frag, cap,
        "{\"role\":\"tool\",\"name\":\"%s\",\"content\":\"%s\"}",
        tool_name, esc);
    free(esc);
    hist_append(frag);
}

/* 添加原始 JSON 片段 (用于 assistant tool_calls 消息等) */
static void hist_add_raw(const char *json_frag)
{
    if (!json_frag) return;
    hist_append(strdup(json_frag));
}

static void hist_clear(void)
{
    for (int i = 0; i < HIST_MAX; i++) {
        free(g_hist[i]); g_hist[i] = NULL;
    }
    g_hist_n = g_hist_head = 0;
}

/* ── 构建请求 body (含 tools schema) ── */
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

#ifdef __linux__
/* ── HTTP POST /api/chat (仅 Linux/ARM, PC 走模拟分支) ── */
static char* http_chat(const char *body)
{
    LV_LOG_USER("AI: POST to %s:%d (%zu bytes)", g_host, g_port, strlen(body));

    HttpResponse *r = http_post(g_host, g_port, "/api/chat", body, HTTP_TIMEOUT_S);
    if (!r || r->status_code != 200) {
        char err[256];
        snprintf(err, sizeof(err), "HTTP error: %s",
                 r ? (r->errmsg[0] ? r->errmsg : "unknown") : "network unreachable");
        LV_LOG_USER("AI: %s", err);
        if (r) http_response_free(r);
        return strdup(err);
    }
    char *resp = strdup(r->body ? r->body : "");
    http_response_free(r);
    return resp;
}
#endif /* __linux__ */

/* ── 处理 tool_calls (核心) ──
 * @param msg  Ollama 返回的完整 assistant message 对象 (含 role+tool_calls)
 *             会被序列化存入对话历史, 保证后续轮次模型能回溯自己的工具调用 */
static void handle_tool_calls(cJSON *msg)
{
    cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");
    if (!tool_calls) return;

    /* 1. 先把完整的 assistant 消息存入历史 (含 tool_calls 数组),
     *    这样下一轮请求时模型能看到 "我上次调了哪些工具" */
    char *msg_json = cJSON_PrintUnformatted(msg);
    hist_add_raw(msg_json);
    free(msg_json);

    /* 2. 遍历每个 tool_call → 分发 → 存 tool 结果 */
    cJSON *tc;
    cJSON_ArrayForEach(tc, tool_calls) {
        cJSON *fn   = cJSON_GetObjectItem(tc, "function");
        if (!fn) continue;
        cJSON *name_item = cJSON_GetObjectItem(fn, "name");
        if (!name_item || !cJSON_IsString(name_item)) continue;
        const char *name = name_item->valuestring;
        cJSON *args = cJSON_GetObjectItem(fn, "arguments");

        /* arguments 可能是对象, 也可能是字符串 (需再 parse) */
        cJSON *args_obj = args;
        if (cJSON_IsString(args)) {
            args_obj = cJSON_Parse(args->valuestring);
        }

        LV_LOG_USER("AI: tool_call %s", name);

        char *result = ai_tools_dispatch(name, args_obj);

        char *args_str = args_obj ? cJSON_PrintUnformatted(args_obj) : NULL;
        ai_event_t evt = {
            .type   = AI_EVT_ACTION,
            .tool   = name,
            .args   = args_str ? args_str : "",
            .result = result ? result : "(tool not impl)"
        };
        emit_event(&evt);
        free(args_str);

        /* 工具结果带 name 字段存入历史, Ollama 据此关联 tool_call */
        if (result) {
            hist_add_tool(name, result);
        }

        if (args_obj != args) cJSON_Delete(args_obj);
        free(result);
    }
}

/* ── 从 content 中拆出 <think>...</think> 思考块 ── */
static void emit_thinking_if_any(const char *content)
{
    const char *open = strstr(content, THINK_OPEN);
    if (!open) return;
    open += strlen(THINK_OPEN);
    const char *close = strstr(open, THINK_CLOSE);
    if (!close) return;

    size_t len = (size_t)(close - open);
    char *think = malloc(len + 1);
    if (!think) return;
    memcpy(think, open, len);
    think[len] = '\0';

    ai_event_t te = { .type = AI_EVT_THINKING, .text = think };
    emit_event(&te);
    free(think);
}

/* ── Worker 线程 ── */
static void* worker_thread(void *arg)
{
    char *user_msg = (char *)arg;

#ifdef __linux__
    char *tools_json = ai_tools_build_schema_json();

    for (int loop = 0; loop < TOOL_LOOP_MAX; loop++) {
        if (g_stop) {
            ai_event_t e = { .type = AI_EVT_ERROR, .text = "stopped" };
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
            LV_LOG_USER("AI: JSON parse failed, resp=%.200s", resp);
            ai_event_t e = { .type = AI_EVT_ERROR, .text = "JSON parse failed" };
            emit_event(&e);
            free(resp);
            break;
        }

        cJSON *msg        = cJSON_GetObjectItem(root, "message");
        cJSON *content    = msg ? cJSON_GetObjectItem(msg, "content") : NULL;
        cJSON *tool_calls = msg ? cJSON_GetObjectItem(msg, "tool_calls") : NULL;

        /* 1. 有 tool_calls → 执行工具 → 回到循环再请求模型 */
        if (tool_calls && cJSON_GetArraySize(tool_calls) > 0) {
            LV_LOG_USER("AI: got %d tool_calls, dispatching (loop %d/%d)",
                        cJSON_GetArraySize(tool_calls), loop + 1, TOOL_LOOP_MAX);
            handle_tool_calls(msg);   /* 传入完整 assistant 消息以存历史 */
            cJSON_Delete(root);
            free(resp);
            continue;
        }

        /* 2. 没有 tool_calls → content 是最终回复 */
        if (content && cJSON_IsString(content) && content->valuestring[0]) {
            emit_thinking_if_any(content->valuestring);
            ai_event_t ae = { .type = AI_EVT_ANSWER,
                              .text = content->valuestring };
            emit_event(&ae);
            hist_add("assistant", content->valuestring);
        } else {
            ai_event_t e = { .type = AI_EVT_ERROR, .text = "empty response" };
            emit_event(&e);
        }

        cJSON_Delete(root);
        free(resp);
        break;
    }

    free(tools_json);

#else  /* !__linux__ — PC 模拟, 不连 Ollama */
    LV_LOG_USER("AI: PC stub simulated response");
    ai_event_t th = {
        .type = AI_EVT_THINKING,
        .text = "分析用户问题…\n检索知识库: 水产养殖手册\n"
                "匹配合适的回答模板…\n置信度: 0.94"
    };
    emit_event(&th);
    ai_event_t an = {
        .type = AI_EVT_ANSWER,
        .text = "这是模拟的 AI 回复。在 Linux/ARM 板卡上运行时会连接到真实的 "
                "Ollama(qwen2.5:7b) 服务器, 并通过 Function Calling 真正控制板载 "
                "LED/蜂鸣器/传感器。\n\n请确保:\n"
                "1. Ollama 服务已启动 (ollama serve)\n"
                "2. 已拉取模型 (ollama pull qwen2.5:7b)\n"
                "3. 板子能访问 Ollama 服务器"
    };
    emit_event(&an);
#endif

    free(user_msg);   /* linux 已置 NULL (no-op), PC 在此释放 */

    ai_event_t done = { .type = AI_EVT_DONE };
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
    /* NULL / 0 / "" 表示沿用编译期内置默认 (板子 192.168.137.1, PC localhost) */
    if (host && host[0])  snprintf(g_host,  sizeof(g_host),  "%s", host);
    if (port > 0)         g_port = port;
    if (model && model[0]) snprintf(g_model, sizeof(g_model), "%s", model);
    LV_LOG_USER("AI: agent ready (host=%s, port=%d, model=%s)",
                g_host, g_port, g_model);
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
    if (!msg_copy) {
        g_running = false;
        return false;
    }
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
    LV_LOG_USER("AI: history cleared");
}

bool ai_agent_is_running(void)
{
    return g_running;
}
