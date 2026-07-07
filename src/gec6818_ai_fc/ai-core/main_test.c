/*
 * main_test.c - PC 测试入口 (不依赖 LVGL)
 *
 * 编译 (WSL2 Ubuntu):
 *   sudo apt install libcurl4-openssl-dev
 *   下载 cJSON.c / cJSON.h 到当前目录 (单文件库):
 *     wget https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
 *     wget https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h
 *
 *   gcc -D__PC_TEST__ -I. main_test.c ai_agent.c ai_tools.c \
 *       http_client.c cJSON.c -lpthread -o agent_test
 *
 * 运行前确认:
 *   1. Windows 上 Ollama 已启动, 并监听 0.0.0.0:
 *        set OLLAMA_HOST=0.0.0.0
 *        ollama serve
 *   2. 模型已拉: ollama pull qwen2.5:7b
 *   3. WSL2 能 ping 通 Windows host (cat /etc/resolv.conf | grep nameserver)
 *      拿到 IP 后改下面 OLLAMA_HOST
 *
 * 测试用例 (输入后回车):
 *   "帮我点亮 1 号 LED"
 *   "切换 2 号 LED"
 *   "读取温度传感器"
 *   "把所有 LED 关掉"
 *   "/clear"  清空对话历史
 *   "/quit"   退出
 */
#include "ai_agent.h"
#include "ai_tools.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* printf 事件回调 - 验证模型 tool_calls 是否正确触发 */
static void on_event(const ai_event_t *evt, void *user_data)
{
    (void)user_data;
    switch (evt->type) {
    case AI_EVT_THINKING:
        printf("\n[思考] %.400s%s\n", evt->text,
               strlen(evt->text) > 400 ? "..." : "");
        break;
    case AI_EVT_ACTION:
        printf("\n[工具调用] %s(%s)\n", evt->tool,
               evt->args ? evt->args : "");
        printf("  └─ 结果: %s\n", evt->result ? evt->result : "(空)");
        break;
    case AI_EVT_ANSWER:
        printf("\n[最终回答]\n%s\n", evt->text ? evt->text : "(空)");
        break;
    case AI_EVT_ERROR:
        printf("\n[错误] %s\n", evt->text ? evt->text : "未知");
        break;
    case AI_EVT_DONE:
        printf("\n─── 一次完整请求结束 ───\n");
        break;
    }
    fflush(stdout);
}

int main(void)
{
    /* WSL2 访问 Windows host 上的 Ollama, 改成你的 Windows IP */
    const char *host = "192.168.137.1";
    int         port = 11434;
    const char *model = "qwen2.5:7b";

    ai_agent_init(host, port, model);
    ai_agent_set_callback(on_event, NULL);
    ai_agent_set_system(
        "你是智慧水产养殖AI助手。具备以下能力:\n"
        "- 控制 LED 灯: control_led(led_id, state)\n"
        "- 切换蜂鸣器: toggle_buzzer()\n"
        "- 读取传感器: read_sensor(sensor_type)\n"
        "需要操作硬件时直接调用工具, 不要口头描述动作。"
    );

    printf("=== AI Agent PC 测试 ===\n");
    printf("Ollama: %s:%d  model: %s\n", host, port, model);
    printf("命令: /clear 清空历史  /quit 退出\n\n");

    char line[1024];
    while (1) {
        printf("用户> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (strcmp(line, "/quit") == 0) break;
        if (strcmp(line, "/clear") == 0) {
            ai_agent_clear_history();
            printf("(历史已清空)\n\n");
            continue;
        }

        if (!ai_agent_send(line)) {
            printf("(忙碌中, 等待...)\n");
            continue;
        }

        /* 等本次请求完成 (DONE 事件) */
        while (ai_agent_is_running()) {
            usleep(50000);
        }
    }

    printf("\n退出测试\n");
    return 0;
}
