// ========== edge_analysis.c ==========
// 纯数学实现: Pearson 相关 + 最小二乘线性回归。零依赖。
#include "edge_analysis.h"
#include <math.h>
#include <stddef.h>

/* Pearson 相关系数 r = cov(a,b) / (σa · σb)
 *  对齐数组 a[], b[] 同下标 (由引擎保证: 同一帧采样 6 路同步入环)。 */
float analysis_correlation(const float * a, const float * b, int n, bool * valid)
{
    if(valid) *valid = false;
    if(a == NULL || b == NULL || n < 2) return 0.0f;

    double sa = 0, sb = 0;
    for(int i = 0; i < n; i++) { sa += a[i]; sb += b[i]; }
    double ma = sa / n, mb = sb / n;

    double cov = 0, va = 0, vb = 0;
    for(int i = 0; i < n; i++) {
        double da = a[i] - ma;
        double db = b[i] - mb;
        cov += da * db;
        va  += da * da;
        vb  += db * db;
    }
    double denom = sqrt(va * vb);
    if(denom < 1e-12) return 0.0f;   /* 方差为 0 (常数序列) → 无相关性 */
    if(valid) *valid = true;
    return (float)(cov / denom);
}

/* 最小二乘: y = intercept + slope * t, t = 0..n-1
 *  slope     = Σ(t-t̄)(y-ȳ) / Σ(t-t̄)²
 *  intercept = ȳ - slope · t̄ */
void analysis_regression(const float * y, int n,
                         float * slope, float * intercept, bool * valid)
{
    if(valid) *valid = false;
    if(slope)     *slope = 0.0f;
    if(intercept) *intercept = 0.0f;
    if(y == NULL || n < 2) return;

    double mt = (n - 1) / 2.0;          /* t̄ = (0+1+...+n-1)/n = (n-1)/2 */
    double sy = 0;
    for(int i = 0; i < n; i++) sy += y[i];
    double my = sy / n;

    double num = 0, den = 0;
    for(int i = 0; i < n; i++) {
        double dt = i - mt;
        num += dt * (y[i] - my);
        den += dt * dt;
    }
    if(den < 1e-12) return;             /* 全在同一点 → 无法拟合 */
    double b = num / den;
    double a = my - b * mt;
    if(slope)     *slope     = (float)b;
    if(intercept) *intercept = (float)a;
    if(valid)     *valid     = true;
}

/* 外推预测: pred = intercept + slope * (n + steps_ahead) */
float analysis_predict(const regr_t * r, int n_points, int steps_ahead)
{
    if(r == NULL || !r->valid) return r ? r->cur : 0.0f;
    return r->intercept + r->slope * (float)(n_points + steps_ahead);
}
