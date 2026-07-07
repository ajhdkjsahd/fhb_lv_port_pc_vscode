// ========== edge_analysis.h ==========
// 纯数学: Pearson 相关 + 最小二乘线性回归 + 当日汇总。
// 零依赖(仅 libc), 可独立单测。由 edge_engine 周期调用。
#ifndef EDGE_ANALYSIS_H
#define EDGE_ANALYSIS_H
#include <stdbool.h>
#include "edge_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pearson 相关系数。n<2 或分母为 0 时返回 0 并置 *valid=false。 */
float analysis_correlation(const float * a, const float * b, int n, bool * valid);

/* 最小二乘线性回归 y = intercept + slope * t, t=0..n-1。
 *  out: slope, intercept, r²(可选)。n<2 时 valid=false。 */
void analysis_regression(const float * y, int n,
                         float * slope, float * intercept, bool * valid);

/* 由回归结果外推: pred = intercept + slope * (n + steps_ahead) */
float analysis_predict(const regr_t * r, int n_points, int steps_ahead);

#ifdef __cplusplus
}
#endif
#endif /* EDGE_ANALYSIS_H */
