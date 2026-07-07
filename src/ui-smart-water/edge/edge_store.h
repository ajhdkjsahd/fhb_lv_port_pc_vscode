// ========== edge_store.h ==========
// 存储层: CSV 平铺文件 + 分析缓存 JSON (用 cJSON)。
// 所有路径基于板载 eMMC (Linux: /root/aqua_data, PC: ./aqua_data)。
// 仅由 edge_engine 的 worker 线程调用写接口; 读接口可在 init 阶段单线程调用。
#ifndef EDGE_STORE_H
#define EDGE_STORE_H
#include <stdint.h>
#include <stdbool.h>
#include "edge_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 建数据目录 raw/ + analysis/ (幂等) */
void store_ensure_dirs(void);

/* 今日 CSV 路径 (静态缓冲, 仅 worker/init 调用, 非可重入)。
 *  文件名: raw/YYYY-MM-DD.csv */
const char * store_today_path(void);

/* 追加一帧原始数据 (含 flag)。每 EDGE_FSYNC_EVERY 条 fsync 一次, 掉电安全。 */
void store_append_raw(const raw_sample_t * s, int flag);

/* 启动时加载今日文件尾部, 重建快照+历史环。
 *  out[] 容量 max, 返回实际写入条数(按文件正序, 即最旧的在前)。
 *  只回放「有效」(flag==0) 的样本。无文件返回 0。 */
int store_load_tail_samples(raw_sample_t * out, int max);

/* 加载今日文件最近 max 条「有效」样本(用于分析回归/相关性)。
 *  返回条数。无文件返回 0。 */
int store_load_recent_valid(raw_sample_t * out, int max);

/* 扫描今日整个 CSV, 计算每路传感器当日 min/max/mean/有效数/剔除数。
 *  跨重启也正确 (直接读文件, 不依赖内存计数)。out[i].valid=false 表示无数据。 */
void store_compute_daily(daily_t out[SENSOR_IDX_COUNT]);

/* 分析缓存 JSON 读写 (analysis/latest.json) */
bool store_load_analysis(edge_analysis_t * out);
void store_save_analysis(const edge_analysis_t * in);

#ifdef __cplusplus
}
#endif
#endif /* EDGE_STORE_H */
