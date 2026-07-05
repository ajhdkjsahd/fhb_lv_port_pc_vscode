/*
 * mqtt_pub_demo.c - GEC6818 MQTT 发布程序
 * 模拟采集传感器数据，每 3 秒向 broker 发布一条 JSON 消息
 *
 * 编译：./build_app.sh
 * 运行：./mqtt_pub_demo
 *
 * 配合 mqtt_sub 使用：板子跑 pub，PC/另一端跑 sub 即可看到数据
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "MQTTClient.h"

/* ===== 配置区（按需修改）===== */
#define BROKER_ADDRESS "mqtt://broker.emqx.io:1883"
#define CLIENT_ID       "gec6818_pub_001"
#define TOPIC           "gec6818/test/data"
#define QOS             1
#define INTERVAL        3   /* 发布间隔(秒) */
/* ============================== */

int main(int argc, char *argv[])
{
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    /* 1. 创建客户端 */
    if ((rc = MQTTClient_create(&client, BROKER_ADDRESS, CLIENT_ID,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("[错误] 创建客户端失败, rc=%d\n", rc);
        return -1;
    }

    /* 2. 连接参数 */
    conn_opts.keepAliveInterval = 60;
    conn_opts.cleansession     = 1;
    conn_opts.connectTimeout   = 10;

    printf("[信息] 正在连接 %s ...\n", BROKER_ADDRESS);
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("[错误] 连接失败, rc=%d\n", rc);
        MQTTClient_destroy(&client);
        return -1;
    }
    printf("[信息] 连接成功! 开始向 '%s' 发布数据\n", TOPIC);
    printf("------------------------------------------\n");

    /* 3. 循环发布（模拟温湿度采集）*/
    int seq = 0;
    char payload[256];
    unsigned int seed = (unsigned int)time(NULL);

    while (1) {
        /* 模拟传感器数据 */
        float temp = 25.0 + (rand_r(&seed) % 100) / 10.0;   /* 25.0 ~ 34.9 */
        float hum  = 50.0 + (rand_r(&seed) % 300) / 10.0;   /* 50.0 ~ 79.9 */

        snprintf(payload, sizeof(payload),
                 "{\"device\":\"gec6818\",\"seq\":%d,\"temp\":%.1f,\"hum\":%.1f}",
                 seq, temp, hum);

        pubmsg.payload    = payload;
        pubmsg.payloadlen = (int)strlen(payload);
        pubmsg.qos        = QOS;
        pubmsg.retained   = 0;

        rc = MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
        if (rc != MQTTCLIENT_SUCCESS) {
            printf("[错误] 发布失败, rc=%d\n", rc);
        } else {
            printf("[发布] seq=%d  %s\n", seq, payload);
            MQTTClient_waitForCompletion(client, token, 5000);
        }

        seq++;
        sleep(INTERVAL);
    }

    /* 4. 清理 */
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return 0;
}
