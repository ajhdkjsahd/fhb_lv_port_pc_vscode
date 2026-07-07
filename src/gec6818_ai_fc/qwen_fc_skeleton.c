/*
 * qwen_fc_skeleton.c
 * GEC6818 + LVGL + Qwen2.5:7b Function Calling 最小骨架
 *
 * 依赖: libcurl, cJSON (单文件库, 直接丢项目里)
 * 交叉编译示例:
 *   arm-linux-gcc qwen_fc_skeleton.c -lcurl -lcjson -lpthread -o agent
 *
 * 数据流:
 *   LVGL输入 -> C客户端 -> Ollama -> tool_calls -> 硬件控制 -> 回传模型
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define OLLAMA_URL "http://localhost:11434/api/chat"
#define MODEL_NAME "qwen2.5:7b"

/* === 工具函数类型 === */
typedef char* (*tool_handler_t)(const cJSON *args);

typedef struct {
    const char *name;
    const char *description;
    const char *parameters_json;   /* JSON schema 字符串 */
    tool_handler_t handler;
} tool_t;

/* === 硬件控制实现 === */
/* TODO: 替换成你原理图里实际的 GPIO 引脚 / sysfs 路径 */
static char* tool_control_led(const cJSON *args) {
    int led_id = cJSON_GetObjectItem(args, "led_id")->valueint;
    const char *state = cJSON_GetObjectItem(args, "state")->valuestring;
    int on = (strcmp(state, "on") == 0) ? 1 : 0;

    /* 粤嵌 GEC6818 通常走 /sys/class/leds/ 或直接 mmap 寄存器 */
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/leds/led%d/brightness", led_id);
    FILE *f = fopen(path, "w");
    if (!f) return strdup("LED 控制失败: 无法打开设备");
    fprintf(f, "%d", on);
    fclose(f);

    char buf[128];
    snprintf(buf, sizeof(buf), "LED %d 已%s", led_id, on ? "点亮" : "熄灭");
    return strdup(buf);
}

/* 工具2: 读取传感器 (示例占位, 替换成 MMA8653FCR1 / IR0038B 等真实驱动) */
static char* tool_read_sensor(const cJSON *args) {
    return strdup("{\"temp\": 26.5, \"unit\": \"C\"}");
}

/* === 工具注册表 (新增硬件只需加一行) === */
static tool_t g_tools[] = {
    {"control_led", "控制 GEC6818 板载 LED 的开关",
     "{\"type\":\"object\",\"properties\":{"
     "\"led_id\":{\"type\":\"integer\",\"description\":\"LED 编号\"},"
     "\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\"]}"
     "},\"required\":[\"led_id\",\"state\"]}",
     tool_control_led},

    {"read_sensor", "读取板载传感器数据",
     "{\"type\":\"object\",\"properties\":{}}",
     tool_read_sensor},
};
#define TOOL_COUNT (sizeof(g_tools)/sizeof(g_tools[0]))

/* 构造 Ollama tools 字段 JSON 数组 */
static char* build_tools_json(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < TOOL_COUNT; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", g_tools[i].name);
        cJSON_AddStringToObject(fn, "description", g_tools[i].description);
        cJSON_AddItemToObject(fn, "parameters", cJSON_Parse(g_tools[i].parameters_json));
        cJSON_AddItemToObject(t, "function", fn);
        cJSON_AddItemToArray(arr, t);
    }
    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}

/* === libcurl HTTP 回调 === */
struct resp_buf { char *data; size_t size; };
static size_t write_cb(void *p, size_t s, size_t n, void *ud) {
    struct resp_buf *b = ud;
    b->data = realloc(b->data, b->size + n + 1);
    memcpy(b->data + b->size, p, n);
    b->size += n;
    b->data[b->size] = 0;
    return n * s;
}

/* 调用 Ollama /api/chat */
static char* ollama_chat(const char *user_msg, const char *tools_json) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", MODEL_NAME);
    cJSON_AddBoolToObject(body, "stream", 0);

    cJSON *msgs = cJSON_CreateArray();
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "user");
    cJSON_AddStringToObject(m, "content", user_msg);
    cJSON_AddItemToArray(msgs, m);
    cJSON_AddItemToObject(body, "messages", msgs);

    cJSON_AddItemToObject(body, "tools", cJSON_Parse(tools_json));

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    CURL *curl = curl_easy_init();
    struct resp_buf buf = {0};
    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, OLLAMA_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(hdrs);
    free(body_str);

    return (rc == CURLE_OK) ? buf.data : NULL;
}

/* === 解析 tool_calls 并分发 === */
static void dispatch_tool_calls(const cJSON *tool_calls) {
    cJSON *tc;
    cJSON_ArrayForEach(tc, tool_calls) {
        cJSON *fn = cJSON_GetObjectItem(tc, "function");
        const char *name = cJSON_GetObjectItem(fn, "name")->valuestring;
        cJSON *args = cJSON_GetObjectItem(fn, "arguments");

        int found = 0;
        for (int i = 0; i < TOOL_COUNT; i++) {
            if (strcmp(name, g_tools[i].name) == 0) {
                char *result = g_tools[i].handler(args);
                printf("[Tool %s] -> %s\n", name, result);
                free(result);
                found = 1;
                break;
            }
        }
        if (!found) printf("[未实现的工具] %s\n", name);
    }
}

/* === 主流程 === */
int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    char *tools_json = build_tools_json();
    const char *user_msg = "帮我点亮 2 号 LED";

    char *resp = ollama_chat(user_msg, tools_json);
    free(tools_json);

    if (!resp) {
        printf("Ollama 调用失败\n");
        return 1;
    }

    cJSON *root = cJSON_Parse(resp);
    cJSON *msg = cJSON_GetObjectItem(root, "message");
    cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");

    if (tool_calls && cJSON_GetArraySize(tool_calls) > 0) {
        printf("模型请求调用工具:\n");
        dispatch_tool_calls(tool_calls);
        /* TODO: 把工具结果作为 role=tool 消息追加, 再次调用模型生成最终回复 */
    } else {
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        printf("模型回复: %s\n", content ? content->valuestring : "(空)");
    }

    cJSON_Delete(root);
    free(resp);
    curl_global_cleanup();
    return 0;
}
