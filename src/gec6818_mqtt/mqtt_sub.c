/*
 * mqtt_sub.c - GEC6818 MQTT 订阅程序
 * 连接公共 broker，订阅主题并打印收到的消息
 *
 * 编译：./build_app.sh
 * 运行：./mqtt_sub
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"

/* ===== 配置区（按需修改）===== */
#define BROKER_ADDRESS "mqtt://broker.emqx.io:1883"
#define CLIENT_ID       "gec6818_sub_001"
#define TOPIC           "gec6818/test/data"
#define QOS             1
/* ============================== */

/* 收到消息时回调 */
static int msg_arrived(void *context, char *topicName, int topicLen,
                       MQTTClient_message *message)
{
    printf("[收到] 主题:%s  长度:%d  内容:", topicName, message->payloadlen);
    fflush(stdout);
    fwrite(message->payload, 1, message->payloadlen, stdout);
    printf("\n");
    fflush(stdout);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;  /* 1 表示已处理 */
}

/* 连接断开时回调 */
static void conn_lost(void *context, char *cause)
{
    printf("[告警] 连接丢失, 原因: %s\n", cause ? cause : "未知");
}

int main(int argc, char *argv[])
{
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    /* 1. 创建客户端 */
    if ((rc = MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("[错误] 创建客户端失败, rc=%d\n", rc);
        return -1;
    }

    /* 2. 设置回调（消息到达、连接丢失）*/
    if ((rc = MQTTClient_setCallbacks(client, NULL, conn_lost, msg_arrived, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("[错误] 设置回调失败, rc=%d\n", rc);
        MQTTClient_destroy(&client);
        return -1;
    }

    /* 3. 连接参数 */
    conn_opts.keepAliveInterval = 60;   /* 心跳 60s */
    conn_opts.cleansession     = 1;
    conn_opts.connectTimeout   = 10;   /* 连接超时 10s */

    /* 4. 连接 broker */
    printf("[信息] 正在连接 %s ...\n", BROKER_ADDRESS);
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("[错误] 连接失败, rc=%d\n", rc);
        MQTTClient_destroy(&client);
        return -1;
    }
    printf("[信息] 连接成功!\n");

    /* 5. 订阅主题 */
    printf("[信息] 订阅主题: %s\n", TOPIC);
    if ((rc = MQTTClient_subscribe(client, TOPIC, QOS)) != MQTTCLIENT_SUCCESS) {
        printf("[错误] 订阅失败, rc=%d\n", rc);
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);
        return -1;
    }
    printf("[信息] 订阅成功, 等待消息...\n");
    printf("[提示] 用 PC 端 MQTT 客户端向 '%s' 发布消息即可看到\n", TOPIC);
    printf("------------------------------------------\n");

    /* 6. 主循环（回调在内部线程触发，这里只需保持进程存活）*/
    while (1) {
        sleep(1);
    }

    /* 7. 清理（正常不会走到这里）*/
    MQTTClient_unsubscribe(client, TOPIC);
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return 0;
}
