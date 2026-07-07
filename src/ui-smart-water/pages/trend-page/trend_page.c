// ========== trend_page.c ==========
// 养殖环境趋势报表页。
// 布局严格对照 preview/trend_preview.html:
//   顶栏(返回|标题|下拉) + 中行(曲线卡 | 预测卡/相关性卡) + 底行(当日报表 | 剔除图例)
// 曲线采用 LVGL 官方 "Faded area under line chart":
//   LV_EVENT_DRAW_TASK_ADDED + lv_draw_triangle/rect 渐变填充,
//   参考 lvgl/examples/widgets/chart/lv_example_chart_area_gradient.c
// 多字体规则 (Mistake #15): 中文用 kaiti, 箭头/图标用 FA6, 分属不同 label
//   (SIMKAI 不含 →↑↓↔≥ 等字形, 凡箭头一律 FA6, 凡 ≥ 改 >).
#include "trend_page.h"
#include "../app_fonts.h"
#include "../../edge/edge_engine.h"
#include "../../edge/sensor_range.h"
#include "lvgl/lvgl_private.h"   /* draw-task API: lv_event_get_draw_task 等 */
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

/* Ocean 主题色 (与 sensor_page / home_page 一致, 对照 trend_preview.html) */
#define COLOR_BG       0x060E14
#define COLOR_CARD     0x0A1620
#define COLOR_BORDER   0x1C2E36
#define COLOR_ACCENT   0x00D4AA
#define COLOR_ACCENT2  0x0288D1
#define COLOR_TEXT     0xE6F2EE
#define COLOR_TEXT2    0x9AB8B0
#define COLOR_TEXT3    0x5A7A72
#define COLOR_REJECT   0xFF6B6B
#define COLOR_WARN     0xFFB800

#define CHART_N       180
#define FADE_FACTOR   32          /* 曲线下渐变填充最大不透明度(%), 对照 html fill-opacity 0.10 */
#define REGR_SAMPLES  600         /* 回归样本数 (=ANALYSIS_N), 600 点 @3s = 30min */
#define REGR_WINDOW   30          /* 回归窗口分钟数 */

/* FA6 Free Solid 码点 (U+F0xx) */
#define FA_CHEVRON_LEFT  "\xEF\x81\x93"   /* f053 */
#define FA_ARROW_RIGHT   "\xEF\x81\xA1"   /* f061 */
#define FA_ARROW_UP      "\xEF\x81\xA2"   /* f062 */
#define FA_ARROW_DOWN    "\xEF\x81\xA3"   /* f063 */
#define FA_MINUS         "\xEF\x81\xA8"   /* f068 */
#define FA_ANGLE_DOWN    "\xEF\x81\xB8"   /* f078 — 下拉箭头 */

/**********************
 *  TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t          * screen;
    lv_obj_t          * chart;
    lv_chart_series_t * ser;
    lv_obj_t          * dropdown;
    lv_obj_t          * range_min, * range_cur, * range_max;
    lv_obj_t          * pred_cur, * pred_val, * pred_trend, * pred_meta;
    lv_obj_t          * corr_pair[3], * corr_r[3];
    lv_obj_t          * daily_range[6], * daily_mean[6], * daily_rej[6];
    lv_timer_t        * timer;
    sensor_idx_t        sel;
    trend_back_cb_t     back_cb;
} trend_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_back(lv_event_t * e);
static void on_dropdown(lv_event_t * e);
static void on_delete(lv_event_t * e);
static void timer_cb(lv_timer_t * t);
static void chart_draw_cb(lv_event_t * e);
static void refresh(trend_ctx_t * ctx);
static void fmt_val(char * out, size_t sz, float v, int dec);

/* ── 快捷建 label ── */
static lv_obj_t * mk_label(lv_obj_t * parent, const char * txt,
                           const lv_font_t * f, uint32_t c)
{
    lv_obj_t * lb = lv_label_create(parent);
    if(txt) lv_label_set_text(lb, txt);
    lv_obj_set_style_text_font(lb, f, 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(c), 0);
    return lb;
}

/* ── 建卡片 (绝对定位, 海洋主题) ── */
static lv_obj_t * mk_card(lv_obj_t * parent, int x, int y, int w, int h)
{
    lv_obj_t * c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_set_style_clip_corner(c, true, 0);
    NO_SCROLL(c);
    return c;
}

static void fmt_val(char * out, size_t sz, float v, int dec)
{
    if(dec <= 0)      snprintf(out, sz, "%.0f", (double)v);
    else if(dec == 1) snprintf(out, sz, "%.1f", (double)v);
    else              snprintf(out, sz, "%.2f", (double)v);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * trend_page_create(trend_back_cb_t back_cb)
{
    trend_ctx_t * ctx = lv_malloc_zeroed(sizeof(trend_ctx_t));
    ctx->back_cb = back_cb;
    ctx->sel = SENSOR_IDX_TEMP;

    /* ===== SCREEN 800×480 ===== */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    NO_SCROLL(screen);
    ctx->screen = screen;
    lv_obj_add_event_cb(screen, on_delete, LV_EVENT_DELETE, ctx);

    /* ===== TOP BAR (12,10) 776×40 — flex row space-between ===== */
    lv_obj_t * bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 12, 10);
    lv_obj_set_size(bar, 776, 40);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(bar, 0, 0);
    NO_SCROLL(bar);

    /* 返回按钮: FA6 图标 + 中文 (kaiti 无 ◀ 字形, 必须分两个 label) */
    lv_obj_t * back = lv_obj_create(bar);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_pad_hor(back, 14, 0);
    lv_obj_set_style_pad_ver(back, 6, 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(back, 6, 0);
    NO_SCROLL(back);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, ctx);
    mk_label(back, FA_CHEVRON_LEFT, app_font_fa6_20(), COLOR_ACCENT);
    mk_label(back, "返回", app_font_kaiti_18(), COLOR_ACCENT);

    /* 标题 */
    mk_label(bar, "养殖环境趋势报表", app_font_kaiti_24(), COLOR_TEXT);

    /* 传感器下拉 — MAIN / ITEMS / 列表 三处都设字体, 否则列表项缺字 */
    {
        char opts[128];
        int p = 0;
        for(int i = 0; i < SENSOR_IDX_COUNT; i++)
            p += snprintf(opts + p, sizeof(opts) - (size_t)p, "%s%s",
                          g_sensor_phys[i].name,
                          i < SENSOR_IDX_COUNT - 1 ? "\n" : "");
        ctx->dropdown = lv_dropdown_create(bar);
        lv_dropdown_set_options(ctx->dropdown, opts);
        lv_dropdown_set_selected(ctx->dropdown, 0);
        lv_obj_set_width(ctx->dropdown, 124);
        lv_obj_set_style_text_font(ctx->dropdown, app_font_kaiti_14(), LV_PART_MAIN);
        lv_obj_set_style_text_font(ctx->dropdown, app_font_kaiti_14(), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(ctx->dropdown, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ctx->dropdown, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ctx->dropdown, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(ctx->dropdown, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
        lv_obj_set_style_radius(ctx->dropdown, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_left(ctx->dropdown, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(ctx->dropdown, 34, LV_PART_MAIN);  /* 文字在此截断, 给箭头留位 */
        lv_obj_set_style_text_color(ctx->dropdown, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
        /* symbol=NULL 时下拉框默认把文字"居中", 会挤到右侧箭头边;
         * 强制左对齐 → 文字贴最左, 箭头贴最右, 互不打架 (见 lv_dropdown.c:1083) */
        lv_obj_set_style_text_align(ctx->dropdown, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

        lv_obj_t * dd_list = lv_dropdown_get_list(ctx->dropdown);
        lv_obj_set_style_text_font(dd_list, app_font_kaiti_14(), 0);
        lv_obj_set_style_bg_color(dd_list, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(dd_list, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(dd_list, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_pad_hor(dd_list, 12, 0);
        lv_obj_set_style_pad_ver(dd_list, 6, 0);

        /* 下拉箭头: 内置 symbol 用 kaiti 字体(无 FA 字形)会空白,
         * 故隐藏内置 symbol, 另用 FA6 label 画 ▾ */
        lv_dropdown_set_symbol(ctx->dropdown, NULL);
        lv_obj_t * dd_arrow = mk_label(ctx->dropdown, FA_ANGLE_DOWN,
                                       app_font_fa6_20(), COLOR_ACCENT);
        lv_obj_add_flag(dd_arrow, LV_OBJ_FLAG_FLOATING);   /* 对齐到下拉框完整右边, 不被 pad_right 挤进文字区 */
        lv_obj_align(dd_arrow, LV_ALIGN_RIGHT_MID, -10, 0);

        lv_obj_add_event_cb(ctx->dropdown, on_dropdown, LV_EVENT_VALUE_CHANGED, ctx);
    }

    /* ===== 曲线卡 (12,58) 506×260 — 内部绝对定位 ===== */
    {
        lv_obj_t * ccard = mk_card(screen, 12, 58, 506, 260);   /* 内部 482×236 */

        ctx->chart = lv_chart_create(ccard);
        lv_chart_set_type(ctx->chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(ctx->chart, CHART_N);
        lv_obj_set_pos(ctx->chart, 0, 0);
        lv_obj_set_size(ctx->chart, 482, 206);
        lv_chart_set_div_line_count(ctx->chart, 3, 6);
        lv_obj_set_style_bg_opa(ctx->chart, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ctx->chart, 0, 0);
        lv_obj_set_style_radius(ctx->chart, 0, 0);
        lv_obj_set_style_pad_all(ctx->chart, 0, 0);
        /* 网格线 */
        lv_obj_set_style_line_color(ctx->chart, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
        lv_obj_set_style_line_width(ctx->chart, 1, LV_PART_MAIN);
        lv_obj_set_style_line_opa(ctx->chart, LV_OPA_60, LV_PART_MAIN);
        /* 本版本 lv_chart 无刻度 API, 默认不画刻度/标签, 仅 div_line_count 网格线 */
        /* 折线: 宽 2, 无数据点 */
        lv_obj_set_style_line_color(ctx->chart, lv_color_hex(COLOR_ACCENT), LV_PART_ITEMS);
        lv_obj_set_style_line_width(ctx->chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(ctx->chart, 0, 0, LV_PART_INDICATOR);

        ctx->ser = lv_chart_add_series(ctx->chart, lv_color_hex(COLOR_ACCENT),
                                       LV_CHART_AXIS_PRIMARY_Y);

        /* Faded area: 官方 draw-task 钩子 */
        lv_obj_add_event_cb(ctx->chart, chart_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_flag(ctx->chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

        /* 曲线下统计条: 最小 | 当前(强调) | 最大 */
        lv_obj_t * rrow = lv_obj_create(ccard);
        lv_obj_remove_style_all(rrow);
        lv_obj_set_pos(rrow, 0, 212);
        lv_obj_set_size(rrow, 482, 22);
        lv_obj_set_flex_flow(rrow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(rrow, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                    LV_FLEX_ALIGN_CENTER,
                                    LV_FLEX_ALIGN_CENTER);
        NO_SCROLL(rrow);
        ctx->range_min = mk_label(rrow, "最小 —", app_font_kaiti_14(), COLOR_TEXT2);
        ctx->range_cur = mk_label(rrow, "当前 —", app_font_kaiti_18(), COLOR_ACCENT);
        ctx->range_max = mk_label(rrow, "最大 —", app_font_kaiti_14(), COLOR_TEXT2);
    }

    /* ===== 预测卡 (528,58) 260×120 — flex column ===== */
    {
        lv_obj_t * pcard = mk_card(screen, 528, 58, 260, 120);  /* 内部 236×96 */
        lv_obj_set_flex_flow(pcard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(pcard, LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(pcard, 8, 0);
        NO_SCROLL(pcard);

        mk_label(pcard, "线性回归 · 1h预测", app_font_kaiti_14(), COLOR_TEXT3);

        /* 预测值行: 当前 →(FA6) 预测值+单位  趋势(FA6)  — 4 个 label */
        lv_obj_t * prow = lv_obj_create(pcard);
        lv_obj_remove_style_all(prow);
        lv_obj_set_width(prow, LV_PCT(100));
        lv_obj_set_height(prow, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_CENTER,
                                    LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(prow, 6, 0);
        NO_SCROLL(prow);
        ctx->pred_cur   = mk_label(prow, "—",        app_font_kaiti_18(), COLOR_TEXT2);
        mk_label                (prow, FA_ARROW_RIGHT, app_font_fa6_20(), COLOR_TEXT3);
        ctx->pred_val   = mk_label(prow, "—",        app_font_kaiti_18(), COLOR_ACCENT);
        ctx->pred_trend = mk_label(prow, FA_MINUS,   app_font_fa6_20(), COLOR_ACCENT);

        ctx->pred_meta = mk_label(pcard, "", app_font_kaiti_14(), COLOR_TEXT3);
        lv_obj_set_width(ctx->pred_meta, LV_PCT(100));
        lv_obj_set_style_text_line_space(ctx->pred_meta, 4, 0);
    }

    /* ===== 相关性卡 (528,186) 260×132 — flex column ===== */
    {
        lv_obj_t * ccard = mk_card(screen, 528, 186, 260, 132); /* 内部 236×108 */
        lv_obj_set_flex_flow(ccard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ccard, LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(ccard, 4, 0);
        NO_SCROLL(ccard);

        mk_label(ccard, "变量相关性 |r|>0.5", app_font_kaiti_14(), COLOR_TEXT3);

        for(int i = 0; i < 3; i++) {
            lv_obj_t * it = lv_obj_create(ccard);
            lv_obj_remove_style_all(it);
            lv_obj_set_width(it, LV_PCT(100));
            lv_obj_set_height(it, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_ver(it, 3, 0);
            lv_obj_set_flex_flow(it, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(it, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                        LV_FLEX_ALIGN_CENTER,
                                        LV_FLEX_ALIGN_CENTER);
            if(i < 2) {
                lv_obj_set_style_border_width(it, 1, 0);
                lv_obj_set_style_border_side(it, LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_color(it, lv_color_hex(COLOR_BORDER), 0);
            }
            NO_SCROLL(it);
            ctx->corr_pair[i] = mk_label(it, "—", app_font_kaiti_14(), COLOR_TEXT);
            ctx->corr_r[i]    = mk_label(it, "",  app_font_kaiti_14(), COLOR_ACCENT2);
        }
    }

    /* ===== 当日报表 (12,326) 556×144 — 内部绝对定位 + 2×3 网格 ===== */
    {
        lv_obj_t * dcard = mk_card(screen, 12, 326, 556, 144);  /* 内部 532×120 */

        lv_obj_t * header = mk_label(dcard, "当日报表", app_font_kaiti_14(), COLOR_TEXT3);
        lv_obj_set_pos(header, 0, 0);

        /* 2 列 × 3 行 网格 (对照 html grid-template-columns:1fr 1fr) */
        static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
        static int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
        lv_obj_t * body = lv_obj_create(dcard);
        lv_obj_remove_style_all(body);
        lv_obj_set_pos(body, 0, 22);
        lv_obj_set_size(body, 532, 98);
        lv_obj_set_grid_dsc_array(body, col_dsc, row_dsc);
        lv_obj_set_style_pad_all(body, 0, 0);
        lv_obj_set_style_pad_row(body, 4, 0);
        lv_obj_set_style_pad_column(body, 20, 0);
        NO_SCROLL(body);

        for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
            int col = i % 2, row = i / 2;
            lv_obj_t * r = lv_obj_create(body);
            lv_obj_remove_style_all(r);
            lv_obj_set_grid_cell(r, LV_GRID_ALIGN_STRETCH, col, 1,
                                    LV_GRID_ALIGN_STRETCH, row, 1);
            lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START,
                                        LV_FLEX_ALIGN_CENTER,
                                        LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(r, 6, 0);
            NO_SCROLL(r);

            lv_obj_t * nm = mk_label(r, g_sensor_phys[i].name, app_font_kaiti_14(), COLOR_TEXT2);
            lv_obj_set_style_min_width(nm, 38, 0);

            ctx->daily_range[i] = mk_label(r, "—", app_font_kaiti_14(), COLOR_TEXT);
            lv_obj_set_flex_grow(ctx->daily_range[i], 1);
            lv_label_set_long_mode(ctx->daily_range[i], LV_LABEL_LONG_DOT);

            ctx->daily_mean[i] = mk_label(r, "", app_font_kaiti_14(), COLOR_ACCENT);
            lv_obj_set_style_min_width(ctx->daily_mean[i], 50, 0);

            ctx->daily_rej[i] = mk_label(r, "", app_font_kaiti_14(), COLOR_REJECT);
            lv_obj_set_style_min_width(ctx->daily_rej[i], 48, 0);
        }
    }

    /* ===== 剔除标记图例 (578,326) 210×144 — flex column ===== */
    {
        lv_obj_t * lcard = mk_card(screen, 578, 326, 210, 144); /* 内部 186×120 */
        lv_obj_set_flex_flow(lcard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(lcard, LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START,
                                    LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(lcard, 7, 0);
        NO_SCROLL(lcard);

        mk_label(lcard, "剔除标记说明", app_font_kaiti_14(), COLOR_TEXT3);

        static const struct { uint32_t c; const char * n; const char * d; } L[4] = {
            { COLOR_ACCENT, "0", "正常 · 通过检查"   },
            { COLOR_WARN,   "1", "超量程 · 超出范围" },
            { COLOR_REJECT, "2", "突变 · 速率跳变"   },
            { COLOR_TEXT3,  "3", "重复 · 重投/卡死"  },
        };
        for(int i = 0; i < 4; i++) {
            lv_obj_t * it = lv_obj_create(lcard);
            lv_obj_remove_style_all(it);
            lv_obj_set_width(it, LV_PCT(100));
            lv_obj_set_height(it, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(it, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START,
                                        LV_FLEX_ALIGN_CENTER,
                                        LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(it, 6, 0);
            NO_SCROLL(it);

            /* 圆点 + 发光 (对照 html box-shadow) */
            lv_obj_t * dot = lv_obj_create(it);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(L[i].c), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 8, 0);
            lv_obj_set_style_shadow_color(dot, lv_color_hex(L[i].c), 0);
            lv_obj_set_style_shadow_opa(dot, LV_OPA_60, 0);
            NO_SCROLL(dot);

            lv_obj_t * num = mk_label(it, L[i].n, app_font_kaiti_14(), COLOR_TEXT);
            lv_obj_set_style_min_width(num, 10, 0);
            mk_label(it, L[i].d, app_font_kaiti_14(), COLOR_TEXT2);
        }
    }

    /* 首帧 + 5s 刷新 */
    refresh(ctx);
    ctx->timer = lv_timer_create(timer_cb, 5000, ctx);

    return screen;
}

/**********************
 *  FADED AREA DRAW (官方 lv_example_chart_area_gradient 适配)
 **********************/
static void chart_draw_cb(lv_event_t * e)
{
    lv_draw_task_t * task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t * base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
    if(base->part != LV_PART_ITEMS) return;
    if(lv_draw_task_get_type(task) != LV_DRAW_TASK_TYPE_LINE) return;

    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const lv_chart_series_t * ser = lv_chart_get_series_next(obj, NULL);
    if(ser == NULL) return;
    lv_color_t ser_color = lv_chart_get_series_color(obj, ser);

    lv_draw_line_dsc_t * line_dsc = lv_draw_task_get_line_dsc(task);
    if(line_dsc == NULL || line_dsc->point_cnt < 2) return;

    int32_t full_h = lv_obj_get_height(obj);
    if(full_h <= 0) full_h = 1;

    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);

    for(int32_t i = 0; i < line_dsc->point_cnt - 1; i++) {
        lv_point_precise_t p1 = line_dsc->points[i];
        lv_point_precise_t p2 = line_dsc->points[i + 1];
        if(p1.x == LV_DRAW_LINE_POINT_NONE || p1.y == LV_DRAW_LINE_POINT_NONE) continue;
        if(p2.x == LV_DRAW_LINE_POINT_NONE || p2.y == LV_DRAW_LINE_POINT_NONE) continue;

        /* 三角形: 两点 + 下方一点, 垂直渐变 (近线浓, 远线淡) */
        tri.p[0].x = p1.x; tri.p[0].y = p1.y;
        tri.p[1].x = p2.x; tri.p[1].y = p2.y;
        tri.p[2].x = (p1.y < p2.y) ? p1.x : p2.x;
        tri.p[2].y = LV_MAX(p1.y, p2.y);
        tri.grad.dir = LV_GRAD_DIR_VER;

        int32_t fract_upper = (int32_t)(LV_MIN(p1.y, p2.y) - coords.y1) * 255 / full_h;
        int32_t fract_lower = (int32_t)(LV_MAX(p1.y, p2.y) - coords.y1) * 255 / full_h;
        lv_opa_t opa_u = (lv_opa_t)((255 - fract_upper) * FADE_FACTOR / 100);
        lv_opa_t opa_l = (lv_opa_t)((255 - fract_lower) * FADE_FACTOR / 100);

        tri.grad.stops[0].color = ser_color;
        tri.grad.stops[0].opa  = opa_u;
        tri.grad.stops[0].frac = 0;
        tri.grad.stops[1].color = ser_color;
        tri.grad.stops[1].opa  = opa_l;
        tri.grad.stops[1].frac = 255;
        lv_draw_triangle(base->layer, &tri);

        /* 三角形下方矩形: 从较低点延伸到图表底部, 渐变到完全透明 */
        lv_draw_rect_dsc_t rect;
        lv_draw_rect_dsc_init(&rect);
        rect.bg_grad.dir = LV_GRAD_DIR_VER;
        rect.bg_grad.stops[0].color = ser_color;
        rect.bg_grad.stops[0].opa  = opa_l;
        rect.bg_grad.stops[0].frac = 0;
        rect.bg_grad.stops[1].color = ser_color;
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

/**********************
 *   DATA REFRESH
 **********************/
static void refresh_chart(trend_ctx_t * ctx)
{
    static float   fbuf[CHART_N];
    static int32_t ibuf[CHART_N];

    lv_chart_set_all_values(ctx->chart, ctx->ser, LV_CHART_POINT_NONE);
    int n = edge_engine_get_history(ctx->sel, fbuf, CHART_N);
    const sensor_phys_t * ph = &g_sensor_phys[ctx->sel];

    if(n > 0) {
        float mn = fbuf[0], mx = fbuf[0], cur = fbuf[n - 1];
        for(int k = 0; k < n; k++) {
            float v = fbuf[k];
            if(v < mn) mn = v;
            if(v > mx) mx = v;
            ibuf[k] = (int32_t)(v * 10.0f);
        }
        lv_chart_set_series_values(ctx->chart, ctx->ser, ibuf, (size_t)n);

        /* 自适应纵轴: 数据范围 + 15% 余量, 钳制在物理量程内 */
        float lo = mn, hi = mx, pad = (hi - lo) * 0.15f;
        if(pad < 0.5f) pad = 0.5f;
        lo -= pad; hi += pad;
        if(lo < ph->phys_min) lo = ph->phys_min;
        if(hi > ph->phys_max) hi = ph->phys_max;
        if(hi <= lo) hi = lo + 1.0f;
        lv_chart_set_axis_range(ctx->chart, LV_CHART_AXIS_PRIMARY_Y,
                                (int32_t)(lo * 10), (int32_t)(hi * 10));

        char s[32], smn[16], scur[16], smx[16];
        fmt_val(smn,  sizeof(smn),  mn,  ph->dec);
        fmt_val(scur, sizeof(scur), cur, ph->dec);
        fmt_val(smx,  sizeof(smx),  mx,  ph->dec);
        snprintf(s, sizeof(s), "最小 %s%s", smn, ph->unit);  lv_label_set_text(ctx->range_min, s);
        snprintf(s, sizeof(s), "当前 %s%s", scur, ph->unit); lv_label_set_text(ctx->range_cur, s);
        snprintf(s, sizeof(s), "最大 %s%s", smx, ph->unit);  lv_label_set_text(ctx->range_max, s);
    } else {
        lv_label_set_text(ctx->range_min, "最小 —");
        lv_label_set_text(ctx->range_cur, "当前 —");
        lv_label_set_text(ctx->range_max, "最大 —");
    }
    lv_chart_refresh(ctx->chart);
}

static void refresh_analysis(trend_ctx_t * ctx)
{
    edge_analysis_t a;
    if(!edge_engine_get_analysis(&a)) {
        lv_label_set_text(ctx->pred_cur, "—");
        lv_label_set_text(ctx->pred_val, "—");
        lv_label_set_text(ctx->pred_trend, FA_MINUS);
        lv_label_set_text(ctx->pred_meta, "暂无分析数据");
        for(int i = 0; i < 3; i++) {
            lv_label_set_text(ctx->corr_pair[i], i == 0 ? "暂无分析数据" : "");
            lv_label_set_text(ctx->corr_r[i], "");
        }
        return;
    }
    const regr_t * r = &a.regr[ctx->sel];
    const sensor_phys_t * ph = &g_sensor_phys[ctx->sel];

    if(r->valid) {
        float pred = r->pred_1h;   /* 再约束一次, 防旧缓存越界/冲到边界 */
        {
            float max_dev = (ph->phys_max - ph->phys_min) * 0.20f;
            if(pred > r->cur + max_dev) pred = r->cur + max_dev;
            if(pred < r->cur - max_dev) pred = r->cur - max_dev;
            if(pred < ph->phys_min) pred = ph->phys_min;
            if(pred > ph->phys_max) pred = ph->phys_max;
        }
        /* 趋势方向用"预测值 vs 当前值"判断, 阈值取量程的 0.5%,
         * 适应各路量级 (pH/氨氮变化小, 旧的固定 0.005/点阈值把它们全判成持平) */
        float range  = ph->phys_max - ph->phys_min;
        float diff   = pred - r->cur;
        float thresh = range * 0.005f;
        const char * arrow = (diff >  thresh) ? FA_ARROW_UP
                           : (diff < -thresh) ? FA_ARROW_DOWN
                           : FA_MINUS;
        const char * trend = (diff >  thresh) ? "上升"
                           : (diff < -thresh) ? "下降"
                           : "持平";
        char scur[16], spred[16], spred_u[24], meta[128];
        fmt_val(scur,  sizeof(scur),  r->cur, ph->dec);
        fmt_val(spred, sizeof(spred), pred,  ph->dec);
        snprintf(spred_u, sizeof(spred_u), "%s%s", spred, ph->unit);
        lv_label_set_text(ctx->pred_cur, scur);
        lv_label_set_text(ctx->pred_val, spred_u);
        lv_label_set_text(ctx->pred_trend, arrow);
        /* 斜率换算成 /min (采样间隔 3s → 20 点/min; 原始 slope 是 /点) */
        float slope_per_min = r->slope * 20.0f;
        snprintf(meta, sizeof(meta), "趋势%s · 斜率%+.4f/min\n样本 %d 点 (最近%dmin)",
                 trend, (double)slope_per_min, REGR_SAMPLES, REGR_WINDOW);
        lv_label_set_text(ctx->pred_meta, meta);
    } else {
        lv_label_set_text(ctx->pred_cur, "—");
        lv_label_set_text(ctx->pred_val, "—");
        lv_label_set_text(ctx->pred_trend, FA_MINUS);
        lv_label_set_text(ctx->pred_meta, "数据不足 · 至少2个有效点");
    }

    /* 相关性: 最多展示 3 对 (对照 html) */
    int shown = a.corr_n < 3 ? a.corr_n : 3;
    for(int i = 0; i < 3; i++) {
        lv_label_set_text(ctx->corr_pair[i], "");
        lv_label_set_text(ctx->corr_r[i], "");
    }
    for(int i = 0; i < shown; i++) {
        const corr_pair_t * c = &a.corr[i];
        if(c->a < 0 || c->a >= SENSOR_IDX_COUNT) continue;
        if(c->b < 0 || c->b >= SENSOR_IDX_COUNT) continue;
        char p[64], rr[24];
        snprintf(p, sizeof(p), "%s · %s",
                 g_sensor_phys[c->a].name, g_sensor_phys[c->b].name);
        snprintf(rr, sizeof(rr), "r=%+.2f", (double)c->r);
        lv_label_set_text(ctx->corr_pair[i], p);
        lv_label_set_text(ctx->corr_r[i], rr);
    }
    if(shown == 0) {
        lv_label_set_text(ctx->corr_pair[0], "无显著相关 |r|<0.5");
    }
}

static void refresh_daily(trend_ctx_t * ctx)
{
    edge_analysis_t a;
    if(!edge_engine_get_analysis(&a)) {
        for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
            lv_label_set_text(ctx->daily_range[i], "—");
            lv_label_set_text(ctx->daily_mean[i], "");
            lv_label_set_text(ctx->daily_rej[i], "");
        }
        return;
    }
    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        const daily_t * d = &a.daily[i];
        const sensor_phys_t * ph = &g_sensor_phys[i];
        if(d->valid && d->count > 0) {
            char smn[12], smx[12], smean[12], range[40], avg[20], rej[16];
            fmt_val(smn, sizeof(smn), d->min, ph->dec);
            fmt_val(smx, sizeof(smx), d->max, ph->dec);
            snprintf(range, sizeof(range), "%s~%s%s", smn, smx, ph->unit);
            fmt_val(smean, sizeof(smean), d->mean, ph->dec);
            snprintf(avg, sizeof(avg), "均%s", smean);
            snprintf(rej, sizeof(rej), "剔除%d", d->reject);
            lv_label_set_text(ctx->daily_range[i], range);
            lv_label_set_text(ctx->daily_mean[i], avg);
            lv_label_set_text(ctx->daily_rej[i], rej);
        } else {
            lv_label_set_text(ctx->daily_range[i], "暂无数据");
            lv_label_set_text(ctx->daily_mean[i], "");
            lv_label_set_text(ctx->daily_rej[i], "");
        }
    }
}

static void refresh(trend_ctx_t * ctx)
{
    refresh_chart(ctx);
    refresh_analysis(ctx);
    refresh_daily(ctx);
}

/**********************
 *   EVENTS
 **********************/
static void timer_cb(lv_timer_t * t)
{
    trend_ctx_t * ctx = lv_timer_get_user_data(t);
    if(ctx) refresh(ctx);
}

static void on_dropdown(lv_event_t * e)
{
    trend_ctx_t * ctx = lv_event_get_user_data(e);
    if(!ctx) return;
    uint32_t sel = lv_dropdown_get_selected(ctx->dropdown);
    if(sel < SENSOR_IDX_COUNT) {
        ctx->sel = (sensor_idx_t)sel;
        refresh(ctx);
    }
}

static void on_back(lv_event_t * e)
{
    trend_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->back_cb) ctx->back_cb();
}

static void on_delete(lv_event_t * e)
{
    trend_ctx_t * ctx = lv_event_get_user_data(e);
    if(!ctx) return;
    if(ctx->timer) lv_timer_delete(ctx->timer);
    lv_free(ctx);
}
