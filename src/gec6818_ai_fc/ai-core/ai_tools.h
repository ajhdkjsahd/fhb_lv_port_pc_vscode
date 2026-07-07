/*
 * ai_tools.h - 工具注册表接口
 *
 * 设计原则:
 *   1. 数据驱动: 新增硬件只需在 ai_tools.c 的 g_tools[] 加一行
 *   2. 与 ai_agent 解耦: agent 只通过本接口拿工具 schema 和调用 handler
 *   3. 不依赖 LVGL, 可在 PC 上模拟 (handler 内 stub 返回假数据)
 */
#ifndef AI_TOOLS_H
#define AI_TOOLS_H

#include "cJSON.h"   /* 单文件库, 和本文件同目录 */

#ifdef __cplusplus
extern "C" {
#endif

/* 工具 handler 类型: 接收 args (cJSON 对象), 返回 malloc 的结果字符串 */
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
