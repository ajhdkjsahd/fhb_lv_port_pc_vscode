// ========== app_mqtt.c ==========
// GEC6818 MQTT 传感器数据订阅模块。
// 依赖 Eclipse Paho MQTT C 库。仅在 APP_USE_MQTT 宏定义时编译。
// 收到 JSON 消息后解析各路传感器数值，调用 app_action_sensor_set() 存入全局存储，
// 由 sensor_page 定时器通过 app_action_sensor_read() 取用。
#ifdef APP_USE_MQTT

#include "app_mqtt.h"
#include "app_actions.h"
#include "../edge/edge_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <MQTTClient.h>
#include "cJSON.h"

/*********************
 *      DEFINES
 *********************/
#ifndef MQTT_BROKER_ADDR
#define MQTT_BROKER_ADDR  "mqtt://broker.emqx.io:1883"
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID    "fhb_gec6818_lvgl_001"
#endif
#ifndef MQTT_TOPIC
#define MQTT_TOPIC        "fhb/smart_aquaculture/sensor"
#endif
#define MQTT_QOS           1
#define MQTT_TIMEOUT       10000    /* 连接/断开超时 ms */

/**********************
 *  STATIC VARIABLES
 **********************/
static MQTTClient    g_client    = NULL;
static bool          g_connected = false;

/* 传感器键名（与发布端 JSON 字段、sensor_page.c 的 g_sensors[] 顺序一致） */
static const char * s_keys[SENSOR_IDX_COUNT] = {
    "temperature", "humidity", "light", "do", "ph", "nh3n"
};

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int  mqtt_msg_arrived(void * ctx, char * topic, int topic_len,
                              MQTTClient_message * msg);
static void mqtt_conn_lost(void * ctx, char * cause);
static bool mqtt_do_connect(void);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
bool app_mqtt_init(void)
{
    return mqtt_do_connect();
}

void app_mqtt_deinit(void)
{
    if(g_client) {
        if(g_connected) {
            MQTTClient_unsubscribe(g_client, MQTT_TOPIC);
            MQTTClient_disconnect(g_client, MQTT_TIMEOUT);
            g_connected = false;
        }
        MQTTClient_destroy(&g_client);
        g_client = NULL;
    }
}

void app_mqtt_ensure_connected(void)
{
    if(g_connected) return;
    static int attempt = 0;
    attempt++;
    printf("[MQTT] [%d] 断线重连中...\n", attempt);
    if(mqtt_do_connect()) {
        printf("[MQTT] [%d] 重连成功!\n", attempt);
        attempt = 0;
    }
}

void app_mqtt_yield(void)
{
    /* 驱动 Paho 内部接收循环, 把网络缓冲区里的 MQTT 消息分发到
     * mqtt_msg_arrived 回调。ARM 交叉编译环境下 Paho 的异步接收线程
     * 常因 pthread 配置不完整而无法工作, 必须由外部定时器驱动。
     * PC 上异步线程正常时此调用快速返回 (约 10ms 超时)。 */
    if(g_client) {
#ifdef __linux__
        MQTTClient_yield();                    /* ARM 旧版 Paho (v1.2-): 无参 */
#else
        MQTTClient_yield(g_client, 10);        /* PC 新版 Paho (v1.3+): (client, timeout) */
#endif
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static bool mqtt_do_connect(void)
{
    int rc;

    if(g_client) {
        MQTTClient_destroy(&g_client);
        g_client = NULL;
    }

    /* 1. 创建客户端 */
    rc = MQTTClient_create(&g_client, MQTT_BROKER_ADDR, MQTT_CLIENT_ID,
                            MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if(rc != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] 创建客户端失败, rc=%d\n", rc);
        g_client = NULL;
        return false;
    }

    /* 2. 设置回调 */
    rc = MQTTClient_setCallbacks(g_client, NULL,
                                  mqtt_conn_lost, mqtt_msg_arrived, NULL);
    if(rc != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] 设置回调失败, rc=%d\n", rc);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
        return false;
    }

    /* 3. 连接 broker (keepAliveInterval=60 → 60s 无心跳则服务端断开,
     *     Paho 内部线程检测到断开后回调 mqtt_conn_lost) */
    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = 60;
    opts.cleansession      = 1;
    opts.connectTimeout    = 10;

    printf("[MQTT] 连接 %s (topic: %s) ...\n", MQTT_BROKER_ADDR, MQTT_TOPIC);
    rc = MQTTClient_connect(g_client, &opts);
    if(rc != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] 连接失败, rc=%d (0=SUCCESS, 1=FAILURE, 2=DISCONNECTED, 3=MAX_INFLIGHT)\n", rc);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
        return false;
    }

    /* 4. 订阅主题 */
    rc = MQTTClient_subscribe(g_client, MQTT_TOPIC, MQTT_QOS);
    if(rc != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] 订阅失败, rc=%d\n", rc);
        MQTTClient_disconnect(g_client, MQTT_TIMEOUT);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
        return false;
    }

    g_connected = true;
    printf("[MQTT] ✓ 已连接 %s · 已订阅 %s\n", MQTT_BROKER_ADDR, MQTT_TOPIC);
    return true;
}

/* Paho 内部线程回调：收到传感器数据 → cJSON 解析 → 整帧推入边缘引擎 */
static int mqtt_msg_arrived(void * ctx, char * topic, int topic_len,
                             MQTTClient_message * msg)
{
    (void)ctx;
    cJSON * root = NULL;
    char * buf = (char *)malloc(msg->payloadlen + 1);

    if(buf) {
        /* 拷贝 payload 到空终止缓冲区 */
        memcpy(buf, msg->payload, msg->payloadlen);
        buf[msg->payloadlen] = '\0';

        printf("[MQTT] 收到 topic=%.*s  len=%d\n", topic_len, topic,
               msg->payloadlen);

        /* cJSON 解析整条 JSON → 构造整帧 raw_sample_t → 推入边缘引擎 */
        root = cJSON_Parse(buf);
        if(root) {
            raw_sample_t s;
            memset(&s, 0, sizeof(s));
            s.ts  = (uint32_t)time(NULL);
            cJSON * seq_item = cJSON_GetObjectItemCaseSensitive(root, "seq");
            s.seq = cJSON_IsNumber(seq_item) ? (int32_t)seq_item->valuedouble : -1;

            int got = 0;
            for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
                cJSON * item = cJSON_GetObjectItemCaseSensitive(root, s_keys[i]);
                if(cJSON_IsNumber(item)) {
                    s.values[i] = (float)item->valuedouble;
                    got++;
                }
            }
            /* 仅完整帧入引擎; 缺字段帧丢弃, 避免污染分析 */
            if(got == SENSOR_IDX_COUNT) {
                edge_engine_push(&s);
                printf("[MQTT] 已入引擎 seq=%d temp=%.1f humi=%.1f light=%.0f do=%.1f ph=%.1f nh3n=%.2f\n",
                       (int)s.seq, s.values[0], s.values[1], s.values[2],
                       s.values[3], s.values[4], s.values[5]);
            } else {
                printf("[MQTT] 帧不完整 (%d/%d), 已丢弃\n", got, SENSOR_IDX_COUNT);
            }

        } else {
            const char * err = cJSON_GetErrorPtr();
            printf("[MQTT] JSON 解析失败: %.60s\n", err ? err : "(null)");
        }

        cJSON_Delete(root);   /* NULL 安全 */
        free(buf);
    }

    MQTTClient_freeMessage(&msg);
    MQTTClient_free(topic);
    return 1;
}

static void mqtt_conn_lost(void * ctx, char * cause)
{
    (void)ctx;
    printf("[MQTT] ✗ 连接断开! 原因: %s (将在 10s 内自动重连)\n",
           cause ? cause : "unknown");
    g_connected = false;
    /* 引擎持久化后断连不清空数据; reset_all 现仅打日志, 卡片保留上次有效值 */
    app_action_sensor_reset_all();
}

#endif /* APP_USE_MQTT */
