// ========== edge_engine.h ==========
// 边缘端数据预处理引擎 — 公共接口。
//
// 数据流:
//   MQTT 回调线程 → edge_engine_push()  非阻塞入队
//   worker 线程   → 去重 / 异常过滤 / 写 CSV(eMMC) / 更新快照+历史环 / 周期分析
//   LVGL 主线程   → get_latest / get_history / get_analysis 读取(短锁拷出)
//
// 线程模型:
//   - Linux/ARM: 队列 + condvar + 独立 worker pthread
//   - PC/Windows: 单线程桩, push() 直接同步处理(无队列), 便于演示
// 路径:
//   - Linux: /root/aqua_data/{raw,analysis}
//   - PC  : ./aqua_data/{raw,analysis}
#ifndef EDGE_ENGINE_H
#define EDGE_ENGINE_H
#include <stdint.h>
#include <stdbool.h>
#include "sensor_range.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 一帧原始采样 (MQTT 收到时由 app_mqtt.c 填充) ===== */
typedef struct {
    uint32_t ts;                          /* time(NULL) 到达时间戳 */
    int32_t  seq;                         /* 发布端序号(去重用)；-1 表示无 */
    float    values[SENSOR_IDX_COUNT];    /* 6 路 */
} raw_sample_t;

/* 数据有效标志 (CSV flags 列) */
enum {
    EDGE_FLAG_VALID = 0,   /* 有效 → 进历史环 + 分析 */
    EDGE_FLAG_RANGE = 1,   /* 超物理量程 */
    EDGE_FLAG_SPIKE = 2,   /* 速率突变(误采样) */
    EDGE_FLAG_DUPE  = 3,   /* 重复(seq 相同或 6 值全同) */
};

/* ===== 引擎维护的快照与历史 =====
 *  history 环固定容量 EDGE_RING_CAP (≈24min @3s/条), 滚动覆盖。 */
#define EDGE_RING_CAP 480

/* ===== 分析结果 (worker 周期产出, LVGL 只读) ===== */
#define EDGE_CORR_MAX 15   /* C(6,2) = 15 对 */

typedef struct {
    int8_t a, b;   /* sensor_idx_t 配对 */
    float  r;      /* Pearson 相关系数 [-1,1] */
} corr_pair_t;

typedef struct {
    float slope;       /* 单位: value/采样点 */
    float intercept;
    float cur;         /* 当前值 */
    float pred_1h;     /* 外推 1h 预测值 */
    bool  valid;
} regr_t;

typedef struct {
    float min, max, mean;
    int   count;       /* 有效样本数 */
    int   reject;      /* 被剔除数(flag≠0) */
    bool  valid;
} daily_t;

typedef struct {
    uint32_t     ts;                            /* 生成时间 */
    regr_t       regr[SENSOR_IDX_COUNT];        /* 每路回归 + 1h 预测 */
    corr_pair_t  corr[EDGE_CORR_MAX];           /* 相关性对(按 |r| 降序) */
    int          corr_n;
    daily_t      daily[SENSOR_IDX_COUNT];       /* 当日每路汇总 */
    bool         valid;
} edge_analysis_t;

/* ===== 生命周期 =====
 *  init: 建目录 → 启动 worker(Linux) → 从今日 CSV 重建快照+历史 → 加载分析缓存。
 *        即便 MQTT 还没连上, 卡片/趋势页启动即有「上次已知值」。 */
bool edge_engine_init(void);
void edge_engine_deinit(void);

/* ===== 生产者 (MQTT 回调线程 / PC sim) =====
 *  非阻塞: Linux 入队即返回; PC 直接同步处理。NULL 安全忽略。 */
void edge_engine_push(const raw_sample_t * s);

/* ===== 消费者 (LVGL 主线程) =====
 *  get_latest: 返回最近一次「有效」值；无数据返回 false。
 *  get_history: 拷出历史环(按时间正序), 返回写入条数(0..max)。
 *  get_analysis: 拷出分析缓存; 无缓存返回 false。 */
bool edge_engine_get_latest(sensor_idx_t idx, float * value, uint32_t * ts);
int  edge_engine_get_history(sensor_idx_t idx, float * buf, int max);
bool edge_engine_get_analysis(edge_analysis_t * out);

/* ===== PC 模拟喂料 (无 MQTT 时由 ui.c 定时调用) =====
 *  复刻 mqtt_pub_demo.c 的随机生成逻辑, 便于 Windows 上演示趋势页。 */
void edge_engine_sim_feed(void);

#ifdef __cplusplus
}
#endif
#endif /* EDGE_ENGINE_H */
