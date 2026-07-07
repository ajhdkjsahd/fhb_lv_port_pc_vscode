// ========== sensor_range.h ==========
// 传感器共享定义：索引枚举 + 物理量程表。
// 抽取自 app_actions.h，供「边缘引擎」(edge/) 与 UI 层 (pages/) 共用，
// 避免在多处各写一份量程。引擎的异常过滤直接查 g_sensor_phys[]。
// 不依赖 lvgl，纯 C。
#ifndef SENSOR_RANGE_H
#define SENSOR_RANGE_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 6 路传感器索引：温度 / 湿度 / 光照 / 溶解氧 / pH / 氨氮 */
typedef enum {
    SENSOR_IDX_TEMP = 0,   /* 温度     °C    */
    SENSOR_IDX_HUMI,       /* 湿度     %     */
    SENSOR_IDX_LIGHT,      /* 光照     %     */
    SENSOR_IDX_DO,         /* 溶解氧   mg/L  */
    SENSOR_IDX_PH,         /* pH值     —     */
    SENSOR_IDX_NH3N,       /* 氨氮     mg/L  */
    SENSOR_IDX_COUNT
} sensor_idx_t;

/* 单路传感器的物理约束 + 显示小数位。
 *  - phys_min/phys_max: 物理量程，超出即判「超量程」误采样
 *  - max_delta:         单采样周期(≈3s)内最大允许突变，超出即判「速率异常」
 *  - dec:               显示小数位（与 sensor_page 保持一致） */
typedef struct {
    const char * name;       /* 中文短名 */
    const char * unit;       /* 单位 */
    float phys_min, phys_max;
    float max_delta;
    int   dec;
} sensor_phys_t;

/* 量程表（顺序与 sensor_idx_t 一致）。
 * 数值参考 sensor_page.c 的展示元数据 + mqtt_pub_demo.c 的模拟区间。 */
extern const sensor_phys_t g_sensor_phys[SENSOR_IDX_COUNT];

#ifdef __cplusplus
}
#endif
#endif /* SENSOR_RANGE_H */
