// ========== pwm_output.h ==========
// PWM 输出抽象层 — 屏蔽 Linux sysfs / PC 桩差异。
//
// 三路设备各占一个 PWM 通道:
//   PWM_DEV_AERATOR  增氧机  (PID 闭环调速)
//   PWM_DEV_PUMP     循环水泵(阈值启停: 0 / 100%)
//   PWM_DEV_FEEDER   投喂电机(定时定量: 固定占空比)
//
// Linux: 写 /sys/class/pwm/pwmchipX/pwmY/{period,duty_cycle,enable}
//        路径在 .c 顶部宏配置,板子实测后修改。
//        当前板子未接电机驱动板,写入 sysfs 成功但无物理效果;
//        接上驱动板后即可真实调速。
// PC  : 桩,只记录占空比返回 true,UI 可演示闭环曲线。
//
#ifndef PWM_OUTPUT_H
#define PWM_OUTPUT_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PWM_DEV_AERATOR = 0,
    PWM_DEV_PUMP,
    PWM_DEV_FEEDER,
    PWM_DEV_COUNT
} pwm_dev_t;

/* 初始化指定通道(导出 pwm 节点 + 设周期)。幂等。 */
bool  pwm_init(pwm_dev_t dev);

/* 设置占空比 0~100%。记录最新值并(板端)写 sysfs。 */
bool  pwm_set_duty(pwm_dev_t dev, float duty_pct);

/* 读取最近一次设置的占空比(不读硬件)。 */
float pwm_get_duty(pwm_dev_t dev);

/* 全部归零(急停/退出用)。 */
void  pwm_deinit(void);

#ifdef __cplusplus
}
#endif
#endif /* PWM_OUTPUT_H */
