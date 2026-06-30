// ========== ai_hardware.c ==========
// AI 硬件控制模块 — 实现（GEC6818 板卡适配版 v2）
//
//   ╔══════════════════════════════════════════════════════════╗
//   ║              硬件配置总览（已经板卡实测验证）            ║
//   ╠══════════════════════════════════════════════════════════╣
//   ║  元件     丝印   芯片引脚   sysfs 路径         有效电平  ║
//   ║  ─────── ────── ───────── ────────────────── ─────────  ║
//   ║  LED-1   D7     B26       /sys/class/leds/led1  1=亮   ║
//   ║  LED-2   D8     C11       /sys/class/leds/led2  1=亮   ║
//   ║  LED-3   D9     C7        /sys/class/leds/led3  1=亮   ║
//   ║  LED-4   D10    C12       /sys/class/leds/led4  1=亮   ║
//   ║  按键    K2     A28       GPIO sysfs (28)       0=按下  ║
//   ║  蜂鸣器  —      GPIOC14   GPIO sysfs (78)       1=响    ║
//   ║  加速度  MMA8653 I2C-2,0x1D /dev/i2c-2         (待测)  ║
//   ╚══════════════════════════════════════════════════════════╝
//
//   为什么用 LED 子系统而不是裸 GPIO？
//     GEC6818 内核已有 nxp-gpio LED 驱动，接管了 LED 对应的 GPIO 引脚。
//     GPIO 58/75/71/76 已被内核标记为 "used"，用户空间直接操作 GPIO
//     value 会被内核拒绝。通过 /sys/class/leds/ 操作才能正常工作。
//     同时 LED 子系统自动处理极性（共阳极 0=亮），不用我们操心。
//
//   GPIO 编号公式（S5P6818, 已通过 gpiochip 验证）：
//     GPIOA=0  GPIOB=32  GPIOC=64  GPIOD=96  GPIOE=128  GPIOF=160
//
#include "ai_hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>      /* time() — 随机数种子 */

/* bool 兼容：ARM 交叉编译器可能没有 stdbool.h，直接定义 */
#ifndef __cplusplus
  #ifndef bool
    #define bool int
    #define true  1
    #define false 0
  #endif
#endif

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
/* I2C 用户态驱动头文件（MMA8653 需要） */
#ifdef __has_include
  #if __has_include(<linux/i2c-dev.h>)
    #include <linux/i2c-dev.h>
    #define HAS_I2C_DEV 1
  #endif
#else
  #include <linux/i2c-dev.h>
  #define HAS_I2C_DEV 1
#endif
#endif

/* 日志宏 */
#ifdef LV_LOG_USER
  /* 已包含 lvgl.h */
#else
  #define LV_LOG_USER(fmt, ...) printf("[AI-HW] " fmt "\n", ##__VA_ARGS__)
#endif

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  辅助函数 — 文件读写                                        ║
 *  ╚══════════════════════════════════════════════════════════════╝
 ***********************************************************************/

#ifdef __linux__

/**
 * 向 sysfs 文件写入一个字符 ('0' 或 '1')。
 * 用于操作 /sys/class/leds/ledX/brightness 和 GPIO value。
 *
 * 返回 true 表示写入成功，false 表示失败。
 */
static bool sysfs_write_char(const char *path, char val)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        LV_LOG_USER("  sysfs_write: fopen(%s) 失败", path);
        return false;
    }
    int ret = fputc(val, f);
    fclose(f);
    if (ret == EOF) {
        LV_LOG_USER("  sysfs_write: fputc(%s, '%c') 失败", path, val);
        return false;
    }
    return true;
}

/**
 * 从 sysfs 文件读取一个字符。
 * 用于读取按键 GPIO value。
 *
 * 返回读到的字符，失败返回 -1。
 */
static int sysfs_read_char(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val = fgetc(f);
    fclose(f);
    return val;
}

/* ── GPIO 辅助（仅用于按键和蜂鸣器，LED 不用 GPIO） ── */

static bool gpio_is_exported(int pin)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    return (access(path, F_OK) == 0);
}

static void gpio_ensure(int pin, const char *dir)
{
    if (!gpio_is_exported(pin)) {
        /* 首次使用：导出 + 设方向 */
        char cmd[128];
        snprintf(cmd, sizeof(cmd),
                 "echo %d > /sys/class/gpio/export && "
                 "echo %s > /sys/class/gpio/gpio%d/direction",
                 pin, dir, pin);
        system(cmd);
        LV_LOG_USER("  GPIO%d 首次导出，方向=%s", pin, dir);
    } else {
        /* 已导出但方向可能不对 → 重设方向 */
        char cmd[128];
        snprintf(cmd, sizeof(cmd),
                 "echo %s > /sys/class/gpio/gpio%d/direction",
                 dir, pin);
        system(cmd);
    }
}

static bool gpio_write(int pin, char val)
{
    gpio_ensure(pin, "out");
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    return sysfs_write_char(path, val);
}

static int gpio_read(int pin)
{
    gpio_ensure(pin, "in");
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    return sysfs_read_char(path);
}

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  MMA8653FCR1 三轴加速度计（input 子系统）                   ║
 *  ╚══════════════════════════════════════════════════════════════╝
 *
 *  芯片：NXP MMA8653FCR1
 *  总线：I2C-2 (MCU_SCL_2 / MCU_SDA_2), 地址 0x1D
 *  内核驱动：mma8653 → 注册为 input 设备 → /dev/input/event3
 *
 *  GEC6818 板卡实测：
 *    $ cat /proc/bus/input/devices | grep -A5 mma
 *    N: Name="mma8653"
 *    H: Handlers=event3                    ← 就是这个！
 *
 *    $ cat /sys/bus/i2c/devices/2-001d/name
 *    mma8653                               ← 内核驱动名
 *
 *  为什么不直接用裸 I2C？
 *    内核 mma8653 驱动已绑定 I2C 地址 0x1D（i2cdetect 显示 UU），
 *    用户空间无法再通过 /dev/i2c-2 直接访问该地址。
 *    但内核驱动把数据暴露为 input 设备，用 EVIOCGABS ioctl
 *    即可读取当前 X/Y/Z 轴的加速度值，无需轮询事件。
 *
 *  读取方式：
 *    1. open("/dev/input/event3", O_RDONLY)
 *    2. ioctl(fd, EVIOCGABS(ABS_X), &abs)  → abs.value = X 轴
 *    3. ioctl(fd, EVIOCGABS(ABS_Y), &abs)  → abs.value = Y 轴
 *    4. ioctl(fd, EVIOCGABS(ABS_Z), &abs)  → abs.value = Z 轴
 *    5. close(fd)
 *
 *  数值含义（GEC6818 板卡实测验证）：
 *    ─── 重要：ioctl 返回的 min=-32 / max=31 是假象，忽略！ ───
 *
 *    MMA8653 配置为 ±2g 量程，10-bit 模式：
 *      原始ADC范围 ≈ [-512, 511]（10-bit 有符号）
 *      换算公式:  g值 = 原始值 / 256.0
 *
 *    板卡平放实测（Z轴朝下，承受重力）：
 *       X = 10~14  →  ~0.05g   （基本水平，轻微右倾）
 *       Y = 1~4    →  ~0.01g   （基本水平）
 *       Z = -258~-265 → ~-1.02g （地球重力！负号=指向地心）
 *
 *    倾斜板子验证：
 *      Z轴=重力轴，平放时 Z≈-261(1g)，竖起来 Z≈0
 *      X/Y轴=倾斜角，倾斜越大偏移越多
 *
 *    读取协议验证（shell）：
 *      dd if=/dev/input/event3 bs=24 count=20 | od -A x -t d4
 *      → type=3 code=0 → X轴, code=1 → Y轴, code=2 → Z轴
 *      → 每个ABS事件后跟一个SYN(type=0 code=0)分隔符
 ***********************************************************************/

#ifdef __linux__
#include <linux/input.h>   /* struct input_absinfo, EVIOCGABS, ABS_X/Y/Z */

/**
 * 通过 Linux input 子系统读取 MMA8653 三轴加速度。
 *
 * 内核驱动 mma8653 已将芯片注册为 /dev/input/event3，
 * 方法 A：用 EVIOCGABS ioctl 直接查询各轴的当前值。
 * 方法 B：如果 ioctl 不可用，尝试读原始 input_event 数据。
 *
 * 数据解读（±2g 量程, 10-bit 模式, 1g≈256 LSB）：
 *   X/Y  → 倾斜角（0=水平）
 *   Z    → 重力轴（平放≈-261 ≈ -1g, 竖立≈0）
 *   X/Y > 150 → 板子倾斜超过 30°
 *
 * 返回 malloc 的紧凑格式字符串（供 AI 上下文使用）。
 */
static char* accel_read(void)
{
    char out[512];
    int fd;

    /* ── 打开 input 设备 ── */
    fd = open("/dev/input/event3", O_RDONLY);
    if (fd < 0) {
        /* 备选：扫描其他 event 设备找 "mma8653" */
        for (int i = 0; i < 8; i++) {
            char devpath[32];
            snprintf(devpath, sizeof(devpath), "/dev/input/event%d", i);
            fd = open(devpath, O_RDONLY);
            if (fd >= 0) {
                char name[64] = "";
                ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                if (strstr(name, "mma8653") || strstr(name, "mma")) {
                    LV_LOG_USER("  找到 MMA8653: %s → %s", devpath, name);
                    break;
                }
                close(fd);
                fd = -1;
            }
        }
        if (fd < 0)
            return strdup("[加速度-MMA8653] 错误: 找不到 input 设备 "
                          "(试过 /dev/input/event0~7)");
    }

    /* ── 方法 A: EVIOCGABS ioctl ── */
    struct input_absinfo ax = {0}, ay = {0}, az = {0};
    int rx = ioctl(fd, EVIOCGABS(ABS_X), &ax);
    int ry = ioctl(fd, EVIOCGABS(ABS_Y), &ay);
    int rz = ioctl(fd, EVIOCGABS(ABS_Z), &az);

    LV_LOG_USER("  MMA8653 ioctl: ABS_X=%d(%d) ABS_Y=%d(%d) ABS_Z=%d(%d)",
                rx, rx >= 0 ? ax.value : -1,
                ry, ry >= 0 ? ay.value : -1,
                rz, rz >= 0 ? az.value : -1);

    if (rx >= 0 || ry >= 0 || rz >= 0) {
        /* 详细调试信息 → 串口日志（含g值换算，不进入 AI 上下文） */
        LV_LOG_USER("MMA8653 详细数据 (ioctl min/max 忽略, 实际 ±2g 量程 10-bit):");
        if (rx >= 0) LV_LOG_USER("  X: raw=%-6d ~%.2fg  min=%d max=%d fuzz=%d flat=%d res=%d",
                                 ax.value, ax.value / 256.0,
                                 ax.minimum, ax.maximum, ax.fuzz, ax.flat, ax.resolution);
        if (ry >= 0) LV_LOG_USER("  Y: raw=%-6d ~%.2fg  min=%d max=%d fuzz=%d flat=%d res=%d",
                                 ay.value, ay.value / 256.0,
                                 ay.minimum, ay.maximum, ay.fuzz, ay.flat, ay.resolution);
        if (rz >= 0) LV_LOG_USER("  Z: raw=%-6d ~%.2fg  min=%d max=%d fuzz=%d flat=%d res=%d",
                                 az.value, az.value / 256.0,
                                 az.minimum, az.maximum, az.fuzz, az.flat, az.resolution);
        /* 紧凑格式返回给 AI（原始值 + 物理含义） */
        snprintf(out, sizeof(out),
                 "[加速度] X=%d(~%.2fg) Y=%d(~%.2fg) Z=%d(~%.2fg) — 10-bit原始值 ±2g量程",
                 rx >= 0 ? ax.value : 0,
                 rx >= 0 ? ax.value / 256.0 : 0.0,
                 ry >= 0 ? ay.value : 0,
                 ry >= 0 ? ay.value / 256.0 : 0.0,
                 rz >= 0 ? az.value : 0,
                 rz >= 0 ? az.value / 256.0 : 0.0);
        close(fd);
        return strdup(out);
    }

    /* ── 方法 B: ioctl 全部失败 → 试读 raw 事件 ── */
    LV_LOG_USER("  MMA8653: ioctl 全部失败，尝试读 raw 事件...");

    /* 设为非阻塞，读几帧看看有什么 */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct input_event ev;
    int ev_count = 0;
    int ev_abs_x = 0, ev_abs_y = 0, ev_abs_z = 0;
    int ev_abs_n = 0;

    for (int i = 0; i < 50; i++) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n <= 0) break;
        ev_count++;
        if (ev.type == EV_ABS) {
            ev_abs_n++;
            if (ev.code == ABS_X) ev_abs_x = ev.value;
            if (ev.code == ABS_Y) ev_abs_y = ev.value;
            if (ev.code == ABS_Z) ev_abs_z = ev.value;
        }
        LV_LOG_USER("  MMA8653 ev[%d]: type=%hu code=%hu value=%d",
                    i, ev.type, ev.code, ev.value);
    }
    close(fd);

    if (ev_abs_n > 0) {
        LV_LOG_USER("MMA8653 raw: events=%d abs=%d X=%d(~%.2fg) Y=%d(~%.2fg) Z=%d(~%.2fg)",
                    ev_count, ev_abs_n,
                    ev_abs_x, ev_abs_x / 256.0,
                    ev_abs_y, ev_abs_y / 256.0,
                    ev_abs_z, ev_abs_z / 256.0);
        snprintf(out, sizeof(out),
                 "[加速度] X=%d(~%.2fg) Y=%d(~%.2fg) Z=%d(~%.2fg) — raw事件",
                 ev_abs_x, ev_abs_x / 256.0,
                 ev_abs_y, ev_abs_y / 256.0,
                 ev_abs_z, ev_abs_z / 256.0);
        return strdup(out);
    }

    return strdup("[加速度] 错误: 传感器未响应 (检查 dmesg | grep mma)");
}

#endif /* __linux__ */

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  公开 API — ai_execute_action()                             ║
 *  ╚══════════════════════════════════════════════════════════════╝
 ***********************************************************************/

char* ai_execute_action(const char *action_name)
{
    if (!action_name) return NULL;

    LV_LOG_USER("执行硬件动作: '%s'", action_name);

    /* ── 命令别名：AI 可能把 led1_on 简写成 led1 ── */
    if (strcmp(action_name, "led1") == 0)       action_name = "led1_on";
    else if (strcmp(action_name, "led2") == 0)  action_name = "led2_on";
    else if (strcmp(action_name, "led3") == 0)  action_name = "led3_on";
    else if (strcmp(action_name, "led4") == 0)  action_name = "led4_on";
    else if (strcmp(action_name, "led_all") == 0) action_name = "led_all_on";
    else if (strcmp(action_name, "buzzer") == 0)  action_name = "buzzer_on";
    else if (strcmp(action_name, "led_random") == 0) action_name = "led_random_on";
    else if (strcmp(action_name, "ledX_on") == 0
          || strcmp(action_name, "ledx_on") == 0)   action_name = "led_random_on";

    /* ═══════════════════════════════════════════════════════════
     *  4 路 LED 独立控制（通过 /sys/class/leds/ 子系统）
     *
     *  LED 子系统 brightness: 1=ON(最大亮度), 0=OFF
     *  内核驱动负责极性转换，用户空间只写 1/0 即可。
     *  实测: echo 1 > brightness → 亮, echo 0 > brightness → 灭
     * ═══════════════════════════════════════════════════════════ */

    if (strcmp(action_name, "led1_on") == 0) {
        return sysfs_write_char(AI_LED1_PATH, '1')  /* LED子系统 1=ON */
            ? strdup("[LED-D7] 已打开")
            : strdup("[LED-D7] 操作失败 — 检查 /sys/class/leds/led1/");
    }
    if (strcmp(action_name, "led1_off") == 0) {
        return sysfs_write_char(AI_LED1_PATH, '0')  /* 0=OFF */
            ? strdup("[LED-D7] 已关闭")
            : strdup("[LED-D7] 操作失败");
    }

    if (strcmp(action_name, "led2_on") == 0) {
        return sysfs_write_char(AI_LED2_PATH, '1')  /* LED子系统 1=ON */
            ? strdup("[LED-D8] 已打开")
            : strdup("[LED-D8] 操作失败 — 检查 /sys/class/leds/led2/");
    }
    if (strcmp(action_name, "led2_off") == 0) {
        return sysfs_write_char(AI_LED2_PATH, '0')  /* 0=OFF */
            ? strdup("[LED-D8] 已关闭")
            : strdup("[LED-D8] 操作失败");
    }

    if (strcmp(action_name, "led3_on") == 0) {
        return sysfs_write_char(AI_LED3_PATH, '1')  /* LED子系统 1=ON */
            ? strdup("[LED-D9] 已打开")
            : strdup("[LED-D9] 操作失败 — 检查 /sys/class/leds/led3/");
    }
    if (strcmp(action_name, "led3_off") == 0) {
        return sysfs_write_char(AI_LED3_PATH, '0')  /* 0=OFF */
            ? strdup("[LED-D9] 已关闭")
            : strdup("[LED-D9] 操作失败");
    }

    if (strcmp(action_name, "led4_on") == 0) {
        return sysfs_write_char(AI_LED4_PATH, '1')  /* LED子系统 1=ON */
            ? strdup("[LED-D10] 已打开")
            : strdup("[LED-D10] 操作失败 — 检查 /sys/class/leds/led4/");
    }
    if (strcmp(action_name, "led4_off") == 0) {
        return sysfs_write_char(AI_LED4_PATH, '0')  /* 0=OFF */
            ? strdup("[LED-D10] 已关闭")
            : strdup("[LED-D10] 操作失败");
    }

    /* ── LED 全控 ── */
    if (strcmp(action_name, "led_all_on") == 0) {
        bool ok = true;
        ok &= sysfs_write_char(AI_LED1_PATH, '1');
        ok &= sysfs_write_char(AI_LED2_PATH, '1');
        ok &= sysfs_write_char(AI_LED3_PATH, '1');
        ok &= sysfs_write_char(AI_LED4_PATH, '1');
        return ok ? strdup("[LED] 全部已打开 (D7~D10)")
                  : strdup("[LED] 部分打开失败");
    }
    if (strcmp(action_name, "led_all_off") == 0) {
        bool ok = true;
        ok &= sysfs_write_char(AI_LED1_PATH, '0');
        ok &= sysfs_write_char(AI_LED2_PATH, '0');
        ok &= sysfs_write_char(AI_LED3_PATH, '0');
        ok &= sysfs_write_char(AI_LED4_PATH, '0');
        return ok ? strdup("[LED] 全部已关闭 (D7~D10)")
                  : strdup("[LED] 部分关闭失败");
    }

    /* ── 智能随机开灯：先读当前状态，只从灭的里面选 ── */
    if (strcmp(action_name, "led_random_on") == 0) {
        static int seeded = 0;
        if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }

        const char *paths[] = { AI_LED1_PATH, AI_LED2_PATH,
                                AI_LED3_PATH, AI_LED4_PATH };
        const char *names[] = { "D7", "D8", "D9", "D10" };

        /* 扫描所有 LED 当前状态 */
        int off_idx[4], off_n = 0, on_n = 0;
        char on_list[64] = "";
        for (int i = 0; i < 4; i++) {
            FILE *f = fopen(paths[i], "r");
            int val = f ? fgetc(f) : -1;
            if (f) fclose(f);
            if (val == '0') {
                off_idx[off_n++] = i;       /* 灭的候选 */
            } else {
                if (on_n > 0) strcat(on_list, " ");
                strcat(on_list, names[i]);
                on_n++;
            }
        }
        LV_LOG_USER("  扫描结果: 亮=%d 盏(%s)  灭=%d 盏",
                    on_n, on_list[0] ? on_list : "无", off_n);

        /* 全亮 → 全部熄灭，不再随机开 */
        if (off_n == 0) {
            LV_LOG_USER("  全部亮着，全灭");
            for (int i = 0; i < 4; i++)
                sysfs_write_char(paths[i], '0');
            return strdup("[LED] 检测到全部亮着，已全部熄灭");
        }

        /* 从灭的中随机选一个点亮 */
        int pick = off_idx[rand() % off_n];
        LV_LOG_USER("  随机选中 LED-%s (灭的 %d 盏中选)", names[pick], off_n);
        sysfs_write_char(paths[pick], '1');

        char buf[128];
        snprintf(buf, sizeof(buf),
                 "[LED] %s亮着, 从 %d 盏灭的中随机打开 %s",
                 on_n > 0 ? on_list : "无",
                 off_n, names[pick]);
        return strdup(buf);
    }

    /* ═══════════════════════════════════════════════════════════
     *  蜂鸣器 — GPIO 78 (PWM2/GPIOC14), 高电平鸣响
     *
     *  状态跟踪必须在软件层面完成：gpio_read() 会先把 GPIO 方向
     *  切为输入，导致输出驱动关闭、读回值不可靠 → toggle 永远不反转。
     *  用 static 变量跟踪最后一次写入的状态，避免方向切换。
     * ═══════════════════════════════════════════════════════════ */
    static char buzzer_state = '0';  /* 软件状态：'1'=鸣响 '0'=静音 */

    if (strcmp(action_name, "buzzer_on") == 0) {
        buzzer_state = '1';
        return gpio_write(AI_GPIO_BUZZER, '1')
            ? strdup("[蜂鸣器] 已鸣响")
            : strdup("[蜂鸣器] GPIO 78 操作失败");
    }
    if (strcmp(action_name, "buzzer_off") == 0) {
        buzzer_state = '0';
        return gpio_write(AI_GPIO_BUZZER, '0')
            ? strdup("[蜂鸣器] 已静音")
            : strdup("[蜂鸣器] GPIO 78 操作失败");
    }
    /* ── 蜂鸣器反转（软件状态翻转，不读 GPIO 方向以免破坏输出） ── */
    if (strcmp(action_name, "buzzer_toggle") == 0) {
        char set = (buzzer_state == '1') ? '0' : '1';
        LV_LOG_USER("  蜂鸣器 toggle: 软件状态 '%c' → 写入 '%c'",
                    buzzer_state, set);
        bool ok = gpio_write(AI_GPIO_BUZZER, set);
        LV_LOG_USER("  写入 '%c' → %s", set, ok ? "成功" : "失败!");
        if (ok) {
            buzzer_state = set;
            return (set == '1')
                ? strdup("[蜂鸣器] 已切换为鸣响")
                : strdup("[蜂鸣器] 已切换为静音");
        }
        return strdup("[蜂鸣器] GPIO 78 操作失败");
    }

    /* ═══════════════════════════════════════════════════════════
     *  按键 K2 — GPIO 28 (A28), 外部上拉 → 0=按下, 1=释放
     * ═══════════════════════════════════════════════════════════ */

    if (strcmp(action_name, "read_button") == 0) {
        int val = gpio_read(AI_GPIO_BUTTON);
        if (val < 0) return strdup("[按键-K2] GPIO 28 读取失败");
        return strdup((val == '0') ? "[按键-K2] 已按下" : "[按键-K2] 未按下");
    }

    /* ═══════════════════════════════════════════════════════════
     *  MMA8653FCR1 三轴加速度计 — I2C-2, 0x1D
     * ═══════════════════════════════════════════════════════════ */

    if (strcmp(action_name, "read_accel") == 0) {
        return accel_read();
    }

    /* ═══════════════════════════════════════════════════════════
     *  未知命令
     * ═══════════════════════════════════════════════════════════ */

    LV_LOG_USER("未知硬件命令: '%s'", action_name);
    char err[128];
    snprintf(err, sizeof(err),
             "[错误] 未知硬件命令: %s（可用: led1_on, led1_off, led2_on, ..."
             "led_random_on, buzzer_on/off, buzzer_toggle, read_button, read_accel）",
             action_name);
    return strdup(err);
}

#else /* ═══════════════════════════════════════════════════════════ */
      /*  PC / Windows 模拟实现                                      */
      /* ═══════════════════════════════════════════════════════════ */

char* ai_execute_action(const char *action_name)
{
    if (!action_name) return NULL;

    LV_LOG_USER("PC模拟硬件动作: '%s'", action_name);

    if (strcmp(action_name, "led1_on") == 0)      return strdup("[LED-D7] 已打开 (PC模拟)");
    if (strcmp(action_name, "led1_off") == 0)     return strdup("[LED-D7] 已关闭 (PC模拟)");
    if (strcmp(action_name, "led2_on") == 0)      return strdup("[LED-D8] 已打开 (PC模拟)");
    if (strcmp(action_name, "led2_off") == 0)     return strdup("[LED-D8] 已关闭 (PC模拟)");
    if (strcmp(action_name, "led3_on") == 0)      return strdup("[LED-D9] 已打开 (PC模拟)");
    if (strcmp(action_name, "led3_off") == 0)     return strdup("[LED-D9] 已关闭 (PC模拟)");
    if (strcmp(action_name, "led4_on") == 0)      return strdup("[LED-D10] 已打开 (PC模拟)");
    if (strcmp(action_name, "led4_off") == 0)     return strdup("[LED-D10] 已关闭 (PC模拟)");
    if (strcmp(action_name, "led_all_on") == 0)   return strdup("[LED] 全部已打开 D7~D10 (PC模拟)");
    if (strcmp(action_name, "led_all_off") == 0)  return strdup("[LED] 全部已关闭 D7~D10 (PC模拟)");
    if (strcmp(action_name, "led_random_on") == 0) return strdup("[LED] 检测: 无亮着, 已从 4 盏灭的中随机打开 D8 (PC模拟)");
    if (strcmp(action_name, "buzzer_on") == 0)     return strdup("[蜂鸣器] 已鸣响 (PC模拟)");
    if (strcmp(action_name, "buzzer_off") == 0)    return strdup("[蜂鸣器] 已静音 (PC模拟)");
    if (strcmp(action_name, "buzzer_toggle") == 0) return strdup("[蜂鸣器] 已切换 (PC模拟)");
    if (strcmp(action_name, "read_button") == 0)  return strdup("[按键-K2] 未按下 (PC模拟)");
    if (strcmp(action_name, "read_accel") == 0)   return strdup("[加速度] X=12(~0.05g) Y=3(~0.01g) Z=-261(~-1.02g) — 10-bit原始值 ±2g量程 (PC模拟)");

    char err[128];
    snprintf(err, sizeof(err), "[错误] 未知硬件命令: %s", action_name);
    return strdup(err);
}

#endif /* __linux__ */

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  ai_split_actions() — [ACTION] 命令解析器（v2，非XML语法） ║
 *  ╚══════════════════════════════════════════════════════════════╝
 *
 *  格式： [ACTION]命令名
 *  示例： [ACTION]led_random_on
 *
 *  命令从 "[ACTION]" 之后开始，到行尾 (\n) 或字符串尾结束。
 *  整行（包括前面的空白）被移除。
 *  这样可以彻底避免 DeepSeek-R1 混淆 XML 标签。
 ***********************************************************************/

#define ACTION_MARKER "[ACTION]"
#define ACTION_MARKER_LEN 8

char* ai_split_actions(char *text)
{
    if (!text) return NULL;

    LV_LOG_USER("%s", "开始解析 [ACTION] 命令...");

    size_t rcap = 256, rlen = 0;
    char *result = malloc(rcap);
    if (!result) return NULL;
    result[0] = '\0';

    char *src = text;
    char *dst = text;
    int action_count = 0;
    bool hit_break = false;

    while (*src && action_count < 10) {
        char *marker = strstr(src, ACTION_MARKER);
        if (!marker) {
            size_t rest = strlen(src);
            if (dst != src) memmove(dst, src, rest + 1);
            LV_LOG_USER("%s", "  [ACTION] 解析完毕");
            hit_break = true;
            break;
        }

        LV_LOG_USER("  发现 [ACTION]，偏移=%d", (int)(marker - text));

        /* 复制 [ACTION] 之前的文本 */
        if (marker > dst) {
            size_t prefix = (size_t)(marker - src);
            if (dst != src) memmove(dst, src, prefix);
            dst += prefix;
        }
        src = marker + ACTION_MARKER_LEN;  /* 跳过 "[ACTION]" */

        /* 提取命令名：到行尾或字符串尾 */
        char *cmd_end = src;
        while (*cmd_end && *cmd_end != '\n' && *cmd_end != '\r')
            cmd_end++;
        size_t namelen = (size_t)(cmd_end - src);

        /* 跳过行尾换行符 */
        char *line_end = cmd_end;
        while (*line_end == '\n' || *line_end == '\r')
            line_end++;

        char action_name[64];
        size_t copy_len = namelen < sizeof(action_name) - 1
                          ? namelen : sizeof(action_name) - 1;
        memcpy(action_name, src, copy_len);
        action_name[copy_len] = '\0';

        /* 去掉命令名首尾空白，只取 [a-zA-Z0-9_] 组成的 token */
        char *an = action_name;
        while (*an == ' ' || *an == '\t') an++;
        char *ae = an;
        while (*ae && ((*ae >= 'a' && *ae <= 'z')
                    || (*ae >= 'A' && *ae <= 'Z')
                    || (*ae >= '0' && *ae <= '9')
                    || *ae == '_'))
            ae++;
        *ae = '\0';

        LV_LOG_USER("  命令: '%s'", an);

        /* 执行硬件动作 — 只保留成功结果，忽略错误（AI 引用语法不算） */
        if (an[0]) {
            char *act_result = ai_execute_action(an);
            if (act_result) {
                LV_LOG_USER("  结果: %s", act_result);
                /* 过滤：不以 [错误] 开头的结果才保存 */
                if (strncmp(act_result, "[错误]", 7) != 0) {
                    size_t alen = strlen(act_result);
                    if (rlen + alen + 3 > rcap) {
                        rcap = rlen + alen + 256;
                        char *tmp = realloc(result, rcap);
                        if (!tmp) { free(act_result); free(result); return NULL; }
                        result = tmp;
                    }
                    if (rlen > 0) result[rlen++] = '\n';
                    memcpy(result + rlen, act_result, alen);
                    rlen += alen;
                    result[rlen] = '\0';
                    action_count++;
                }
                free(act_result);
            }
        }

        src = line_end;  /* 跳过整行（命令+换行符） */
    }

    if (!hit_break && dst != text)
        *dst = '\0';

    LV_LOG_USER("[ACTION] 解析完成：%d 个动作，结果 %zu 字符",
                action_count, rlen);

    if (rlen == 0) { free(result); return NULL; }
    return result;
}

/**
 * ai_strip_actions — 仅移除 [ACTION] 行，不执行硬件。
 * 用于清理对话历史中的命令标记，避免历史存储时重复执行。
 */
void ai_strip_actions(char *text)
{
    if (!text) return;

    char *src = text;
    char *dst = text;
    bool hit_break = false;
    int count = 0;

    while (*src && count < 10) {
        char *marker = strstr(src, ACTION_MARKER);
        if (!marker) {
            size_t rest = strlen(src);
            if (dst != src) memmove(dst, src, rest + 1);
            hit_break = true;
            break;
        }

        if (marker > dst) {
            size_t prefix = (size_t)(marker - src);
            if (dst != src) memmove(dst, src, prefix);
            dst += prefix;
        }
        src = marker + ACTION_MARKER_LEN;

        /* 跳过到行尾 */
        while (*src && *src != '\n' && *src != '\r') src++;
        while (*src == '\n' || *src == '\r') src++;
        count++;
    }

    if (!hit_break && dst != text)
        *dst = '\0';
}
