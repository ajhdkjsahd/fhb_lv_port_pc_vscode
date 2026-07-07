// ========== ai_hardware.h ==========
// AI 硬件控制模块 — GEC6818 板卡底层硬件动作 (板卡实测验证)
//
//   现在由 Qwen2.5:7b 原生 Function Calling 驱动:
//     1. 模型输出 tool_calls JSON → ai_agent.c 解析
//     2. ai_tools.c 的 tool handler 把参数翻译成 action_name
//     3. ai_execute_action() 通过 Linux sysfs / input 子系统操作硬件
//     4. 执行结果 (JSON) 回传模型 → 模型基于真实数据生成自然语言回复
//
//   平台支持：
//     - GEC6818 ARM Linux：真实 GPIO + LED 子系统 + I2C (MMA8653)
//     - PC (Windows)：模拟返回值，用于 UI 调试
//
//   硬件配置（GEC6818 板卡）：
//     D7 LED → led1 → B26 → GPIO 58  (LED 子系统: /sys/class/leds/led1/)
//     D8 LED → led2 → C11 → GPIO 75  (LED 子系统: /sys/class/leds/led2/)
//     D9 LED → led3 → C7  → GPIO 71  (LED 子系统: /sys/class/leds/led3/)
//     D10 LED→ led4 → C12 → GPIO 76  (LED 子系统: /sys/class/leds/led4/)
//     K2 按键 → A28 → GPIO 28 (输入，外部上拉，按下=低电平)
//     蜂鸣器   → PWM2 → GPIO 78 (GPIOC14，高电平鸣响)
//     MMA8653 → I2C-2 地址 0x1D（可能被内核驱动占用，需实测确认）
//
//   GPIO 编号公式 (S5P6818 / Nexell, 已通过 gpiochip 验证)：
//     GPIOA = 0, GPIOB = 32, GPIOC = 64, GPIOD = 96, GPIOE = 128
//
#ifndef AI_HARDWARE_H
#define AI_HARDWARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>   /* size_t */

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  外设路径配置（GEC6818 板卡实测）                          ║
 *  ╚══════════════════════════════════════════════════════════════╝
 ***********************************************************************/

/* ── 4 路 LED（D7~D10，内核 LED 子系统，brightness: 1=亮 0=灭） ── */
#define AI_LED1_PATH      "/sys/class/leds/led1/brightness"  /* D7  — B26 */
#define AI_LED2_PATH      "/sys/class/leds/led2/brightness"  /* D8  — C11 */
#define AI_LED3_PATH      "/sys/class/leds/led3/brightness"  /* D9  — C7  */
#define AI_LED4_PATH      "/sys/class/leds/led4/brightness"  /* D10 — C12 */

/* ── 按键 K2（GPIO sysfs，外部上拉 — 0=按下 1=释放） ── */
#define AI_GPIO_BUTTON    28    /* K2  — A28 — GPIO 28 (输入)   */

/* ── 蜂鸣器（PWM2/GPIOC14 复用为 GPIO 输出，1=鸣响 0=静音） ── */
#define AI_GPIO_BUZZER    78    /* PWM2 — GPIOC14 — GPIO 78 (输出) */

/* ── MMA8653FCR1 三轴加速度计（内核 mma8653 驱动 → input 子系统） ── */
#define AI_ACCEL_INPUT    "/dev/input/event3"  /* 内核驱动暴露的 input 设备 */
/* GEC6818 板卡实测（±2g 量程, 10-bit 模式, 1g≈256 LSB）：
 *   I2C-2 地址 0x1D（被内核 mma8653 驱动占用 → i2cdetect 显示 UU）
 *   驱动注册为 input 设备：/dev/input/event3, name="mma8653"
 *   通过 EVIOCGABS ioctl 读取 ABS_X / ABS_Y / ABS_Z 获取加速度
 *   注意: ioctl 返回的 min=-32/max=31 是假象，忽略！
 *   板卡平放实测: X≈12(0.05g) Y≈3(0.01g) Z≈-261(-1.02g 重力) */

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  公开 API                                                    ║
 *  ╚══════════════════════════════════════════════════════════════╝
 ***********************************************************************/

/**
 * 执行单个硬件动作 (由 ai_tools.c 的 tool handler 调用)。
 * 返回 malloc 的中文结果字符串（调用方负责 free）。
 */
char* ai_execute_action(const char *action_name);

#ifdef __cplusplus
}
#endif

#endif /* AI_HARDWARE_H */
