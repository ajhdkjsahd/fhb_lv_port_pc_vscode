// ========== ai_agent.h ==========
// AI Agent 对外接口 — Qwen2.5:7b 原生 Function Calling
//
//   设计原则:
//     1. 通过事件回调通知 UI, UI 层负责线程安全 (lv_async_call)
//     2. 不关心硬件实现, 通过 ai_tools 接口分发 tool_calls
//     3. tool_calls 结果作为 role=tool 回传模型, 形成闭环 (最多循环 3 次)
//
//   数据流 (一次完整 tool calling 闭环):
//     1. 接收 user 消息 → 拼历史 → POST /api/chat (带 tools schema)
//     2. 解析 response
//        a. 有 tool_calls → ai_tools_dispatch 执行硬件控制 → 结果作为
//           role=tool 追加历史 → 回到第 1 步再请求模型 (最多 3 次)
//        b. 没有 tool_calls → content 即最终回复, 触发 AI_EVT_ANSWER
//
//   用法:
//     ai_agent_init("192.168.137.1", 11434, "qwen2.5:7b");
//     ai_agent_set_callback(on_event, NULL);
//     ai_agent_set_system("你是智慧水产养殖AI助手");
//     ai_agent_send("帮我点亮2号LED");
#ifndef AI_AGENT_H
#define AI_AGENT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 事件类型 ── */
typedef enum {
    AI_EVT_THINKING,    /* 模型思考内容 (可空) */
    AI_EVT_ACTION,      /* 工具调用结果 */
    AI_EVT_ANSWER,      /* 模型最终回复 */
    AI_EVT_ERROR,       /* 出错 */
    AI_EVT_DONE         /* 本次请求完成 */
} ai_event_type_t;

/* ── 事件结构 (回调中传入, 字段有效期仅在回调期间, UI 层需自行 strdup) ── */
typedef struct {
    ai_event_type_t type;
    const char *text;     /* thinking/answer/error 的内容 */
    const char *tool;     /* AI_EVT_ACTION 时: 工具名 */
    const char *args;     /* AI_EVT_ACTION 时: 工具参数 JSON */
    const char *result;   /* AI_EVT_ACTION 时: 工具返回值 */
} ai_event_t;

/* 事件回调 (在 worker 线程触发, UI 层用 lv_async_call 桥接) */
typedef void (*ai_event_cb_t)(const ai_event_t *evt, void *user_data);

/* ── 公开 API ── */
void  ai_agent_init(const char *host, int port, const char *model);
void  ai_agent_set_callback(ai_event_cb_t cb, void *user_data);
void  ai_agent_set_system(const char *system_prompt);

bool  ai_agent_send(const char *user_msg);   /* 异步, 内部起线程 */
void  ai_agent_stop(void);                   /* 中断当前请求 */
void  ai_agent_clear_history(void);          /* 清空对话历史 */

bool  ai_agent_is_running(void);

#ifdef __cplusplus
}
#endif
#endif /* AI_AGENT_H */
