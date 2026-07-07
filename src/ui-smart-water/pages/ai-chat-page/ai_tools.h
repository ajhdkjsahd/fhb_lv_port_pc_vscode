// ========== ai_tools.h ==========
// 工具注册表接口 — Qwen2.5:7b Function Calling 的"手"
//
//   设计原则:
//     1. 数据驱动: 新增硬件只需在 ai_tools.c 的 g_tools[] 加一行
//     2. 与 ai_agent 解耦: agent 只通过本接口拿工具 schema 和调用 handler
//     3. handler 全部转调 ai_hardware.c (板卡实测) + edge_engine (水质传感器)
//
//   工具清单 (模型可见):
//     control_led(led_id, state)        控制 1~4 号 LED
//     control_all_leds(state)           全部 LED 开/关
//     random_led_on()                   随机点亮一盏灭的 LED
//     control_buzzer(state)             蜂鸣器开/关
//     toggle_buzzer()                   蜂鸣器反转
//     read_button()                     读 K2 按键状态
//     read_acceleration()               读 MMA8653 三轴加速度
//     read_water_sensor(sensor)         读 6 路水质传感器之一
#ifndef AI_TOOLS_H
#define AI_TOOLS_H

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 工具 handler 类型: 接收 args (cJSON 对象), 返回 malloc 的结果字符串 (JSON) */
typedef char* (*ai_tool_handler_t)(const cJSON *args);

typedef struct {
    const char            *name;             /* 工具名 (模型可见) */
    const char            *description;      /* 描述 (模型可见) */
    const char            *parameters_json;  /* JSON Schema 字符串 */
    ai_tool_handler_t      handler;
} ai_tool_t;

/* 工具注册表入口 (返回数组首地址, count 写入 *count) */
const ai_tool_t* ai_tools_get_all(int *count);

/* 构造 Ollama tools 字段 JSON 字符串 (malloc'd, 调用方释放) */
char* ai_tools_build_schema_json(void);

/* 按 name 查找并调用 handler, 返回结果字符串 (malloc'd, 调用方释放)
 * 找不到工具返回 NULL */
char* ai_tools_dispatch(const char *name, const cJSON *args);

#ifdef __cplusplus
}
#endif
#endif /* AI_TOOLS_H */
