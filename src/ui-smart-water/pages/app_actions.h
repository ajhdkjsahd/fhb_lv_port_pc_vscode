// ========== app_actions.h ==========
// Callback implementations for all user interactions.
// Edit the .c file to replace stubs with real business logic.
#ifndef APP_ACTIONS_H
#define APP_ACTIONS_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"
#include <stdbool.h>
#include "video-page/video_page.h"
#include "../edge/sensor_range.h"   /* sensor_idx_t + 物理量程表 (引擎与 UI 共用) */

/* ---- WiFi Status ---- */
typedef enum {
    WIFI_STATUS_UNKNOWN = 0,  /* Not yet checked */
    WIFI_STATUS_GREEN   = 1,  /* External network reachable */
    WIFI_STATUS_YELLOW  = 2,  /* LAN only (gateway reachable, no WAN) */
    WIFI_STATUS_RED     = 3,  /* No network at all */
} wifi_status_t;

/** Check network connectivity by pinging external + gateway addresses.
 *  Blocks for up to ~3 seconds on first call (ping timeouts).
 *  Safe to call from LVGL timer — but prefer infrequent calls (every 10-15s). */
wifi_status_t app_action_check_wifi(void);

/* ---- Login / Register ---- */
bool app_action_login_verify(const char * username, const char * password);
bool app_action_register_submit(const char * username, const char * password);
void app_action_login_success(void);

/* ---- Video Player ---- */
void app_action_set_video_screen(lv_obj_t * screen);
void app_action_video_stop(void);
void app_action_video_control(video_action_t action);
void app_action_video_seek(int32_t position);

/** Scan the videos/ folder, populate internal list. Call once before video_page_create. */
void app_action_video_scan(void);

/** Get video path list (after scanning). Returns NULL if no videos found. */
const char * const * app_action_video_get_paths(void);

/** Get number of videos found. */
int  app_action_video_get_count(void);

/** Select a video by index (from swipe). Does NOT auto-play. */
void app_action_video_select(int index);

/** Get cover image path for video at index. Returns NULL if not available. */
const char * app_action_video_get_cover(int index);

/* ---- Network Communication ---- */
void app_action_set_network_screen(lv_obj_t * screen);
void app_action_network_connect(const char * ip, const char * port);
void app_action_network_disconnect(void);
void app_action_network_send(const char * message);

/* ---- AI Chat (Ollama) ---- */
void app_action_ai_init(void);
void app_action_ai_set_screen(lv_obj_t * screen);
void app_action_ai_send(const char * message);
void app_action_ai_stop(void);

/* ---- Sensor Data (传感器数据) ---- */
/* 6 路传感器索引 sensor_idx_t 已移至 sensor_range.h, 供边缘引擎与 UI 共用。 */

/** 读取一路传感器当前值（来自边缘引擎快照 edge_engine_get_latest）。
 *  引擎在 MQTT/PC sim 写入并清洗后维护「最近有效值」; 断网/重启不丢失。 */
bool app_action_sensor_read(sensor_idx_t idx, float * value);

/** 写入一路传感器原始值（向后兼容入口; 实际由 app_mqtt.c 调 edge_engine_push）。 */
void app_action_sensor_set(sensor_idx_t idx, float value);

/** 旧版「断连清空」入口; 引擎持久化后保留数据, 本函数仅打日志。 */
void app_action_sensor_reset_all(void);

#ifdef __cplusplus
}
#endif
#endif
