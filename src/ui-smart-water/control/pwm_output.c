// ========== pwm_output.c ==========
#include "pwm_output.h"
#include <stdio.h>

/* ===== PWM 硬件配置 (GEC6818 / S5P6818, 板子实测后修改) =====
 * Linux 标准 PWM sysfs:
 *   /sys/class/pwm/pwmchipX/export         → 写通道号 "Y" 导出
 *   /sys/class/pwm/pwmchipX/pwmY/period     → 周期(ns)
 *   /sys/class/pwm/pwmchipX/pwmY/duty_cycle → 占空比(ns)
 *   /sys/class/pwm/pwmchipX/pwmY/enable     → 1 使能
 *
 * 三路设备接电机驱动板的 PWM 输入端。
 * 当前板子未接驱动板,写 sysfs 返回值会被忽略(失败也不影响 UI),
 * 接上驱动板后 duty_cycle 即真实调速。
 */
#ifdef __linux__
  #define PWM_PERIOD_NS        10000000U   /* 10ms = 100Hz, 适合电机调速 */

  #define PWM_AERATOR_CHIP     "/sys/class/pwm/pwmchip0"
  #define PWM_AERATOR_CHAN     "0"
  #define PWM_PUMP_CHIP        "/sys/class/pwm/pwmchip1"
  #define PWM_PUMP_CHAN        "0"
  #define PWM_FEEDER_CHIP      "/sys/class/pwm/pwmchip2"
  #define PWM_FEEDER_CHAN      "0"
#endif

static float g_duty[PWM_DEV_COUNT] = {0};

#ifdef __linux__
static const char * chip_path(pwm_dev_t d)
{
    switch (d) {
        case PWM_DEV_AERATOR: return PWM_AERATOR_CHIP;
        case PWM_DEV_PUMP:    return PWM_PUMP_CHIP;
        case PWM_DEV_FEEDER:  return PWM_FEEDER_CHIP;
        default: return NULL;
    }
}

static const char * chan_str(pwm_dev_t d)
{
    switch (d) {
        case PWM_DEV_AERATOR: return PWM_AERATOR_CHAN;
        case PWM_DEV_PUMP:    return PWM_PUMP_CHAN;
        case PWM_DEV_FEEDER:  return PWM_FEEDER_CHAN;
        default: return NULL;
    }
}

/* 向 sysfs 文件写一个字符串。失败静默(板子未接驱动板时 export 会失败)。 */
static void sysfs_write_str(const char *path, const char *val)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(val, f);
    fclose(f);
}

static void sysfs_write_uint(const char *path, unsigned int v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", v);
    sysfs_write_str(path, buf);
}
#endif /* __linux__ */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool pwm_init(pwm_dev_t dev)
{
    if (dev < 0 || dev >= PWM_DEV_COUNT) return false;
    g_duty[dev] = 0.0f;

#ifdef __linux__
    const char *chip = chip_path(dev);
    const char *chan = chan_str(dev);
    if (!chip || !chan) return true;   /* 配置缺失,仅记值 */

    char path[160];

    /* 导出通道(已导出则 export 会失败,忽略) */
    snprintf(path, sizeof(path), "%s/export", chip);
    sysfs_write_str(path, chan);

    /* 设周期 */
    snprintf(path, sizeof(path), "%s/pwm%s/period", chip, chan);
    sysfs_write_uint(path, PWM_PERIOD_NS);

    /* 占空比初值 0 */
    snprintf(path, sizeof(path), "%s/pwm%s/duty_cycle", chip, chan);
    sysfs_write_uint(path, 0);

    /* 使能 */
    snprintf(path, sizeof(path), "%s/pwm%s/enable", chip, chan);
    sysfs_write_uint(path, 1);
#else
    /* PC 桩: 无硬件操作 */
#endif
    return true;
}

bool pwm_set_duty(pwm_dev_t dev, float duty_pct)
{
    if (dev < 0 || dev >= PWM_DEV_COUNT) return false;
    duty_pct = clampf(duty_pct, 0.0f, 100.0f);
    g_duty[dev] = duty_pct;

#ifdef __linux__
    const char *chip = chip_path(dev);
    const char *chan = chan_str(dev);
    if (chip && chan) {
        char path[160];
        unsigned int duty_ns = (unsigned int)(duty_pct / 100.0f * (float)PWM_PERIOD_NS);
        snprintf(path, sizeof(path), "%s/pwm%s/duty_cycle", chip, chan);
        sysfs_write_uint(path, duty_ns);
    }
#endif
    return true;
}

float pwm_get_duty(pwm_dev_t dev)
{
    if (dev < 0 || dev >= PWM_DEV_COUNT) return 0.0f;
    return g_duty[dev];
}

void pwm_deinit(void)
{
    int i;
    for (i = 0; i < PWM_DEV_COUNT; i++) {
        pwm_set_duty((pwm_dev_t)i, 0.0f);
    }
}
