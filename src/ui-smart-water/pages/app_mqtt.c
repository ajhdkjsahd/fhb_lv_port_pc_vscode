// ========== app_mqtt.c ==========
// GEC6818 MQTT 传感器数据订阅模块。
// 依赖 Eclipse Paho MQTT C 库。仅在 APP_USE_MQTT 宏定义时编译。
// 收到 JSON 消息后解析各路传感器数值，调用 app_action_sensor_set() 存入全局存储，
// 由 sensor_page 定时器通过 app_action_sensor_read() 取用。
#ifdef APP_USE_MQTT

#include "app_mqtt.h"
#include "app_actions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    /* 库内部线程可能已经掉了，尝试静默重连 */
    if(g_connected) return;
    mqtt_do_connect();
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
        printf("[MQTT] create failed, rc=%d\n", rc);
        g_client = NULL;
        return false;
    }

    /* 2. 设置回调 */
    rc = MQTTClient_setCallbacks(g_client, NULL,
                                  mqtt_conn_lost, mqtt_msg_arrived, NULL);
    if(rc != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] setCallbacks failed, rc=%d\n", rc);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
        return false;
    }

    /* 3. 连接 broker */
    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = 60;
    opts.cleansession      = 1;
    opts.connectTimeout    = 10;

    printf("[MQTT] connecting %s ...\n", MQTT_BROKER_ADDR);
    rc = MQTTClient_connect(g_client, &opts);
    if(rc != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] connect failed, rc=%d\n", rc);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
        return false;
    }

    /* 4. 订阅主题 */
    rc = MQTTClient_subscribe(g_client, MQTT_TOPIC, MQTT_QOS);
    if(rc != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] subscribe failed, rc=%d\n", rc);
        MQTTClient_disconnect(g_client, MQTT_TIMEOUT);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
        return false;
    }

    g_connected = true;
    printf("[MQTT] connected & subscribed: %s\n", MQTT_TOPIC);
    return true;
}

/* Paho 内部线程回调：收到传感器数据 → cJSON 解析 → 存入全局存储 */
static int mqtt_msg_arrived(void * ctx, char * topic, int topic_len,
                             MQTTClient_message * msg)
{
    (void)ctx;
    int updated = 0;
    cJSON * root = NULL;
    char * buf = (char *)malloc(msg->payloadlen + 1);

    if(buf) {
        /* 拷贝 payload 到空终止缓冲区 */
        memcpy(buf, msg->payload, msg->payloadlen);
        buf[msg->payloadlen] = '\0';

        printf("[MQTT] recv topic=%.*s  len=%d\n", topic_len, topic,
               msg->payloadlen);

        /* cJSON 解析整条 JSON */
        root = cJSON_Parse(buf);
        if(root) {
            for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
                cJSON * item = cJSON_GetObjectItemCaseSensitive(root, s_keys[i]);
                if(cJSON_IsNumber(item)) {
                    app_action_sensor_set((sensor_idx_t)i, (float)item->valuedouble);
                    updated++;
                }
            }
        } else {
            const char * err = cJSON_GetErrorPtr();
            printf("[MQTT] JSON parse failed near: %.40s\n", err ? err : "(null)");
        }
        printf("[MQTT] parsed %d/%d sensor values\n", updated, SENSOR_IDX_COUNT);

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
    printf("[MQTT] connection lost: %s\n", cause ? cause : "unknown");
    g_connected = false;
    /* 断连后清空数据，传感器页恢复 "--" */
    app_action_sensor_reset_all();
}

#endif /* APP_USE_MQTT */
