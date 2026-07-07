// ========== trend_page.h ==========
// 养殖环境趋势报表页: 历史曲线(lv_chart) + 线性回归预测 + 变量相关性 + 当日报表。
// 数据全部来自边缘引擎(edge_engine), 不直接碰 MQTT/文件。
#ifndef TREND_PAGE_H
#define TREND_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/** Callback: called when the "返回首页" button is clicked */
typedef void (*trend_back_cb_t)(void);

/** Create the trend-report page screen.
 *  back_cb : called when user clicks the back button (navigate to home). */
lv_obj_t * trend_page_create(trend_back_cb_t back_cb);

#ifdef __cplusplus
}
#endif
#endif /* TREND_PAGE_H */
