/*
 * ai_ui_adapter.c - LVGL 适配层 (示例)
 *
 * 职责: 把 ai_agent 的事件回调桥接到 LVGL, 保证 UI 更新在主线程
 *
 * 替换原 ai_chat_page.c 里那一大坨 lv_async_call + ai_ui_packet_t 逻辑
 *
 * 接入方式 (在你的 app_main 里):
 *
 *   ai_ui_adapter_init(ai_chat_page_get_screen());
 *   ai_agent_init("192.168.137.1", 11434, "qwen2.5:7b");
 *   ai_agent_set_callback(ai_ui_adapter_on_event, NULL);
 *   ai_agent_set_system("你是智慧水产养殖AI助手");
 *
 *   // UI 发送按钮回调里调:
 *   ai_agent_send(text);
 */
#include "ai_agent.h"
#include "ai_chat_page.h"   /* 你已有的 UI API: show_error / append_thinking / ... */
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

/* LVGL 的 lv_async_call 只能传一个 void*, 多字段得打包 */
typedef struct {
    ai_event_type_t type;
    char *text;
    char *tool;
    char *args;
    char *result;
    lv_obj_t *screen;
} ui_msg_t;

static lv_obj_t *g_screen = NULL;

/* 在 LVGL 主线程执行 (由 lv_async_call 调度) */
static void ui_dispatch(void *data)
{
    ui_msg_t *m = (ui_msg_t *)data;
    if (!m->screen) goto cleanup;

    switch (m->type) {
    case AI_EVT_THINKING:
        ai_chat_page_append_thinking(m->screen, m->text ? m->text : "");
        break;
    case AI_EVT_ACTION:
        /* UI 显示 "执行了 control_led, 结果: {...}" */
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "[执行 %s] %s",
                     m->tool ? m->tool : "?",
                     m->result ? m->result : "");
            ai_chat_page_append_thinking(m->screen, buf);
        }
        break;
    case AI_EVT_ANSWER:
        ai_chat_page_finish_thinking(m->screen);
        ai_chat_page_append_answer(m->screen, m->text ? m->text : "");
        ai_chat_page_finish_response(m->screen);
        break;
    case AI_EVT_ERROR:
        ai_chat_page_show_error(m->screen, m->text ? m->text : "未知错误");
        break;
    case AI_EVT_DONE:
        /* 可选: 关闭 loading 动画 */
        break;
    }

cleanup:
    free(m->text);
    free(m->tool);
    free(m->args);
    free(m->result);
    free(m);
}

/* ai_agent 回调 (worker 线程触发, 不能直接碰 LVGL) */
static void on_event(const ai_event_t *evt, void *user_data)
{
    (void)user_data;
    if (!g_screen) return;

    ui_msg_t *m = malloc(sizeof(*m));
    if (!m) return;
    m->type   = evt->type;
    m->text   = evt->text   ? strdup(evt->text)   : NULL;
    m->tool   = evt->tool   ? strdup(evt->tool)   : NULL;
    m->args   = evt->args   ? strdup(evt->args)   : NULL;
    m->result = evt->result ? strdup(evt->result) : NULL;
    m->screen = g_screen;

    /* 关键: lv_async_call 保证在 LVGL 主线程执行 */
    lv_async_call(ui_dispatch, m);
}

/* 公开: 初始化适配器 */
void ai_ui_adapter_init(lv_obj_t *screen)
{
    g_screen = screen;
}

/* 公开: 给 ai_agent_set_callback 用的回调函数 */
ai_event_cb_t ai_ui_adapter_get_cb(void)
{
    return on_event;
}
