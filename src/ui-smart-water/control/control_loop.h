// ========== control_loop.h ==========
// 闭环控制引擎 — 采集 → 判断 → PID/阈值 → PWM → 反馈。
//
// 数据流:
//   edge_engine_get_latest(DO/温度/pH/光照)   ← 采集(断网时读本地缓存)
//                 ↓
//   自动模式: 增氧机=PID(DO闭环)  水泵=阈值启停  投喂=定时定量
//   手动模式: 三设备由人直驱(ON/OFF / slider / 单次投喂)
//                 ↓
//   pwm_set_duty(三路)                          ← 执行(写 sysfs,接电机即真实调速)
//                 ↓
//   记历史环(PV/SP)                              ← 反馈(UI 画曲线)
//
// 驱动: 由 UI 的 lv_timer(500ms) 调 control_step(),Linux/PC 通用,
//       天然在 LVGL 线程,断网独立(只依赖 edge 本地缓存)。
//
#ifndef CONTROL_LOOP_H
#define CONTROL_LOOP_H
#include <stdbool.h>
#include "pid_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CTRL_HIST_N 120   /* 历史环容量 (500ms/条 → 60s) */

typedef enum {
    CTRL_MODE_AUTO = 0,    /* 闭环控制 Tab: PID + 阈值自主 */
    CTRL_MODE_MANUAL       /* 手动控制 Tab: 人直驱 */
} ctrl_mode_t;

/* 循环水泵阈值 (自动模式) */
typedef struct {
    float temp_max;   /* 温度上限 °C   — 超过 → 循环换水 */
    float ph_min;     /* pH 下限       — 低于 → 循环换水 */
    float ph_max;     /* pH 上限       — 高于 → 循环换水 */
} pump_threshold_t;

/* 投喂电机定时 (自动模式,无反馈传感器→纯定时) */
typedef struct {
    int feed_hour1;   /* 投喂时段1 起始小时 (0~23) */
    int feed_hour2;   /* 投喂时段2 起始小时 */
    int feed_minutes; /* 每次投喂持续分钟 */
} feeder_schedule_t;

/* UI/AI 只读状态快照 */
typedef struct {
    ctrl_mode_t mode;
    bool estop;

    /* 实时传感器值 */
    float do_val, temp, ph, light;

    /* 增氧机 PID */
    float aerator_sp;     /* DO 设定值 mg/L */
    float aerator_duty;   /* 当前 PWM% */
    float pid_p, pid_i, pid_d;

    /* 水泵 */
    bool  pump_on;
    /* 投喂 */
    bool  feeder_on;
    float feeder_duty;
} control_status_t;

/* ===== 生命周期 ===== */
void control_init(void);     /* 初始化 PID 默认参数 + 三路 PWM */
void control_deinit(void);

/* 一次闭环 (lv_timer 周期调用) */
void control_step(void);

/* ===== 模式 ===== */
void control_set_mode(ctrl_mode_t m);
ctrl_mode_t control_get_mode(void);
void control_estop(void);        /* 急停: 三路 PWM 立即归零 */
void control_clear_estop(void);

/* ===== 增氧机 PID ===== */
void  control_set_aerator_sp(float do_mgpl);
void  control_set_pid_gains(float Kp, float Ki, float Kd);
void  control_get_pid(float *Kp, float *Ki, float *Kd, float *sp);

/* ===== 水泵阈值 ===== */
void control_set_pump_threshold(float temp_max, float ph_min, float ph_max);
void control_get_pump_threshold(pump_threshold_t *out);

/* ===== 投喂定时 ===== */
void control_set_feeder_schedule(int h1, int h2, int minutes);
void control_get_feeder_schedule(feeder_schedule_t *out);
void control_set_feeder_duty(float duty_pct);   /* 投喂电机工作占空比 0~100 */

/* ===== 手动控制 (仅手动模式生效) ===== */
void control_manual_set_aerator(float duty_pct);   /* 0~100 */
void control_manual_set_pump(bool on);
void control_manual_feed_trigger(void);            /* 单次投喂,持续 feed_minutes */

/* ===== 状态查询 (UI 刷新 + AI 读取) ===== */
void control_get_status(control_status_t *out);
int  control_get_pv_history(float *buf, int max);  /* 返回写入条数(时间正序) */
int  control_get_sp_history(float *buf, int max);

#ifdef __cplusplus
}
#endif
#endif /* CONTROL_LOOP_H */
