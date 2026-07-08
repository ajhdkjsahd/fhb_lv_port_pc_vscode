// ========== control_page.h ==========
// 智能闭环控制页 — PID + PWM 本地闭环。
//
// 布局 800×480 Ocean 主题:
//   顶栏: 返回 | 标题 | [闭环控制(自动)|手动控制] 段控 | 状态点
//   自动视图: DO闭环曲线 + 增氧机PWM条 | 被控量卡 + PID参数slider + 设备状态卡(点击弹窗)
//   手动视图: 实时反馈条 + 三设备操作面板 + 急停
//   弹窗: 循环水泵阈值 / 投喂电机定时
//
// 控制逻辑全部在 control/ 模块, 本页只通过 app_action_control_* 接口操作,
// 与 LVGL 无关的逻辑零耦合。lv_timer(500ms) 驱动 control_step + UI 刷新。
//
#ifndef CONTROL_PAGE_H
#define CONTROL_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

typedef void (*control_back_cb_t)(void);

lv_obj_t * control_page_create(control_back_cb_t back_cb);

#ifdef __cplusplus
}
#endif
#endif /* CONTROL_PAGE_H */
