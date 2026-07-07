/*
 * ai_tools.c - 工具注册表 + 硬件实现
 *
 * 替换原 ai_chat_page.c 里的 ai_execute_action (关键词匹配)。
 * 现在 Qwen2.5:7b 输出 tool_calls JSON, agent 按 name 派发到这里。
 *
 * 新增硬件步骤:
 *   1. 实现 handler 函数 (签名: char* foo(const cJSON* args))
 *   2. 在 g_tools[] 加一行: {name, desc, schema, handler}
 */
#include "ai_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 硬件控制实现
 *
 * 双版本: PC 测试用 printf mock, 板子上用真实 sysfs/寄存器
 * 切换方式: 编译时加 -D__PC_TEST__ 走 mock, 否则走真实硬件
 * ============================================================ */

static int hw_led_set(int led_id, int on)
{
#ifdef __PC_TEST__
    printf("  [MOCK hw_led_set] LED %d -> %s\n", led_id, on ? "ON" : "OFF");
    return 0;
#else
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/leds/led%d/brightness", led_id);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d", on ? 1 : 0);
    fclose(f);
    return 0;
#endif
}

static int hw_buzzer_set(int freq, int duration_ms)
{
#ifdef __PC_TEST__
    printf("  [MOCK hw_buzzer_set] freq=%dHz duration=%dms\n", freq, duration_ms);
    return 0;
#else
    /* TODO: 替换成 GEC6818 蜂鸣器实际控制方式
     * 粤嵌板子常用: PWM 寄存器 mmap, 或 /sys/class/pwm/ */
    (void)freq; (void)duration_ms;
    return 0;
#endif
}

static char* hw_read_sensor_temp(void)
{
#ifdef __PC_TEST__
    return strdup("{\"temp\":26.5,\"unit\":\"C\",\"source\":\"mock\"}");
#else
    /* TODO: 接 IR0038B / DS18B20 / 板载温度传感器 */
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"temp\":26.5,\"unit\":\"C\"}");
    return strdup(buf);
#endif
}

static char* hw_read_accel(void)
{
#ifdef __PC_TEST__
    return strdup("{\"x\":0.12,\"y\":-0.05,\"z\":9.78,\"unit\":\"m/s^2\",\"source\":\"mock\"}");
#else
    /* TODO: 接 MMA8653FCR1 (I2C) */
    char buf[96];
    snprintf(buf, sizeof(buf),
        "{\"x\":0.12,\"y\":-0.05,\"z\":9.78,\"unit\":\"m/s^2\"}");
    return strdup(buf);
#endif
}

/* ============================================================
 * 工具 handler 函数 (供 g_tools[] 注册)
 * ============================================================ */

static char* tool_control_led(const cJSON *args)
{
    int led_id = cJSON_GetObjectItem(args, "led_id")->valueint;
    const char *state = cJSON_GetObjectItem(args, "state")->valuestring;
    int on = (strcmp(state, "on") == 0) ? 1 : 0;

    if (hw_led_set(led_id, on) != 0)
        return strdup("{\"ok\":false,\"msg\":\"无法打开 LED 设备\"}");

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"led\":%d,\"state\":\"%s\"}",
             led_id, on ? "on" : "off");
    return strdup(buf);
}

static char* tool_control_buzzer(const cJSON *args)
{
    int freq    = cJSON_GetObjectItem(args, "freq")->valueint;
    int dur     = cJSON_GetObjectItem(args, "duration_ms")->valueint;

    hw_buzzer_set(freq, dur);
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"freq\":%d,\"duration_ms\":%d}", freq, dur);
    return strdup(buf);
}

static char* tool_read_temperature(const cJSON *args)
{
    (void)args;
    return hw_read_sensor_temp();
}

static char* tool_read_acceleration(const cJSON *args)
{
    (void)args;
    return hw_read_accel();
}

/* ============================================================
 * 工具注册表 (新增硬件只改这一处)
 * ============================================================ */

static const ai_tool_t g_tools[] = {
    {
        "control_led", "控制 GEC6818 板载 LED 的开关",
        "{\"type\":\"object\",\"properties\":{"
        "\"led_id\":{\"type\":\"integer\",\"description\":\"LED 编号\"},"
        "\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\"]}"
        "},\"required\":[\"led_id\",\"state\"]}",
        tool_control_led
    },
    {
        "control_buzzer", "控制板载蜂鸣器",
        "{\"type\":\"object\",\"properties\":{"
        "\"freq\":{\"type\":\"integer\",\"description\":\"频率 Hz\"},"
        "\"duration_ms\":{\"type\":\"integer\",\"description\":\"持续毫秒\"}"
        "},\"required\":[\"freq\",\"duration_ms\"]}",
        tool_control_buzzer
    },
    {
        "read_temperature", "读取板载温度传感器",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_read_temperature
    },
    {
        "read_acceleration", "读取加速度计 (MMA8653FCR1)",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_read_acceleration
    },
};
#define TOOL_COUNT (sizeof(g_tools) / sizeof(g_tools[0]))

/* ============================================================
 * 公开接口实现
 * ============================================================ */

const ai_tool_t* ai_tools_get_all(int *count)
{
    if (count) *count = (int)TOOL_COUNT;
    return g_tools;
}

char* ai_tools_build_schema_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < (int)TOOL_COUNT; i++) {
        cJSON *t  = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name",        g_tools[i].name);
        cJSON_AddStringToObject(fn, "description", g_tools[i].description);
        cJSON_AddItemToObject(fn, "parameters",
            cJSON_Parse(g_tools[i].parameters_json));  /* 已校验过, 不会失败 */
        cJSON_AddItemToObject(t, "function", fn);
        cJSON_AddItemToArray(arr, t);
    }
    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}

char* ai_tools_dispatch(const char *name, const cJSON *args)
{
    for (int i = 0; i < (int)TOOL_COUNT; i++) {
        if (strcmp(name, g_tools[i].name) == 0) {
            return g_tools[i].handler(args);
        }
    }
    return NULL;  /* 未注册的工具 */
}
