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
#define CLIENT_ID       "fhb_gec6818_pub_001"
#define TOPIC           "fhb/smart_aquaculture/sensor"
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

    /* 3. 循环发布（模拟 6 路传感器采集）*/
    int seq = 0;
    char payload[256];
    unsigned int seed = (unsigned int)time(NULL);

    while (1) {
        /* 模拟传感器数据（键名与 app_mqtt.c 的 s_keys[] 一致，全名）*/
        float temperature = 25.0f + (rand_r(&seed) % 100) / 10.0f;  /* 25.0 ~ 34.9 °C  */
        float humidity    = 50.0f + (rand_r(&seed) % 300) / 10.0f;  /* 50.0 ~ 79.9 %   */
        float light       = (float)(rand_r(&seed) % 101);           /* 0 ~ 100 %       */
        float do_val      = 4.0f + (rand_r(&seed) % 51) / 10.0f;    /* 4.0 ~ 9.0 mg/L  */
        float ph          = 6.5f + (rand_r(&seed) % 21) / 10.0f;    /* 6.5 ~ 8.5       */
        float nh3n        = (rand_r(&seed) % 101) / 100.0f;         /* 0.00 ~ 1.00 mg/L */

        snprintf(payload, sizeof(payload),
                 "{\"seq\":%d,\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%.0f,"
                 "\"do\":%.1f,\"ph\":%.1f,\"nh3n\":%.2f}",
                 seq, temperature, humidity, light, do_val, ph, nh3n);

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
