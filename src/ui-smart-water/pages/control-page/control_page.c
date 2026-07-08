// ========== control_page.c ==========
// 智能闭环控制页 — 实现。
// 控制逻辑全部在 control/ 模块, 本页只通过 app_action_control_* 接口操作 + 刷新显示,
// 与 LVGL 无关的逻辑零耦合。lv_timer(500ms) 仅刷新 UI; control_step 由 ui.c 全局定时器驱动,
// 这样切到别的页面闭环仍在跑。
//
#include "control_page.h"
#include "../app_fonts.h"
#include "../app_actions.h"
#include "lvgl/core/lv_obj_event.h"     /* lv_event_get_draw_task */
#include "lvgl/draw/lv_draw.h"           /* lv_draw_task_t / get_type / get_draw_dsc */
#include "lvgl/draw/lv_draw_line.h"      /* lv_draw_line_dsc_t / lv_draw_task_get_line_dsc */
#include "lvgl/draw/lv_draw_triangle.h"  /* lv_draw_triangle_dsc_init / lv_draw_triangle */
#include "lvgl/draw/lv_draw_rect.h"      /* lv_draw_rect_dsc_init / lv_draw_rect */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#ifndef NO_SCROLL
#define NO_SCROLL(obj) \
    lv_obj_set_scrollbar_mode((obj), LV_SCROLLBAR_MODE_OFF); \
    lv_obj_clear_flag((obj), LV_OBJ_FLAG_SCROLLABLE)
#endif

#define COLOR_BG       0x060E14
#define COLOR_CARD     0x0A1620
#define COLOR_BORDER   0x1C2E36
#define COLOR_ACCENT   0x00D4AA
#define COLOR_ACCENT2  0x0288D1
#define COLOR_WARN     0xFFB800
#define COLOR_ALARM    0xFF6B6B
#define COLOR_TEXT     0xE6F2EE
#define COLOR_TEXT2    0x9AB8B0
#define COLOR_TEXT3    0x5A7A72
#define COLOR_BTN_OFF  0x0E1A26

#define CHART_PTS 60
#define FADE_FACTOR 32   /* 曲线下渐变填充最大不透明度(%), 对照 html fill-opacity */

/* FontAwesome 6 Free Solid 图标 (fa6_20 字体从完整 OTF 加载, 任意 FA6 图标可用) */
#define ICON_WIND      "\xEF\x9C\xAE"   /* fa-wind        增氧机 */
#define ICON_WATER     "\xEF\x9B\x83"   /* fa-water       循环水泵 */
#define ICON_SEEDLING  "\xEF\x93\x98"   /* fa-seedling    投喂电机 */
#define ICON_DROPLET   "\xEF\x80\xA3"   /* fa-droplet     溶氧 DO */
#define ICON_TEMP      "\xEF\x8B\x88"   /* fa-temperature-half  温度 */
#define ICON_FLASK     "\xEF\x83\x83"   /* fa-flask       pH */
#define ICON_SUN       "\xEF\x86\x85"   /* fa-sun         光照 */
#define ICON_ROTATE    "\xEF\x80\xA1"   /* fa-arrows-rotate  复位 */
#define ICON_GEARS     "\xEF\x82\x85"   /* fa-gears          PID 参数 */
#define ICON_BOLT      "\xEF\x83\xA7"   /* fa-bolt           设备状态 */
#define ICON_CHEVRON   "\xEF\x81\x94"   /* fa-chevron-right  可点击提示 */

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t * screen;
    control_back_cb_t back_cb;
    lv_timer_t * timer;

    /* 顶栏 */
    lv_obj_t * tab_auto, * tab_manual;
    lv_obj_t * mode_dot, * mode_txt;

    /* 视图 */
    lv_obj_t * view_auto, * view_manual;

    /* 自动-左 */
    lv_obj_t * chart;
    lv_chart_series_t * pv_ser, * sp_ser;
    lv_obj_t * pwm_bar, * pwm_val_lbl;

    /* 自动-右 被控量 */
    lv_obj_t * pv_lbl, * sp_lbl, * err_lbl;
    /* PID slider + val */
    lv_obj_t * kp_sl, * ki_sl, * kd_sl, * sp_sl;
    lv_obj_t * kp_v, * ki_v, * kd_v, * sp_v;
    /* 设备状态 */
    lv_obj_t * dev_aer_dot, * dev_aer_pc;
    lv_obj_t * dev_pump_dot, * dev_pump_pc;
    lv_obj_t * dev_feed_dot, * dev_feed_pc;

    /* 手动-反馈 */
    lv_obj_t * fb_do, * fb_temp, * fb_ph, * fb_light;
    /* 手动-增氧机 */
    lv_obj_t * m_aer_sl, * m_aer_v, * m_aer_dot, * m_aer_on, * m_aer_off;
    /* 手动-水泵 */
    lv_obj_t * m_pump_v, * m_pump_on, * m_pump_off, * m_pump_dot;
    /* 手动-投喂 */
    lv_obj_t * m_feed_sl, * m_feed_v, * m_feed_dot, * m_feed_btn;
    /* 急停 / 复位 (同一按钮 toggle) */
    lv_obj_t * estop_btn, * estop_lbl;

    /* 弹窗 */
    lv_obj_t * modal_pump, * modal_feeder;
    lv_obj_t * pt_sl, * pt_v, * pmin_sl, * pmin_v, * pmax_sl, * pmax_v;
    lv_obj_t * fh1_sl, * fh1_v, * fh2_sl, * fh2_v, * fmin_sl, * fmin_v;
} control_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_delete(lv_event_t * e);
static void on_back(lv_event_t * e);
static void on_tab_auto(lv_event_t * e);
static void on_tab_manual(lv_event_t * e);
static void on_pid_slider(lv_event_t * e);
static void on_pump_click(lv_event_t * e);
static void on_feeder_click(lv_event_t * e);
static void on_modal_close(lv_event_t * e);
static void on_pump_thr_slider(lv_event_t * e);
static void on_feeder_slider(lv_event_t * e);
static void on_manual_slider(lv_event_t * e);
static void on_aer_on(lv_event_t * e);
static void on_aer_off(lv_event_t * e);
static void on_pump_on(lv_event_t * e);
static void on_pump_off(lv_event_t * e);
static void on_feed(lv_event_t * e);
static void on_estop(lv_event_t * e);
static void timer_cb(lv_timer_t * t);
static void chart_draw_cb(lv_event_t * e);
static void refresh(control_ctx_t * ctx);
static void show_view(control_ctx_t * ctx, int auto_mode);

/**********************
 *  FACTORIES
 **********************/
static lv_obj_t * mk_card(lv_obj_t * parent, int x, int y, int w, int h)
{
    lv_obj_t * c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_set_style_clip_corner(c, true, 0);
    NO_SCROLL(c);
    return c;
}

static lv_obj_t * mk_label(lv_obj_t * parent, const char * txt,
                           const lv_font_t * f, uint32_t c)
{
    lv_obj_t * lb = lv_label_create(parent);
    if(txt) lv_label_set_text(lb, txt);
    lv_obj_set_style_text_font(lb, f, 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(c), 0);
    return lb;
}

static lv_obj_t * mk_dot(lv_obj_t * parent, uint32_t c)
{
    lv_obj_t * d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 8, 8);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    return d;
}

/* 圆形 logo 徽章: 主题色半透明背景 + 主题色图标, 卡片左上角点缀 (配色丰富) */
static lv_obj_t * mk_logo_badge(lv_obj_t * parent, uint32_t color, const char * icon)
{
    lv_obj_t * b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 36, 36);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_50, 0);
    lv_obj_t * ic = lv_label_create(b);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(color), 0);
    lv_obj_center(ic);
    return b;
}

/* 卡片头: [logo 徽章][状态点][名字+副标题 列 grow] — 圆点紧跟 logo, 名称占剩余 */
static void mk_card_head(lv_obj_t * parent, uint32_t color, const char * icon,
                         const char * name, const char * sub, lv_obj_t ** out_dot)
{
    lv_obj_t * h = lv_obj_create(parent);
    lv_obj_remove_style_all(h);
    lv_obj_set_width(h, LV_PCT(100));
    lv_obj_set_height(h, 40);
    lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(h, 8, 0);
    NO_SCROLL(h);
    mk_logo_badge(h, color, icon);
    if(out_dot) *out_dot = mk_dot(h, COLOR_TEXT3);   /* 圆点紧跟 logo */
    lv_obj_t * col = lv_obj_create(h);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);                     /* 名称列占满剩余, 不再挤飞 */
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 0, 0);
    NO_SCROLL(col);
    mk_label(col, name, app_font_kaiti_18(), COLOR_TEXT);
    mk_label(col, sub,  app_font_kaiti_14(), COLOR_TEXT3);
}

/* 标题行: [图标 主题色][标题文字] — 给卡片标题加 logo + 配色 */
static void mk_title_row(lv_obj_t * parent, const char * icon, uint32_t ic_color,
                         const char * txt, uint32_t txt_color)
{
    lv_obj_t * r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, LV_PCT(100));
    lv_obj_set_height(r, 20);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 6, 0);
    NO_SCROLL(r);
    if(icon) mk_label(r, icon, app_font_fa6_20(), ic_color);
    mk_label(r, txt, app_font_kaiti_14(), txt_color);
}

/* slider 行: [k标签 固定宽][slider grow][val标签 固定宽右对齐] */
static void mk_slider_row(lv_obj_t * parent, const char * k, int k_w,
                          const char * vtxt, int v_w,
                          int lo, int hi, lv_event_cb_t cb,
                          control_ctx_t * ctx,
                          lv_obj_t ** out_sl, lv_obj_t ** out_v)
{
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 22);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    NO_SCROLL(row);

    lv_obj_t * kl = mk_label(row, k, app_font_kaiti_14(), COLOR_TEXT2);
    lv_obj_set_width(kl, k_w);

    lv_obj_t * sl = lv_slider_create(row);
    lv_obj_set_flex_grow(sl, 1);
    lv_slider_set_range(sl, lo, hi);
    lv_obj_set_height(sl, 6);
    lv_obj_set_style_radius(sl, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(sl, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 3, LV_PART_KNOB);
    if(cb) lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, ctx);

    lv_obj_t * v = mk_label(row, vtxt, app_font_kaiti_14(), COLOR_ACCENT);
    lv_obj_set_width(v, v_w);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);

    if(out_sl) *out_sl = sl;
    if(out_v)  *out_v  = v;
}

/* 键值行: [k ........ v] */
static void mk_kv(lv_obj_t * parent, const char * k, lv_obj_t ** out_v, uint32_t vc)
{
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 20);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(row);
    mk_label(row, k, app_font_kaiti_14(), COLOR_TEXT3);
    lv_obj_t * v = mk_label(row, "—", app_font_kaiti_18(), vc);
    if(out_v) *out_v = v;
}

/* 设备状态行 (可点击行高亮: 主题色半透明底 + 边框 + 右侧箭头, 提示可点) */
static void mk_dev_row(lv_obj_t * parent, const char * nm, const char * tag,
                       uint32_t tag_color, int clickable, lv_event_cb_t cb,
                       control_ctx_t * ctx,
                       lv_obj_t ** out_dot, lv_obj_t ** out_pc)
{
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 26);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    NO_SCROLL(row);
    if(clickable) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(tag_color), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(tag_color), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_40, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);  /* 按下加深 */
        if(cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, ctx);
    }
    lv_obj_t * dot = mk_dot(row, COLOR_TEXT3);
    mk_label(row, nm, app_font_kaiti_14(), clickable ? COLOR_TEXT : COLOR_TEXT2);
    lv_obj_t * sp = lv_obj_create(row);
    lv_obj_remove_style_all(sp);
    lv_obj_set_flex_grow(sp, 1);
    mk_label(row, tag, app_font_kaiti_14(), tag_color);
    lv_obj_t * pc = mk_label(row, "—", app_font_kaiti_14(), COLOR_TEXT2);
    lv_obj_set_width(pc, 36);
    lv_obj_set_style_text_align(pc, LV_TEXT_ALIGN_RIGHT, 0);
    if(clickable) mk_label(row, ICON_CHEVRON, app_font_fa6_20(), tag_color);  /* › 可点提示 */
    if(out_dot) *out_dot = dot;
    if(out_pc)  *out_pc  = pc;
}

static void set_btn_on(lv_obj_t * btn, int on)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(on ? COLOR_ACCENT : COLOR_BTN_OFF), 0);
    lv_obj_t * lb = lv_obj_get_child(btn, 0);
    if(lb) lv_obj_set_style_text_color(lb, lv_color_hex(on ? 0x03150F : COLOR_TEXT2), 0);
}

static void set_tab_on(lv_obj_t * btn, int on)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(on ? COLOR_ACCENT : COLOR_BTN_OFF), 0);
    lv_obj_t * lb = lv_obj_get_child(btn, 0);
    if(lb) lv_obj_set_style_text_color(lb, lv_color_hex(on ? 0x03150F : COLOR_TEXT3), 0);
}

/* 通用按钮 (flex_grow 1, height 30) */
static lv_obj_t * mk_btn(lv_obj_t * parent, const char * txt, int on,
                         lv_event_cb_t cb, void * user_data)
{
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_height(b, 30);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 7, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(on ? COLOR_ACCENT : COLOR_BTN_OFF), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t * lb = lv_label_create(b);
    lv_label_set_text(lb, txt);
    lv_obj_set_style_text_font(lb, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(on ? 0x03150F : COLOR_TEXT2), 0);
    lv_obj_center(lb);
    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    return b;
}

/**********************
 *   BUILD
 **********************/
static void build_topbar(control_ctx_t * ctx)
{
    lv_obj_t * bar = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 800, 44);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_pad_left(bar, 12, 0);
    lv_obj_set_style_pad_right(bar, 12, 0);
    lv_obj_set_style_pad_top(bar, 0, 0);
    lv_obj_set_style_pad_bottom(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 10, 0);
    NO_SCROLL(bar);

    lv_obj_t * back = mk_btn(bar, "返回", 0, on_back, ctx);
    lv_obj_set_flex_grow(back, 0);
    lv_obj_set_width(back, 50);

    mk_label(bar, "智能闭环控制", app_font_kaiti_18(), COLOR_TEXT);
    mk_label(bar, "PID + PWM", app_font_kaiti_14(), COLOR_ACCENT);

    /* Tab 段控 */
    lv_obj_t * tabs = lv_obj_create(bar);
    lv_obj_remove_style_all(tabs);
    lv_obj_set_height(tabs, 28);
    lv_obj_set_width(tabs, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(COLOR_BTN_OFF), 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tabs, 8, 0);
    lv_obj_set_style_border_width(tabs, 1, 0);
    lv_obj_set_style_border_color(tabs, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(tabs, 2, 0);
    lv_obj_set_style_pad_column(tabs, 2, 0);
    NO_SCROLL(tabs);

    ctx->tab_auto = mk_btn(tabs, "闭环控制(自动)", 1, on_tab_auto, ctx);
    lv_obj_set_flex_grow(ctx->tab_auto, 0);
    lv_obj_set_height(ctx->tab_auto, 24);
    lv_obj_set_style_radius(ctx->tab_auto, 6, 0);
    lv_obj_set_style_border_width(ctx->tab_auto, 0, 0);
    lv_obj_set_style_pad_hor(ctx->tab_auto, 10, 0);
    ctx->tab_manual = mk_btn(tabs, "手动控制", 0, on_tab_manual, ctx);
    lv_obj_set_flex_grow(ctx->tab_manual, 0);
    lv_obj_set_height(ctx->tab_manual, 24);
    lv_obj_set_style_radius(ctx->tab_manual, 6, 0);
    lv_obj_set_style_border_width(ctx->tab_manual, 0, 0);
    lv_obj_set_style_pad_hor(ctx->tab_manual, 10, 0);

    lv_obj_t * sp = lv_obj_create(bar);
    lv_obj_remove_style_all(sp);
    lv_obj_set_flex_grow(sp, 1);

    ctx->mode_dot = mk_dot(bar, COLOR_ACCENT);
    ctx->mode_txt = mk_label(bar, "自动模式 · PID 闭环", app_font_kaiti_14(), COLOR_TEXT2);
}

static void build_view_auto(control_ctx_t * ctx)
{
    lv_obj_t * v = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(v);
    lv_obj_set_pos(v, 0, 44);
    lv_obj_set_size(v, 800, 436);
    NO_SCROLL(v);
    ctx->view_auto = v;

    /* ===== 曲线卡 (12,12) 496×330 ===== */
    lv_obj_t * cc = mk_card(v, 12, 12, 496, 330);
    lv_obj_t * crow = lv_obj_create(cc);
    lv_obj_remove_style_all(crow);
    lv_obj_set_pos(crow, 0, 0);
    lv_obj_set_size(crow, 476, 20);
    lv_obj_set_flex_flow(crow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(crow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(crow);
    lv_obj_t * ctl = lv_obj_create(crow);
    lv_obj_remove_style_all(ctl);
    lv_obj_set_flex_flow(ctl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctl, 6, 0);
    NO_SCROLL(ctl);
    mk_label(ctl, ICON_DROPLET, app_font_fa6_20(), COLOR_ACCENT);
    mk_label(ctl, "溶氧量 DO 闭环曲线", app_font_kaiti_14(), COLOR_TEXT2);
    lv_obj_t * leg = lv_obj_create(crow);
    lv_obj_remove_style_all(leg);
    lv_obj_set_flex_flow(leg, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(leg, 10, 0);
    NO_SCROLL(leg);
    mk_label(leg, "— SP 设定", app_font_kaiti_14(), COLOR_WARN);
    mk_label(leg, "— PV 实际", app_font_kaiti_14(), COLOR_ACCENT);

    ctx->chart = lv_chart_create(cc);
    lv_obj_set_pos(ctx->chart, 0, 24);
    lv_obj_set_size(ctx->chart, 476, 286);
    lv_chart_set_type(ctx->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ctx->chart, CHART_PTS);
    lv_chart_set_div_line_count(ctx->chart, 3, 5);
    lv_obj_set_style_bg_opa(ctx->chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->chart, 0, 0);
    lv_obj_set_style_pad_all(ctx->chart, 0, 0);
    lv_obj_set_style_line_color(ctx->chart, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_width(ctx->chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(ctx->chart, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_line_width(ctx->chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(ctx->chart, 0, 0, LV_PART_INDICATOR);
    ctx->pv_ser = lv_chart_add_series(ctx->chart, lv_color_hex(COLOR_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
    ctx->sp_ser = lv_chart_add_series(ctx->chart, lv_color_hex(COLOR_WARN), LV_CHART_AXIS_PRIMARY_Y);
    /* PV 曲线下渐变填充 (官方 area_gradient draw-task 钩子, 只画 PV 不画 SP) */
    lv_obj_add_event_cb(ctx->chart, chart_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(ctx->chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    /* ===== PWM 卡 (12,354) 496×70 — 条缩短居中 + 青→蓝渐变 ===== */
    lv_obj_t * pc = mk_card(v, 12, 354, 496, 70);
    lv_obj_t * prow = lv_obj_create(pc);
    lv_obj_remove_style_all(prow);
    lv_obj_set_pos(prow, 0, 0);
    lv_obj_set_size(prow, 476, 20);
    lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(prow);
    lv_obj_t * ptl = lv_obj_create(prow);
    lv_obj_remove_style_all(ptl);
    lv_obj_set_flex_flow(ptl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ptl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ptl, 6, 0);
    NO_SCROLL(ptl);
    mk_label(ptl, ICON_WIND, app_font_fa6_20(), COLOR_ACCENT);
    mk_label(ptl, "增氧机 PWM", app_font_kaiti_14(), COLOR_TEXT2);
    ctx->pwm_val_lbl = mk_label(prow, "0%", app_font_kaiti_18(), COLOR_ACCENT);
    ctx->pwm_bar = lv_bar_create(pc);
    lv_obj_set_pos(ctx->pwm_bar, 48, 28);          /* 居中缩短 476→380 */
    lv_obj_set_size(ctx->pwm_bar, 380, 14);
    lv_bar_set_range(ctx->pwm_bar, 0, 100);
    lv_obj_set_style_bg_color(ctx->pwm_bar, lv_color_hex(0x06101A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->pwm_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->pwm_bar, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(ctx->pwm_bar, lv_color_hex(COLOR_ACCENT2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(ctx->pwm_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ctx->pwm_bar, 7, 0);

    /* ===== 被控量卡 (520,12) 268×108 — pos(0,0) 消除偏右下 ===== */
    lv_obj_t * kc = mk_card(v, 520, 12, 268, 108);
    lv_obj_t * kw = lv_obj_create(kc);
    lv_obj_remove_style_all(kw);
    lv_obj_set_pos(kw, 0, 0);
    lv_obj_set_size(kw, 248, 88);
    lv_obj_set_flex_flow(kw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(kw, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(kw, 2, 0);
    NO_SCROLL(kw);
    mk_title_row(kw, ICON_DROPLET, COLOR_ACCENT, "被控量 · 溶氧 DO", COLOR_TEXT2);
    mk_kv(kw, "实际值 PV", &ctx->pv_lbl, COLOR_TEXT);
    mk_kv(kw, "设定值 SP", &ctx->sp_lbl, COLOR_WARN);
    mk_kv(kw, "误差 e", &ctx->err_lbl, COLOR_ACCENT);

    /* ===== PID 参数卡 (520,128) 268×156 ===== */
    lv_obj_t * pidc = mk_card(v, 520, 128, 268, 156);
    lv_obj_t * pidw = lv_obj_create(pidc);
    lv_obj_remove_style_all(pidw);
    lv_obj_set_pos(pidw, 0, 0);
    lv_obj_set_size(pidw, 248, 136);
    lv_obj_set_flex_flow(pidw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pidw, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(pidw, 6, 0);
    NO_SCROLL(pidw);
    mk_title_row(pidw, ICON_GEARS, COLOR_ACCENT2, "PID 参数", COLOR_TEXT2);
    mk_slider_row(pidw, "Kp", 26, "2.50", 44, 0, 100, on_pid_slider, ctx, &ctx->kp_sl, &ctx->kp_v);
    mk_slider_row(pidw, "Ki", 26, "0.10", 44, 0, 200, on_pid_slider, ctx, &ctx->ki_sl, &ctx->ki_v);
    mk_slider_row(pidw, "Kd", 26, "0.50", 44, 0, 500, on_pid_slider, ctx, &ctx->kd_sl, &ctx->kd_v);
    mk_slider_row(pidw, "SP", 26, "6.0",  44, 30, 90, on_pid_slider, ctx, &ctx->sp_sl, &ctx->sp_v);
    lv_slider_set_value(ctx->kp_sl, 25, LV_ANIM_OFF);
    lv_slider_set_value(ctx->ki_sl, 10, LV_ANIM_OFF);
    lv_slider_set_value(ctx->kd_sl, 50, LV_ANIM_OFF);
    lv_slider_set_value(ctx->sp_sl, 60, LV_ANIM_OFF);

    /* ===== 设备状态卡 (520,292) 268×144 — 水泵/投喂高亮可点 ===== */
    lv_obj_t * dc = mk_card(v, 520, 292, 268, 144);
    lv_obj_t * dw = lv_obj_create(dc);
    lv_obj_remove_style_all(dw);
    lv_obj_set_pos(dw, 0, 0);
    lv_obj_set_size(dw, 248, 124);
    lv_obj_set_flex_flow(dw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dw, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(dw, 8, 0);
    NO_SCROLL(dw);
    mk_title_row(dw, ICON_BOLT, COLOR_WARN, "设备状态 · 3 路", COLOR_TEXT2);
    mk_dev_row(dw, "增氧机", "PID", COLOR_ACCENT, 0, NULL, ctx, &ctx->dev_aer_dot, &ctx->dev_aer_pc);
    mk_dev_row(dw, "循环水泵", "阈值", COLOR_ACCENT2, 1, on_pump_click, ctx, &ctx->dev_pump_dot, &ctx->dev_pump_pc);
    mk_dev_row(dw, "投喂电机", "定时", COLOR_WARN, 1, on_feeder_click, ctx, &ctx->dev_feed_dot, &ctx->dev_feed_pc);
}

static void build_view_manual(control_ctx_t * ctx)
{
    lv_obj_t * v = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(v);
    lv_obj_set_pos(v, 0, 44);
    lv_obj_set_size(v, 800, 436);
    lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    NO_SCROLL(v);
    ctx->view_manual = v;

    /* 反馈条 (12,12) 776×36 — 每项 [图标 主题色][标签 灰][值 主题色 大字] */
    lv_obj_t * fb = mk_card(v, 12, 12, 776, 36);
    lv_obj_set_style_pad_all(fb, 0, 0);
    lv_obj_t * frow = lv_obj_create(fb);
    lv_obj_remove_style_all(frow);
    lv_obj_set_pos(frow, 0, 0);
    lv_obj_set_size(frow, 776, 36);
    lv_obj_set_flex_flow(frow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(frow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(frow);
    mk_label(frow, ICON_DROPLET, app_font_fa6_20(), COLOR_ACCENT);
    mk_label(frow, "DO", app_font_kaiti_14(), COLOR_TEXT3);
    ctx->fb_do    = mk_label(frow, "—", app_font_kaiti_18(), COLOR_ACCENT);
    mk_label(frow, ICON_TEMP, app_font_fa6_20(), COLOR_WARN);
    mk_label(frow, "温度", app_font_kaiti_14(), COLOR_TEXT3);
    ctx->fb_temp  = mk_label(frow, "—", app_font_kaiti_18(), COLOR_WARN);
    mk_label(frow, ICON_FLASK, app_font_fa6_20(), COLOR_ACCENT);
    mk_label(frow, "pH", app_font_kaiti_14(), COLOR_TEXT3);
    ctx->fb_ph    = mk_label(frow, "—", app_font_kaiti_18(), COLOR_ACCENT);
    mk_label(frow, ICON_SUN, app_font_fa6_20(), 0xFFD54F);
    mk_label(frow, "光照", app_font_kaiti_14(), COLOR_TEXT3);
    ctx->fb_light = mk_label(frow, "—", app_font_kaiti_18(), 0xFFD54F);

    /* 增氧机卡 (12,56) 250×296 — 青色主题 */
    lv_obj_t * ac = mk_card(v, 12, 56, 250, 296);
    lv_obj_t * aw = lv_obj_create(ac);
    lv_obj_remove_style_all(aw);
    lv_obj_set_pos(aw, 10, 8);
    lv_obj_set_size(aw, 230, 276);
    lv_obj_set_flex_flow(aw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(aw, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(aw, 8, 0);
    NO_SCROLL(aw);
    mk_card_head(aw, COLOR_ACCENT, ICON_WIND, "增氧机", "PID 调速", &ctx->m_aer_dot);
    ctx->m_aer_v = mk_label(aw, "0%", app_font_kaiti_32(), COLOR_ACCENT);
    mk_label(aw, "PWM 占空比", app_font_kaiti_14(), COLOR_TEXT3);
    ctx->m_aer_sl = lv_slider_create(aw);
    lv_obj_set_width(ctx->m_aer_sl, LV_PCT(82));
    lv_slider_set_range(ctx->m_aer_sl, 0, 100);
    lv_obj_set_height(ctx->m_aer_sl, 6);
    lv_obj_set_style_radius(ctx->m_aer_sl, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->m_aer_sl, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->m_aer_sl, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ctx->m_aer_sl, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ctx->m_aer_sl, lv_color_hex(COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(ctx->m_aer_sl, 3, LV_PART_KNOB);
    lv_obj_add_event_cb(ctx->m_aer_sl, on_manual_slider, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_t * abr = lv_obj_create(aw);
    lv_obj_remove_style_all(abr);
    lv_obj_set_width(abr, LV_PCT(82));
    lv_obj_set_height(abr, 30);
    lv_obj_set_flex_flow(abr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(abr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(abr, 8, 0);
    NO_SCROLL(abr);
    ctx->m_aer_on  = mk_btn(abr, "ON", 1, on_aer_on, ctx);
    lv_obj_set_flex_grow(ctx->m_aer_on, 0);  lv_obj_set_width(ctx->m_aer_on, 88);
    ctx->m_aer_off = mk_btn(abr, "OFF", 0, on_aer_off, ctx);
    lv_obj_set_flex_grow(ctx->m_aer_off, 0); lv_obj_set_width(ctx->m_aer_off, 88);
    mk_label(aw, "拖动滑条手动调速", app_font_kaiti_14(), COLOR_TEXT3);

    /* 水泵卡 (274,56) 250×296 — 蓝色主题 */
    lv_obj_t * pc = mk_card(v, 274, 56, 250, 296);
    lv_obj_t * pw = lv_obj_create(pc);
    lv_obj_remove_style_all(pw);
    lv_obj_set_pos(pw, 10, 8);
    lv_obj_set_size(pw, 230, 276);
    lv_obj_set_flex_flow(pw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pw, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(pw, 8, 0);
    NO_SCROLL(pw);
    mk_card_head(pw, COLOR_ACCENT2, ICON_WATER, "循环水泵", "阈值联动", &ctx->m_pump_dot);
    ctx->m_pump_v = mk_label(pw, "OFF", app_font_kaiti_32(), COLOR_TEXT3);
    mk_label(pw, "手动启停", app_font_kaiti_14(), COLOR_TEXT3);
    lv_obj_t * pbr = lv_obj_create(pw);
    lv_obj_remove_style_all(pbr);
    lv_obj_set_width(pbr, LV_PCT(82));
    lv_obj_set_height(pbr, 30);
    lv_obj_set_flex_flow(pbr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pbr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pbr, 8, 0);
    NO_SCROLL(pbr);
    ctx->m_pump_on  = mk_btn(pbr, "ON", 0, on_pump_on, ctx);
    lv_obj_set_flex_grow(ctx->m_pump_on, 0);  lv_obj_set_width(ctx->m_pump_on, 88);
    ctx->m_pump_off = mk_btn(pbr, "OFF", 1, on_pump_off, ctx);
    lv_obj_set_flex_grow(ctx->m_pump_off, 0); lv_obj_set_width(ctx->m_pump_off, 88);
    mk_label(pw, "阈值: 温度>32°C 或 pH 异常", app_font_kaiti_14(), COLOR_TEXT3);
    mk_label(pw, "手动启动后持续运行", app_font_kaiti_14(), COLOR_TEXT3);

    /* 投喂卡 (536,56) 250×296 — 橙色主题 */
    lv_obj_t * fc = mk_card(v, 536, 56, 250, 296);
    lv_obj_t * fw = lv_obj_create(fc);
    lv_obj_remove_style_all(fw);
    lv_obj_set_pos(fw, 10, 8);
    lv_obj_set_size(fw, 230, 276);
    lv_obj_set_flex_flow(fw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(fw, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(fw, 8, 0);
    NO_SCROLL(fw);
    mk_card_head(fw, COLOR_WARN, ICON_SEEDLING, "投喂电机", "定时定量", &ctx->m_feed_dot);
    ctx->m_feed_v = mk_label(fw, "60%", app_font_kaiti_32(), COLOR_WARN);
    mk_label(fw, "投喂量 PWM", app_font_kaiti_14(), COLOR_TEXT3);
    ctx->m_feed_sl = lv_slider_create(fw);
    lv_obj_set_width(ctx->m_feed_sl, LV_PCT(82));
    lv_slider_set_range(ctx->m_feed_sl, 0, 100);
    lv_slider_set_value(ctx->m_feed_sl, 60, LV_ANIM_OFF);
    lv_obj_set_height(ctx->m_feed_sl, 6);
    lv_obj_set_style_radius(ctx->m_feed_sl, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->m_feed_sl, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->m_feed_sl, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ctx->m_feed_sl, lv_color_hex(COLOR_WARN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ctx->m_feed_sl, lv_color_hex(COLOR_WARN), LV_PART_KNOB);
    lv_obj_set_style_pad_all(ctx->m_feed_sl, 3, LV_PART_KNOB);
    lv_obj_add_event_cb(ctx->m_feed_sl, on_manual_slider, LV_EVENT_VALUE_CHANGED, ctx);
    /* 单次投喂按钮 (warn 色, 82% 居中) */
    ctx->m_feed_btn = lv_button_create(fw);
    lv_obj_set_width(ctx->m_feed_btn, LV_PCT(82));
    lv_obj_set_height(ctx->m_feed_btn, 32);
    lv_obj_set_style_radius(ctx->m_feed_btn, 7, 0);
    lv_obj_set_style_bg_color(ctx->m_feed_btn, lv_color_hex(0x2A2410), 0);
    lv_obj_set_style_bg_opa(ctx->m_feed_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->m_feed_btn, 1, 0);
    lv_obj_set_style_border_color(ctx->m_feed_btn, lv_color_hex(COLOR_WARN), 0);
    lv_obj_set_style_pad_all(ctx->m_feed_btn, 0, 0);
    lv_obj_t * flb = lv_label_create(ctx->m_feed_btn);
    lv_label_set_text(flb, "单次投喂");
    lv_obj_set_style_text_font(flb, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(flb, lv_color_hex(COLOR_WARN), 0);
    lv_obj_center(flb);
    lv_obj_add_event_cb(ctx->m_feed_btn, on_feed, LV_EVENT_CLICKED, ctx);
    mk_label(fw, "到点自动投喂 · 持续设定时长", app_font_kaiti_14(), COLOR_TEXT3);

    /* 急停 / 复位 (12,360) 776×40 — 同一按钮 toggle: 急停↔复位 */
    ctx->estop_btn = lv_button_create(v);
    lv_obj_set_pos(ctx->estop_btn, 12, 360);
    lv_obj_set_size(ctx->estop_btn, 776, 40);
    lv_obj_set_style_radius(ctx->estop_btn, 10, 0);
    lv_obj_set_style_bg_color(ctx->estop_btn, lv_color_hex(0x2A1518), 0);
    lv_obj_set_style_bg_opa(ctx->estop_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->estop_btn, 1, 0);
    lv_obj_set_style_border_color(ctx->estop_btn, lv_color_hex(COLOR_ALARM), 0);
    lv_obj_set_style_pad_all(ctx->estop_btn, 0, 0);
    ctx->estop_lbl = lv_label_create(ctx->estop_btn);
    lv_label_set_text(ctx->estop_lbl, "■ 急停 — 全部设备停止");
    lv_obj_set_style_text_font(ctx->estop_lbl, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(ctx->estop_lbl, lv_color_hex(COLOR_ALARM), 0);
    lv_obj_center(ctx->estop_lbl);
    lv_obj_add_event_cb(ctx->estop_btn, on_estop, LV_EVENT_CLICKED, ctx);

    lv_obj_t * hint = mk_label(v, "急停后点击同按钮复位 · 恢复当前模式控制",
                               app_font_kaiti_14(), COLOR_TEXT3);
    lv_obj_set_pos(hint, 12, 408);
}

/* 弹窗: 半透明遮罩 + box 偏左上 (不再 flex 居中) */
static lv_obj_t * build_modal(control_ctx_t * ctx, lv_obj_t ** out_modal,
                              const char * title)
{
    lv_obj_t * mask = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(mask);
    lv_obj_set_size(mask, 800, 480);
    lv_obj_set_style_bg_color(mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_60, 0);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
    NO_SCROLL(mask);
    *out_modal = mask;

    /* box 绝对定位偏左上 (原 flex 居中 → 上移左移) */
    lv_obj_t * box = mk_card(mask, 180, 80, 340, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_t * bw = lv_obj_create(box);
    lv_obj_remove_style_all(bw);
    lv_obj_set_pos(bw, 12, 12);
    lv_obj_set_size(bw, 316, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bw, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(bw, 8, 0);
    NO_SCROLL(bw);

    /* header: 标题 (高 32, 与右上角关闭按钮同行对齐) */
    lv_obj_t * hh = lv_obj_create(bw);
    lv_obj_remove_style_all(hh);
    lv_obj_set_width(hh, LV_PCT(100));
    lv_obj_set_height(hh, 32);
    lv_obj_set_flex_flow(hh, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hh, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(hh);
    mk_label(hh, title, app_font_kaiti_18(), COLOR_TEXT);

    /* 关闭按钮: 浮在卡片右上角, 加大 32×32 圆形 alarm 色 (最后创建→顶层, 确保可点) */
    lv_obj_t * close = lv_button_create(box);
    lv_obj_set_pos(close, 298, 10);
    lv_obj_set_size(close, 32, 32);
    lv_obj_set_style_radius(close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(0x2A1518), 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(close, 1, 0);
    lv_obj_set_style_border_color(close, lv_color_hex(COLOR_ALARM), 0);
    lv_obj_set_style_pad_all(close, 0, 0);
    lv_obj_t * cl = lv_label_create(close);
    lv_label_set_text(cl, "X");
    lv_obj_set_style_text_font(cl, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(COLOR_ALARM), 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(close, on_modal_close, LV_EVENT_CLICKED, mask);
    return bw;
}

static void build_modals(control_ctx_t * ctx)
{
    lv_obj_t * bw1 = build_modal(ctx, &ctx->modal_pump, "循环水泵 — 阈值设置");
    mk_slider_row(bw1, "温度上限", 64, "32.0°C", 54, 280, 360, on_pump_thr_slider, ctx, &ctx->pt_sl, &ctx->pt_v);
    mk_slider_row(bw1, "pH 下限",  64, "6.5",    54, 55, 70, on_pump_thr_slider, ctx, &ctx->pmin_sl, &ctx->pmin_v);
    mk_slider_row(bw1, "pH 上限",  64, "9.0",    54, 75, 95, on_pump_thr_slider, ctx, &ctx->pmax_sl, &ctx->pmax_v);
    mk_label(bw1, "超任一阈值 → 循环换水  全部恢复 → 停止", app_font_kaiti_14(), COLOR_TEXT3);
    lv_slider_set_value(ctx->pt_sl, 320, LV_ANIM_OFF);
    lv_slider_set_value(ctx->pmin_sl, 65, LV_ANIM_OFF);
    lv_slider_set_value(ctx->pmax_sl, 90, LV_ANIM_OFF);

    lv_obj_t * bw2 = build_modal(ctx, &ctx->modal_feeder, "投喂电机 — 定时设置");
    mk_slider_row(bw2, "投喂时段1", 64, "08:00", 54, 5, 11, on_feeder_slider, ctx, &ctx->fh1_sl, &ctx->fh1_v);
    mk_slider_row(bw2, "投喂时段2", 64, "17:00", 54, 15, 21, on_feeder_slider, ctx, &ctx->fh2_sl, &ctx->fh2_v);
    mk_slider_row(bw2, "投喂时长",  64, "15 分钟", 54, 5, 30, on_feeder_slider, ctx, &ctx->fmin_sl, &ctx->fmin_v);
    mk_label(bw2, "到点自动投喂  持续设定时长后停止", app_font_kaiti_14(), COLOR_TEXT3);
    lv_slider_set_value(ctx->fh1_sl, 8, LV_ANIM_OFF);
    lv_slider_set_value(ctx->fh2_sl, 17, LV_ANIM_OFF);
    lv_slider_set_value(ctx->fmin_sl, 15, LV_ANIM_OFF);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * control_page_create(control_back_cb_t back_cb)
{
    control_ctx_t * ctx = lv_malloc_zeroed(sizeof(control_ctx_t));
    if(!ctx) return NULL;
    ctx->back_cb = back_cb;

    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    NO_SCROLL(screen);
    ctx->screen = screen;
    lv_obj_add_event_cb(screen, on_delete, LV_EVENT_DELETE, ctx);

    build_topbar(ctx);
    build_view_auto(ctx);
    build_view_manual(ctx);
    build_modals(ctx);

    /* 默认自动视图 (view_manual 已 hidden) */
    ctx->timer = lv_timer_create(timer_cb, 500, ctx);

    return screen;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void show_view(control_ctx_t * ctx, int auto_mode)
{
    if(auto_mode) {
        lv_obj_clear_flag(ctx->view_auto, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->view_manual, LV_OBJ_FLAG_HIDDEN);
        set_tab_on(ctx->tab_auto, 1);
        set_tab_on(ctx->tab_manual, 0);
        lv_obj_set_style_bg_color(ctx->mode_dot, lv_color_hex(COLOR_ACCENT), 0);
        lv_label_set_text(ctx->mode_txt, "自动模式 · PID 闭环");
    } else {
        lv_obj_add_flag(ctx->view_auto, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->view_manual, LV_OBJ_FLAG_HIDDEN);
        set_tab_on(ctx->tab_auto, 0);
        set_tab_on(ctx->tab_manual, 1);
        lv_obj_set_style_bg_color(ctx->mode_dot, lv_color_hex(COLOR_WARN), 0);
        lv_label_set_text(ctx->mode_txt, "手动模式 · 人工操作");
    }
}

static void on_back(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->back_cb) ctx->back_cb();
}

static void on_tab_auto(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    show_view(ctx, 1);
    app_action_control_set_mode(CTRL_MODE_AUTO);
}

static void on_tab_manual(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    show_view(ctx, 0);
    app_action_control_set_mode(CTRL_MODE_MANUAL);
}

static void on_pid_slider(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    lv_obj_t * s = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(s);
    char buf[16];
    if(s == ctx->kp_sl)      { snprintf(buf, sizeof(buf), "%.2f", v / 10.0f);  lv_label_set_text(ctx->kp_v, buf); }
    else if(s == ctx->ki_sl) { snprintf(buf, sizeof(buf), "%.2f", v / 100.0f); lv_label_set_text(ctx->ki_v, buf); }
    else if(s == ctx->kd_sl) { snprintf(buf, sizeof(buf), "%.2f", v / 100.0f); lv_label_set_text(ctx->kd_v, buf); }
    else if(s == ctx->sp_sl) { snprintf(buf, sizeof(buf), "%.1f", v / 10.0f);  lv_label_set_text(ctx->sp_v, buf); }
    float kp = lv_slider_get_value(ctx->kp_sl) / 10.0f;
    float ki = lv_slider_get_value(ctx->ki_sl) / 100.0f;
    float kd = lv_slider_get_value(ctx->kd_sl) / 100.0f;
    app_action_control_set_pid_gains(kp, ki, kd);
    app_action_control_set_aerator_sp(lv_slider_get_value(ctx->sp_sl) / 10.0f);
}

static void on_pump_click(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    lv_obj_clear_flag(ctx->modal_pump, LV_OBJ_FLAG_HIDDEN);
}

static void on_feeder_click(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    lv_obj_clear_flag(ctx->modal_feeder, LV_OBJ_FLAG_HIDDEN);
}

static void on_modal_close(lv_event_t * e)
{
    lv_obj_t * modal = lv_event_get_user_data(e);
    lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);
}

static void on_pump_thr_slider(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    lv_obj_t * s = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(s);
    char buf[16];
    if(s == ctx->pt_sl)       { snprintf(buf, sizeof(buf), "%.1f°C", v / 10.0f); lv_label_set_text(ctx->pt_v, buf); }
    else if(s == ctx->pmin_sl){ snprintf(buf, sizeof(buf), "%.1f", v / 10.0f);   lv_label_set_text(ctx->pmin_v, buf); }
    else if(s == ctx->pmax_sl){ snprintf(buf, sizeof(buf), "%.1f", v / 10.0f);   lv_label_set_text(ctx->pmax_v, buf); }
    app_action_control_set_pump_threshold(
        lv_slider_get_value(ctx->pt_sl)   / 10.0f,
        lv_slider_get_value(ctx->pmin_sl) / 10.0f,
        lv_slider_get_value(ctx->pmax_sl) / 10.0f);
}

static void on_feeder_slider(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    lv_obj_t * s = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(s);
    char buf[16];
    if(s == ctx->fh1_sl)     { snprintf(buf, sizeof(buf), "%02d:00", v); lv_label_set_text(ctx->fh1_v, buf); }
    else if(s == ctx->fh2_sl){ snprintf(buf, sizeof(buf), "%02d:00", v); lv_label_set_text(ctx->fh2_v, buf); }
    else if(s == ctx->fmin_sl){ snprintf(buf, sizeof(buf), "%d 分钟", v); lv_label_set_text(ctx->fmin_v, buf); }
    app_action_control_set_feeder_schedule(
        lv_slider_get_value(ctx->fh1_sl),
        lv_slider_get_value(ctx->fh2_sl),
        lv_slider_get_value(ctx->fmin_sl));
}

static void on_manual_slider(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    lv_obj_t * s = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(s);
    char buf[16];
    if(s == ctx->m_aer_sl) {
        snprintf(buf, sizeof(buf), "%d%%", v);
        lv_label_set_text(ctx->m_aer_v, buf);
        app_action_control_manual_set_aerator((float)v);
    } else if(s == ctx->m_feed_sl) {
        snprintf(buf, sizeof(buf), "%d%%", v);
        lv_label_set_text(ctx->m_feed_v, buf);
        app_action_control_set_feeder_duty((float)v);
    }
}

static void on_aer_on(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    app_action_control_manual_set_aerator((float)lv_slider_get_value(ctx->m_aer_sl));
    set_btn_on(ctx->m_aer_on, 1);
    set_btn_on(ctx->m_aer_off, 0);
}

static void on_aer_off(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    app_action_control_manual_set_aerator(0);
    lv_slider_set_value(ctx->m_aer_sl, 0, LV_ANIM_OFF);
    lv_label_set_text(ctx->m_aer_v, "0%");
    set_btn_on(ctx->m_aer_on, 0);
    set_btn_on(ctx->m_aer_off, 1);
}

static void on_pump_on(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    app_action_control_manual_set_pump(true);
    set_btn_on(ctx->m_pump_on, 1);
    set_btn_on(ctx->m_pump_off, 0);
}

static void on_pump_off(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    app_action_control_manual_set_pump(false);
    set_btn_on(ctx->m_pump_on, 0);
    set_btn_on(ctx->m_pump_off, 1);
}

static void on_feed(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    (void)ctx;
    app_action_control_manual_feed_trigger();
}

static void on_estop(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    control_status_t st;
    app_action_control_get_status(&st);
    /* toggle: 已急停 → 复位; 正常 → 急停 */
    if(st.estop) app_action_control_clear_estop();
    else         app_action_control_estop();
    refresh(ctx);   /* 立即反馈, 不等 500ms 定时 */
}

static void on_delete(lv_event_t * e)
{
    control_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->timer) lv_timer_delete(ctx->timer);
    lv_free(ctx);
}

/**********************
 *   REFRESH
 **********************/
/* PV 曲线下渐变填充 — 官方 lv_example_chart_area_gradient 适配 (同 trend_page)。
 * 只给 PV series (青色) 画面积, SP (橙色水平参考线) 跳过。 */
static void chart_draw_cb(lv_event_t * e)
{
    lv_draw_task_t * task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t * base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
    if(base->part != LV_PART_ITEMS) return;
    if(lv_draw_task_get_type(task) != LV_DRAW_TASK_TYPE_LINE) return;

    lv_draw_line_dsc_t * line_dsc = lv_draw_task_get_line_dsc(task);
    if(line_dsc == NULL || line_dsc->point_cnt < 2) return;

    /* 只给 PV (accent) 画面积, SP (warn) 是水平参考线不画 */
    lv_color_t pv_color = lv_color_hex(COLOR_ACCENT);
    if(line_dsc->color.red   != pv_color.red   ||
       line_dsc->color.green != pv_color.green ||
       line_dsc->color.blue  != pv_color.blue) return;

    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int32_t full_h = lv_obj_get_height(obj);
    if(full_h <= 0) full_h = 1;

    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);

    for(int32_t i = 0; i < line_dsc->point_cnt - 1; i++) {
        lv_point_precise_t p1 = line_dsc->points[i];
        lv_point_precise_t p2 = line_dsc->points[i + 1];
        if(p1.x == LV_DRAW_LINE_POINT_NONE || p1.y == LV_DRAW_LINE_POINT_NONE) continue;
        if(p2.x == LV_DRAW_LINE_POINT_NONE || p2.y == LV_DRAW_LINE_POINT_NONE) continue;

        tri.p[0].x = p1.x; tri.p[0].y = p1.y;
        tri.p[1].x = p2.x; tri.p[1].y = p2.y;
        tri.p[2].x = (p1.y < p2.y) ? p1.x : p2.x;
        tri.p[2].y = LV_MAX(p1.y, p2.y);
        tri.grad.dir = LV_GRAD_DIR_VER;

        int32_t fract_upper = (int32_t)(LV_MIN(p1.y, p2.y) - coords.y1) * 255 / full_h;
        int32_t fract_lower = (int32_t)(LV_MAX(p1.y, p2.y) - coords.y1) * 255 / full_h;
        lv_opa_t opa_u = (lv_opa_t)((255 - fract_upper) * FADE_FACTOR / 100);
        lv_opa_t opa_l = (lv_opa_t)((255 - fract_lower) * FADE_FACTOR / 100);

        tri.grad.stops[0].color = pv_color;
        tri.grad.stops[0].opa  = opa_u;
        tri.grad.stops[0].frac = 0;
        tri.grad.stops[1].color = pv_color;
        tri.grad.stops[1].opa  = opa_l;
        tri.grad.stops[1].frac = 255;
        lv_draw_triangle(base->layer, &tri);

        lv_draw_rect_dsc_t rect;
        lv_draw_rect_dsc_init(&rect);
        rect.bg_grad.dir = LV_GRAD_DIR_VER;
        rect.bg_grad.stops[0].color = pv_color;
        rect.bg_grad.stops[0].opa  = opa_l;
        rect.bg_grad.stops[0].frac = 0;
        rect.bg_grad.stops[1].color = pv_color;
        rect.bg_grad.stops[1].opa  = 0;
        rect.bg_grad.stops[1].frac = 255;
        lv_area_t ra;
        ra.x1 = (int32_t)p1.x;
        ra.x2 = (int32_t)p2.x - 1;
        ra.y1 = (int32_t)LV_MAX(p1.y, p2.y);
        ra.y2 = (int32_t)coords.y2;
        lv_draw_rect(base->layer, &rect, &ra);
    }
}

static void refresh(control_ctx_t * ctx)
{
    control_status_t st;
    app_action_control_get_status(&st);
    char buf[32];

    /* chart */
    static float pvf[CHART_PTS], spf[CHART_PTS];
    static int32_t ibuf[CHART_PTS];
    int n = app_action_control_get_pv_history(pvf, CHART_PTS);
    int m = app_action_control_get_sp_history(spf, CHART_PTS);
    int cnt = n < m ? n : m;
    lv_chart_set_all_values(ctx->chart, ctx->pv_ser, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(ctx->chart, ctx->sp_ser, LV_CHART_POINT_NONE);
    if(cnt > 0) {
        int i;
        for(i = 0; i < cnt; i++) ibuf[i] = (int32_t)(pvf[i] * 10.0f);
        lv_chart_set_series_values(ctx->chart, ctx->pv_ser, ibuf, (uint32_t)cnt);
        for(i = 0; i < cnt; i++) ibuf[i] = (int32_t)(spf[i] * 10.0f);
        lv_chart_set_series_values(ctx->chart, ctx->sp_ser, ibuf, (uint32_t)cnt);
        lv_chart_set_axis_range(ctx->chart, LV_CHART_AXIS_PRIMARY_Y, 30, 90);
    }
    lv_chart_refresh(ctx->chart);

    /* PWM 条 */
    lv_bar_set_value(ctx->pwm_bar, (int32_t)st.aerator_duty, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", (int)st.aerator_duty);
    lv_label_set_text(ctx->pwm_val_lbl, buf);

    /* 被控量 */
    snprintf(buf, sizeof(buf), "%.1f", st.do_val);
    lv_label_set_text(ctx->pv_lbl, buf);
    snprintf(buf, sizeof(buf), "%.1f", st.aerator_sp);
    lv_label_set_text(ctx->sp_lbl, buf);
    float err = st.aerator_sp - st.do_val;
    snprintf(buf, sizeof(buf), "%+.1f", err);
    lv_label_set_text(ctx->err_lbl, buf);

    /* 设备状态 */
    lv_obj_set_style_bg_color(ctx->dev_aer_dot, lv_color_hex(st.aerator_duty > 0 ? COLOR_ACCENT : COLOR_TEXT3), 0);
    snprintf(buf, sizeof(buf), "%d%%", (int)st.aerator_duty);
    lv_label_set_text(ctx->dev_aer_pc, buf);
    lv_obj_set_style_bg_color(ctx->dev_pump_dot, lv_color_hex(st.pump_on ? COLOR_ACCENT2 : COLOR_TEXT3), 0);
    lv_label_set_text(ctx->dev_pump_pc, st.pump_on ? "ON" : "OFF");
    lv_obj_set_style_bg_color(ctx->dev_feed_dot, lv_color_hex(st.feeder_on ? COLOR_ACCENT2 : COLOR_TEXT3), 0);
    lv_label_set_text(ctx->dev_feed_pc, st.feeder_on ? "ON" : "OFF");

    /* 手动反馈 */
    snprintf(buf, sizeof(buf), "%.1f", st.do_val);   lv_label_set_text(ctx->fb_do, buf);
    snprintf(buf, sizeof(buf), "%.1f", st.temp);     lv_label_set_text(ctx->fb_temp, buf);
    snprintf(buf, sizeof(buf), "%.1f", st.ph);       lv_label_set_text(ctx->fb_ph, buf);
    snprintf(buf, sizeof(buf), "%.0f", st.light);    lv_label_set_text(ctx->fb_light, buf);

    /* 手动三卡大数值 + 状态点 (颜色随状态联动, 配色丰富) */
    snprintf(buf, sizeof(buf), "%d%%", (int)st.aerator_duty);
    lv_label_set_text(ctx->m_aer_v, buf);
    lv_obj_set_style_text_color(ctx->m_aer_v, lv_color_hex(st.aerator_duty > 0 ? COLOR_ACCENT : COLOR_TEXT3), 0);
    lv_obj_set_style_bg_color(ctx->m_aer_dot, lv_color_hex(st.aerator_duty > 0 ? COLOR_ACCENT : COLOR_TEXT3), 0);

    lv_label_set_text(ctx->m_pump_v, st.pump_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(ctx->m_pump_v, lv_color_hex(st.pump_on ? COLOR_ACCENT2 : COLOR_TEXT3), 0);
    lv_obj_set_style_bg_color(ctx->m_pump_dot, lv_color_hex(st.pump_on ? COLOR_ACCENT2 : COLOR_TEXT3), 0);

    snprintf(buf, sizeof(buf), "%d%%", (int)st.feeder_duty);
    lv_label_set_text(ctx->m_feed_v, buf);
    lv_obj_set_style_text_color(ctx->m_feed_v, lv_color_hex(st.feeder_on ? COLOR_WARN : COLOR_TEXT3), 0);
    lv_obj_set_style_bg_color(ctx->m_feed_dot, lv_color_hex(st.feeder_on ? COLOR_WARN : COLOR_TEXT3), 0);

    /* estop: 顶栏 + 急停按钮外观联动 (急停→按钮变绿"复位"; 复位→恢复当前模式 + 按钮回红) */
    if(st.estop) {
        lv_obj_set_style_bg_color(ctx->mode_dot, lv_color_hex(COLOR_ALARM), 0);
        lv_label_set_text(ctx->mode_txt, "急停 · 全部设备停止");
        lv_obj_set_style_bg_color(ctx->estop_btn, lv_color_hex(0x0E2A1E), 0);
        lv_obj_set_style_border_color(ctx->estop_btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_label_set_text(ctx->estop_lbl, "复位 · 恢复运行");
        lv_obj_set_style_text_color(ctx->estop_lbl, lv_color_hex(COLOR_ACCENT), 0);
    } else {
        if(st.mode == CTRL_MODE_AUTO) {
            lv_obj_set_style_bg_color(ctx->mode_dot, lv_color_hex(COLOR_ACCENT), 0);
            lv_label_set_text(ctx->mode_txt, "自动模式 · PID 闭环");
        } else {
            lv_obj_set_style_bg_color(ctx->mode_dot, lv_color_hex(COLOR_WARN), 0);
            lv_label_set_text(ctx->mode_txt, "手动模式 · 人工操作");
        }
        lv_obj_set_style_bg_color(ctx->estop_btn, lv_color_hex(0x2A1518), 0);
        lv_obj_set_style_border_color(ctx->estop_btn, lv_color_hex(COLOR_ALARM), 0);
        lv_label_set_text(ctx->estop_lbl, "■ 急停 — 全部设备停止");
        lv_obj_set_style_text_color(ctx->estop_lbl, lv_color_hex(COLOR_ALARM), 0);
    }
}

static void timer_cb(lv_timer_t * t)
{
    control_ctx_t * ctx = lv_timer_get_user_data(t);
    refresh(ctx);
}
