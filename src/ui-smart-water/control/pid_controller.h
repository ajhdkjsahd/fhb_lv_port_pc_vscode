// ========== pid_controller.h ==========
// 通用 PID 控制算法 — 位置式,带积分限幅 + 输出限幅 + 抗积分饱和。
//
// 纯 C,不依赖 LVGL / 平台。被 control_loop.c 用于增氧机 DO 闭环。
//
// 公式:  u(t) = Kp·e + Ki·∫e·dt + Kd·de/dt
//   e = setpoint - measured
//   输出限幅 [out_min, out_max] (增氧机场景 = [0,100]% PWM)
//   积分限幅 integ_limit,且输出饱和时回卷积分(抗饱和)
//
#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 增益 */
    float Kp, Ki, Kd;
    /* 设定值 */
    float setpoint;
    /* 内部状态 */
    float integral;       /* 积分累积 */
    float prev_error;     /* 上次误差(微分用) */
    /* 限幅 */
    float out_min, out_max;
    float integ_limit;
    /* 上一次分量缓存(UI 展示用) */
    float last_p, last_i, last_d, last_out;
} pid_ctrl_t;

/* 初始化。out_min/out_max = 输出限幅。积分限幅自动取输出范围一半。 */
void  pid_init(pid_ctrl_t *p, float Kp, float Ki, float Kd,
               float out_min, float out_max);

/* 运行期改增益(不重置积分) */
void  pid_set_gains(pid_ctrl_t *p, float Kp, float Ki, float Kd);
void  pid_set_setpoint(pid_ctrl_t *p, float sp);   /* SP 大变时清积分,防过冲 */

void  pid_get_gains(const pid_ctrl_t *p, float *Kp, float *Ki, float *Kd);
float pid_get_setpoint(const pid_ctrl_t *p);

/* 一次 PID 计算。dt=采样间隔(秒)。返回限幅后输出。 */
float pid_compute(pid_ctrl_t *p, float measured, float dt);

/* 清积分+前次误差(手动切自动时调,抗饱和残留) */
void  pid_reset(pid_ctrl_t *p);

/* 查询最近一次的 P/I/D 分量(UI 展示) */
void  pid_get_terms(const pid_ctrl_t *p, float *p_term, float *i_term, float *d_term);

#ifdef __cplusplus
}
#endif
#endif /* PID_CONTROLLER_H */
