// ========== app_mqtt.h ==========
// GEC6818 MQTT 传感器数据订阅模块。
// 依赖 Eclipse Paho MQTT C 库（libpaho-mqtt3c + libpaho-mqtt3a）。
// 仅在定义 APP_USE_MQTT 时启用；未定义时所有接口退化为空桩，不影响编译。
#ifndef APP_MQTT_H
#define APP_MQTT_H
#include <stdbool.h>

#ifdef APP_USE_MQTT

/** 初始化 MQTT 客户端：连接 broker → 订阅传感器主题。
 *  配置通过 app_mqtt.c 顶部 MQTT_BROKER_ADDR / CLIENT_ID / TOPIC 宏控制。
 *  返回 true 表示连接+订阅成功；失败时传感器页不显示数据。 */
bool app_mqtt_init(void);

/** 断开连接并销毁客户端 */
void app_mqtt_deinit(void);

/** 检查 MQTT 连接是否存活；断开时非阻塞重连（可从 LVGL 定时器周期调用） */
void app_mqtt_ensure_connected(void);

#else  /* !APP_USE_MQTT — PC / 未启用 MQTT */

static inline bool app_mqtt_init(void) { return false; }
static inline void app_mqtt_deinit(void) {}
static inline void app_mqtt_ensure_connected(void) {}

#endif /* APP_USE_MQTT */
#endif /* APP_MQTT_H */
