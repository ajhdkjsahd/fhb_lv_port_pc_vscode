// ========== control_loop.c ==========
#include "control_loop.h"
#include "pwm_output.h"
#include "../edge/edge_engine.h"   /* edge_engine_get_latest + SENSOR_IDX_* */
#include "../pages/app_mqtt.h"     /* app_mqtt_publish: 下行控制 (水泵→MQTT cmd) */
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ===== 默认参数 ===== */
#define DEF_KP           2.5f
#define DEF_KI           0.1f
#define DEF_KD           0.5f
#define DEF_DO_SP        6.0f     /* 溶氧设定值 mg/L */
#define DEF_FEEDER_DUTY  60.0f    /* 投喂电机工作占空比 % */
#define STEP_DT          0.5f     /* 控制周期 500ms */
#define MQTT_CMD_TOPIC   "fhb/smart_aquaculture/cmd"   /* 下行控制主题 (水泵开关) */

typedef struct {
    pid_ctrl_t             aerator_pid;
    ctrl_mode_t       mode;
    bool              estop;

    pump_threshold_t  pump_thr;
    feeder_schedule_t feeder_sch;
    float             feeder_duty;   /* 投喂工作占空比 */

    /* 手动 */
    float             manual_aerator_duty;
    bool              manual_pump_on;
    time_t            manual_feed_start;  /* 0=未触发 */

    /* 实时值 (最近一次 step) */
    float do_val, temp, ph, light;
    float aerator_duty;
    float pid_p, pid_i, pid_d;
    bool  pump_on;
    bool  last_pump_on;    /* 上次水泵状态 (边沿检测 → MQTT 下行) */
    bool  feeder_on;

    /* 历史 */
    float pv_hist[CTRL_HIST_N];
    float sp_hist[CTRL_HIST_N];
    int   hist_idx, hist_count;

    uint32_t step_count;
} control_state_t;

static control_state_t g;

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void control_init(void)
{
    memset(&g, 0, sizeof(g));

    pid_init(&g.aerator_pid, DEF_KP, DEF_KI, DEF_KD, 0.0f, 100.0f);
    pid_set_setpoint(&g.aerator_pid, DEF_DO_SP);

    g.mode         = CTRL_MODE_AUTO;
    g.pump_thr.temp_max = 32.0f;
    g.pump_thr.ph_min   = 6.5f;
    g.pump_thr.ph_max   = 9.0f;
    g.feeder_sch.feed_hour1   = 8;
    g.feeder_sch.feed_hour2   = 17;
    g.feeder_sch.feed_minutes = 15;
    g.feeder_duty  = DEF_FEEDER_DUTY;

    g.manual_aerator_duty = 0.0f;
    g.manual_pump_on      = false;
    g.manual_feed_start   = 0;

    /* 三路 PWM 上电初始化 */
    for (int i = 0; i < PWM_DEV_COUNT; i++) {
        pwm_init((pwm_dev_t)i);
    }
}

void control_deinit(void)
{
    pwm_deinit();
}

void control_step(void)
{
    /* ── 采集 (断网时 edge 本地缓存仍有上次值) ── */
    uint32_t ts;
    float do_v = 0, t_v = 0, ph_v = 0, li_v = 0;
    edge_engine_get_latest(SENSOR_IDX_DO,   &do_v, &ts);
    edge_engine_get_latest(SENSOR_IDX_TEMP, &t_v,  NULL);
    edge_engine_get_latest(SENSOR_IDX_PH,   &ph_v, NULL);
    edge_engine_get_latest(SENSOR_IDX_LIGHT,&li_v, NULL);
    g.do_val = do_v; g.temp = t_v; g.ph = ph_v; g.light = li_v;

    if (g.estop) {
        g.aerator_duty = 0; g.pump_on = false; g.feeder_on = false;
        pwm_set_duty(PWM_DEV_AERATOR, 0);
        pwm_set_duty(PWM_DEV_PUMP,    0);
        pwm_set_duty(PWM_DEV_FEEDER,  0);
    }
    else if (g.mode == CTRL_MODE_AUTO) {
        /* ── 增氧机: PID 闭环 (DO 误差 → PWM%) ── */
        float duty = pid_compute(&g.aerator_pid, do_v, STEP_DT);
        g.aerator_duty = duty;
        pid_get_terms(&g.aerator_pid, &g.pid_p, &g.pid_i, &g.pid_d);
        pwm_set_duty(PWM_DEV_AERATOR, duty);

        /* ── 循环水泵: 阈值启停 ── */
        bool pump = (t_v  > g.pump_thr.temp_max) ||
                    (ph_v < g.pump_thr.ph_min)   ||
                    (ph_v > g.pump_thr.ph_max);
        g.pump_on = pump;
        pwm_set_duty(PWM_DEV_PUMP, pump ? 100.0f : 0.0f);

        /* ── 投喂电机: 定时定量 ── */
        time_t now = time(NULL);
        struct tm * lt = localtime(&now);
        int now_min = lt->tm_hour * 60 + lt->tm_min;
        int f1 = g.feeder_sch.feed_hour1 * 60;
        int f2 = g.feeder_sch.feed_hour2 * 60;
        int dur = g.feeder_sch.feed_minutes;
        bool feed = (now_min >= f1 && now_min < f1 + dur) ||
                    (now_min >= f2 && now_min < f2 + dur);
        g.feeder_on = feed;
        pwm_set_duty(PWM_DEV_FEEDER, feed ? g.feeder_duty : 0.0f);
    }
    else {
        /* ── 手动模式: 人直驱 ── */
        g.aerator_duty = g.manual_aerator_duty;
        pwm_set_duty(PWM_DEV_AERATOR, g.manual_aerator_duty);

        g.pump_on = g.manual_pump_on;
        pwm_set_duty(PWM_DEV_PUMP, g.manual_pump_on ? 100.0f : 0.0f);

        /* 单次投喂: 触发后持续 feed_minutes */
        if (g.manual_feed_start > 0) {
            if (time(NULL) - g.manual_feed_start < (time_t)g.feeder_sch.feed_minutes * 60) {
                g.feeder_on = true;
                pwm_set_duty(PWM_DEV_FEEDER, g.feeder_duty);
            } else {
                g.feeder_on = false;
                g.manual_feed_start = 0;
                pwm_set_duty(PWM_DEV_FEEDER, 0.0f);
            }
        } else {
            g.feeder_on = false;
            pwm_set_duty(PWM_DEV_FEEDER, 0.0f);
        }
    }

    /* ── 反馈: 记历史环 ── */
    g.pv_hist[g.hist_idx] = do_v;
    g.sp_hist[g.hist_idx] = pid_get_setpoint(&g.aerator_pid);
    g.hist_idx = (g.hist_idx + 1) % CTRL_HIST_N;
    if (g.hist_count < CTRL_HIST_N) g.hist_count++;
    g.step_count++;

    /* ── 下行控制: 水泵状态边沿 → MQTT 发布 cmd (打通 手动/自动 → 设备 链路) ──
     * 手动 ON/OFF、自动阈值启停、急停 → 任一使 pump_on 翻转即发布一次
     * {"cmd":"motor","state":1|0} 到 MQTT_CMD_TOPIC, 由下位机/执行端订阅执行。 */
    if (g.pump_on != g.last_pump_on) {
        char payload[64];
        snprintf(payload, sizeof(payload),
                 "{\"cmd\":\"motor\",\"state\":%d}", g.pump_on ? 1 : 0);
        app_mqtt_publish(MQTT_CMD_TOPIC, payload);
        g.last_pump_on = g.pump_on;
    }
}

/* ===== 模式 ===== */
void control_set_mode(ctrl_mode_t m)
{
    if (g.mode == m) return;
    g.mode = m;
    if (m == CTRL_MODE_AUTO) {
        /* 手动→自动: 清积分抗饱和残留 */
        pid_reset(&g.aerator_pid);
    } else {
        /* 自动→手动: 手动值继承当前输出,平滑过渡 */
        g.manual_aerator_duty = g.aerator_duty;
        g.manual_pump_on      = g.pump_on;
        g.manual_feed_start   = 0;
    }
}

ctrl_mode_t control_get_mode(void) { return g.mode; }

void control_estop(void)
{
    g.estop = true;
    pwm_set_duty(PWM_DEV_AERATOR, 0);
    pwm_set_duty(PWM_DEV_PUMP,    0);
    pwm_set_duty(PWM_DEV_FEEDER,  0);
}

void control_clear_estop(void) { g.estop = false; }

/* ===== 增氧机 PID ===== */
void control_set_aerator_sp(float sp)
{
    pid_set_setpoint(&g.aerator_pid, sp);
}

void control_set_pid_gains(float Kp, float Ki, float Kd)
{
    pid_set_gains(&g.aerator_pid, Kp, Ki, Kd);
}

void control_get_pid(float *Kp, float *Ki, float *Kd, float *sp)
{
    pid_get_gains(&g.aerator_pid, Kp, Ki, Kd);
    if (sp) *sp = pid_get_setpoint(&g.aerator_pid);
}

/* ===== 水泵阈值 ===== */
void control_set_pump_threshold(float temp_max, float ph_min, float ph_max)
{
    g.pump_thr.temp_max = temp_max;
    g.pump_thr.ph_min   = ph_min;
    g.pump_thr.ph_max   = ph_max;
}

void control_get_pump_threshold(pump_threshold_t *out)
{
    if (out) *out = g.pump_thr;
}

/* ===== 投喂定时 ===== */
void control_set_feeder_schedule(int h1, int h2, int minutes)
{
    g.feeder_sch.feed_hour1   = h1;
    g.feeder_sch.feed_hour2   = h2;
    g.feeder_sch.feed_minutes = minutes;
}

void control_get_feeder_schedule(feeder_schedule_t *out)
{
    if (out) *out = g.feeder_sch;
}

void control_set_feeder_duty(float duty)
{
    g.feeder_duty = clampf(duty, 0.0f, 100.0f);
}

/* ===== 手动控制 ===== */
void control_manual_set_aerator(float duty)
{
    g.manual_aerator_duty = clampf(duty, 0.0f, 100.0f);
}

void control_manual_set_pump(bool on)
{
    g.manual_pump_on = on;
}

void control_manual_feed_trigger(void)
{
    g.manual_feed_start = time(NULL);
}

/* ===== 状态查询 ===== */
void control_get_status(control_status_t *out)
{
    if (!out) return;
    out->mode         = g.mode;
    out->estop        = g.estop;
    out->do_val       = g.do_val;
    out->temp         = g.temp;
    out->ph           = g.ph;
    out->light        = g.light;
    out->aerator_sp   = pid_get_setpoint(&g.aerator_pid);
    out->aerator_duty = g.aerator_duty;
    out->pid_p        = g.pid_p;
    out->pid_i        = g.pid_i;
    out->pid_d        = g.pid_d;
    out->pump_on      = g.pump_on;
    out->feeder_on    = g.feeder_on;
    out->feeder_duty  = g.feeder_duty;
}

int control_get_pv_history(float *buf, int max)
{
    if (!buf || max <= 0) return 0;
    int n = g.hist_count < max ? g.hist_count : max;
    int start = (g.hist_idx - g.hist_count + CTRL_HIST_N) % CTRL_HIST_N;
    for (int i = 0; i < n; i++) {
        buf[i] = g.pv_hist[(start + i) % CTRL_HIST_N];
    }
    return n;
}

int control_get_sp_history(float *buf, int max)
{
    if (!buf || max <= 0) return 0;
    int n = g.hist_count < max ? g.hist_count : max;
    int start = (g.hist_idx - g.hist_count + CTRL_HIST_N) % CTRL_HIST_N;
    for (int i = 0; i < n; i++) {
        buf[i] = g.sp_hist[(start + i) % CTRL_HIST_N];
    }
    return n;
}
