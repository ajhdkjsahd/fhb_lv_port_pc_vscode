// ========== pid_controller.c ==========
#include "pid_controller.h"

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void pid_init(pid_ctrl_t *p, float Kp, float Ki, float Kd,
              float out_min, float out_max)
{
    p->Kp = Kp; p->Ki = Ki; p->Kd = Kd;
    p->setpoint   = 0;
    p->integral   = 0;
    p->prev_error = 0;
    p->out_min    = out_min;
    p->out_max    = out_max;
    /* 积分限幅默认取输出范围的一半,够用且不易饱和 */
    p->integ_limit = (out_max - out_min) * 0.5f;
    p->last_p = p->last_i = p->last_d = p->last_out = 0;
}

void pid_set_gains(pid_ctrl_t *p, float Kp, float Ki, float Kd)
{
    p->Kp = Kp; p->Ki = Ki; p->Kd = Kd;
}

void pid_set_setpoint(pid_ctrl_t *p, float sp)
{
    /* SP 变化大时清积分,避免过冲 */
    if (sp != p->setpoint) {
        p->integral = 0;
    }
    p->setpoint = sp;
}

void pid_get_gains(const pid_ctrl_t *p, float *Kp, float *Ki, float *Kd)
{
    if (Kp) *Kp = p->Kp;
    if (Ki) *Ki = p->Ki;
    if (Kd) *Kd = p->Kd;
}

float pid_get_setpoint(const pid_ctrl_t *p)
{
    return p->setpoint;
}

float pid_compute(pid_ctrl_t *p, float measured, float dt)
{
    if (dt <= 0.0f) dt = 1.0f;   /* 防除零 */

    float error = p->setpoint - measured;

    /* 积分(带限幅) */
    p->integral += error * dt;
    p->integral  = clampf(p->integral, -p->integ_limit, p->integ_limit);

    /* 微分 */
    float deriv = (error - p->prev_error) / dt;
    p->prev_error = error;

    /* 三分量 */
    p->last_p = p->Kp * error;
    p->last_i = p->Ki * p->integral;
    p->last_d = p->Kd * deriv;

    float out = p->last_p + p->last_i + p->last_d;
    out = clampf(out, p->out_min, p->out_max);

    /* 抗积分饱和:输出饱和且误差同向 → 回卷积分 */
    if (out >= p->out_max && error > 0)
        p->integral -= error * dt;
    else if (out <= p->out_min && error < 0)
        p->integral -= error * dt;

    p->last_out = out;
    return out;
}

void pid_reset(pid_ctrl_t *p)
{
    p->integral   = 0;
    p->prev_error = 0;
}

void pid_get_terms(const pid_ctrl_t *p, float *p_term, float *i_term, float *d_term)
{
    if (p_term) *p_term = p->last_p;
    if (i_term) *i_term = p->last_i;
    if (d_term) *d_term = p->last_d;
}
