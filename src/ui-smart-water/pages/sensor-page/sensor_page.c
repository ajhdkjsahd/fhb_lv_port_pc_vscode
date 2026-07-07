// ========== sensor_page.c ==========
// 传感器数据监测页：6 路传感器实时展示（温度/湿度/光照/溶解氧/pH/氨氮）
// 风格与首页/子页保持一致（海洋深色主题），2×3 卡片网格 + 三态告警配色。
#include "sensor_page.h"
#include "../app_fonts.h"
#include "../app_actions.h"
#include "../app_mqtt.h"
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

/* 主题色（与 home_page / gallery_page 等保持一致） */
#define COLOR_BG       0x060E14
#define COLOR_CARD     0x0A1620
#define COLOR_BORDER   0x1C2E36
#define COLOR_ACCENT   0x00D4AA
#define COLOR_ACCENT2  0x0288D1
#define COLOR_WARN     0xFFB800
#define COLOR_ALARM    0xFF4455
#define COLOR_TEXT     0xE6F2EE
#define COLOR_TEXT2    0x9AB8B0
#define COLOR_TEXT3    0x5A7A72

/**********************
 *      TYPEDEFS
 **********************/
typedef enum {
    SENSOR_STATUS_NORMAL = 0,
    SENSOR_STATUS_WARN,
    SENSOR_STATUS_ALARM
} sensor_status_t;

/* 单路传感器的展示元数据（量程 / 阈值 / 图标 / 单位）。
 * 状态判定由本表完成；数值来源由 app_action_sensor_read() 提供。 */
typedef struct {
    const char * name;       /* 中文名 */
    const char * unit;       /* 单位 */
    const char * icon;       /* FA6 图标 UTF-8 字面量 */
    float min, max;          /* 量程 → 进度条映射 */
    float safe_lo, safe_hi;  /* 安全带（正常） */
    float warn_lo, warn_hi;  /* 预警带边界（超出即告警） */
    int   dec;               /* 小数位 */
    bool  is_light;          /* 光照卡片：发光强度随数值 */
} sensor_meta_t;

/* 单张卡片的控件句柄 */
typedef struct {
    lv_obj_t * card;
    lv_obj_t * value;        /* 大号数值 label */
    lv_obj_t * bar;          /* 进度条 */
    lv_obj_t * dot;          /* 状态圆点 */
    lv_obj_t * icon_badge;   /* 图标底座 */
    lv_obj_t * icon;         /* FA6 图标 label */
} sensor_card_t;

typedef struct {
    lv_obj_t * screen;
    sensor_card_t cards[SENSOR_IDX_COUNT];
    lv_timer_t * timer;
    sensor_back_cb_t back_cb;
} sensor_page_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_back_click(lv_event_t * e);
static void on_page_delete(lv_event_t * e);
static void timer_cb(lv_timer_t * timer);
static void live_dot_glow_cb(void * var, int32_t v);
static void create_sensor_card(lv_obj_t * body, int idx, sensor_page_ctx_t * ctx);
static void update_sensors(sensor_page_ctx_t * ctx);
static sensor_status_t compute_status(const sensor_meta_t * m, float v);
static lv_color_t status_color(sensor_status_t st);

/**********************
 *  STATIC VARIABLES
 **********************/
/* 6 路传感器元数据表（顺序与 sensor_idx_t 一致） */
static const sensor_meta_t g_sensors[SENSOR_IDX_COUNT] = {
    [SENSOR_IDX_TEMP]  = { "温度",   "°C",   "\xEF\x8B\x87", 0, 40,  18,  28,   15,  32,   1, false }, /* fa-temperature-half */
    [SENSOR_IDX_HUMI]  = { "湿度",   "%",    "\xEF\x81\x83", 0, 100, 40,  80,   30,  90,   1, false }, /* fa-droplet          */
    [SENSOR_IDX_LIGHT] = { "光照",   "%",    "\xEF\x86\x85", 0, 100, 20,  90,   10,  100,  1, true  }, /* fa-sun              */
    [SENSOR_IDX_DO]    = { "溶解氧", "mg/L", "\xEF\x9C\xAE", 0, 20,  5,   20,   3,   20,   1, false }, /* fa-wind             */
    [SENSOR_IDX_PH]    = { "pH值",   "",     "\xEF\x92\x92", 0, 14,  6.5f,8.5f, 6,   9,    2, false }, /* fa-vial             */
    [SENSOR_IDX_NH3N]  = { "氨氮",   "mg/L", "\xEF\x83\x83", 0, 5,   0,   0.5f, 0,   1.0f, 2, false }, /* fa-flask            */
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * sensor_page_create(sensor_back_cb_t back_cb)
{
    sensor_page_ctx_t * ctx = lv_malloc(sizeof(sensor_page_ctx_t));
    if(ctx == NULL) return NULL;
    memset(ctx, 0, sizeof(sensor_page_ctx_t));
    ctx->back_cb = back_cb;

    /* ===== SCREEN ===== */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(screen);
    ctx->screen = screen;

    lv_obj_add_event_cb(screen, on_page_delete, LV_EVENT_DELETE, ctx);

    /* ===== HEADER（居中：logo + 标题 + 副标题） ===== */
    lv_obj_t * header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(header, 4, 0);
    lv_obj_set_style_pad_top(header, 18, 0);
    lv_obj_set_style_pad_bottom(header, 8, 0);
    NO_SCROLL(header);

    /* logo 圆形（fa-gauge-high 仪表盘图标） */
    lv_obj_t * logo = lv_obj_create(header);
    lv_obj_remove_style_all(logo);
    lv_obj_set_size(logo, 46, 46);
    lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(logo, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_grad_color(logo, lv_color_hex(COLOR_ACCENT2), 0);
    lv_obj_set_style_bg_grad_dir(logo, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(logo, 20, 0);
    lv_obj_set_style_shadow_color(logo, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_shadow_opa(logo, LV_OPA_40, 0);
    lv_obj_set_flex_flow(logo, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(logo, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(logo);

    lv_obj_t * logo_icon = lv_label_create(logo);
    lv_obj_set_style_text_font(logo_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(logo_icon, lv_color_white(), 0);
    lv_label_set_text(logo_icon, "\xEF\x98\xA4");  /* fa-gauge-high */

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "传感器数据监测");
    lv_obj_set_style_text_font(title, app_font_kaiti_24(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);

    lv_obj_t * sub = lv_label_create(header);
    lv_label_set_text(sub, "REAL-TIME  SENSOR  MONITORING");
    lv_obj_set_style_text_font(sub, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_TEXT3), 0);

    /* ===== 返回按钮（左上角，浮动） ===== */
    lv_obj_t * back = lv_button_create(screen);
    lv_obj_set_size(back, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_pad_hor(back, 14, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(back, 6, 0);
    NO_SCROLL(back);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_add_flag(back, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_event_cb(back, on_back_click, LV_EVENT_CLICKED, ctx);

    lv_obj_t * back_icon = lv_label_create(back);
    lv_obj_set_style_text_font(back_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(COLOR_TEXT2), 0);
    lv_label_set_text(back_icon, "\xEF\x81\x93");  /* fa-chevron-left */

    lv_obj_t * back_txt = lv_label_create(back);
    lv_label_set_text(back_txt, "返回首页");
    lv_obj_set_style_text_font(back_txt, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(back_txt, lv_color_hex(COLOR_TEXT2), 0);

    /* ===== 实时监测徽章（右上角，浮动 + 脉冲） ===== */
    lv_obj_t * live = lv_obj_create(screen);
    lv_obj_remove_style_all(live);
    lv_obj_set_size(live, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_pad_hor(live, 12, 0);
    lv_obj_set_style_radius(live, 10, 0);
    lv_obj_set_style_bg_color(live, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(live, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(live, 1, 0);
    lv_obj_set_style_border_color(live, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_flex_flow(live, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(live, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(live, 6, 0);
    NO_SCROLL(live);
    lv_obj_align(live, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_add_flag(live, LV_OBJ_FLAG_FLOATING);

    lv_obj_t * ldot = lv_obj_create(live);
    lv_obj_remove_style_all(ldot);
    lv_obj_set_size(ldot, 8, 8);
    lv_obj_set_style_radius(ldot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ldot, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(ldot, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(ldot, 8, 0);
    lv_obj_set_style_shadow_color(ldot, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_shadow_opa(ldot, 0, 0);
    NO_SCROLL(ldot);

    lv_obj_t * ltxt = lv_label_create(live);
    lv_label_set_text(ltxt, "实时监测");
    lv_obj_set_style_text_font(ltxt, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(ltxt, lv_color_hex(COLOR_ACCENT), 0);

    /* 脉冲发光动画（shadow_opa 0 → 70 → 0 循环） */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ldot);
    lv_anim_set_exec_cb(&a, live_dot_glow_cb);
    lv_anim_set_values(&a, 0, 179);
    lv_anim_set_duration(&a, 1200);
    lv_anim_set_playback_duration(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    /* ===== BODY：2 行 × 3 列卡片网格 ===== */
    lv_obj_t * body = lv_obj_create(screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body, 1);
    static int32_t col_dsc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static int32_t row_dsc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(body, col_dsc, row_dsc);
    lv_obj_set_style_pad_all(body, 16, 0);
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_set_style_pad_column(body, 12, 0);
    NO_SCROLL(body);

    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        create_sensor_card(body, i, ctx);
    }

    /* ===== FOOTER ===== */
    lv_obj_t * footer = lv_obj_create(screen);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(footer, 8, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(footer);

    lv_obj_t * ftxt = lv_label_create(footer);
    lv_label_set_text(ftxt, "实时数据 · 每秒刷新 · Smart Water Aquaculture System v1.0");
    lv_obj_set_style_text_font(ftxt, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(ftxt, lv_color_hex(COLOR_TEXT3), 0);

    /* ===== 1 秒刷新定时器 + 首帧立即填充 ===== */
    ctx->timer = lv_timer_create(timer_cb, 1000, ctx);
    update_sensors(ctx);

    return screen;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* 构建单张传感器卡片 */
static void create_sensor_card(lv_obj_t * body, int idx, sensor_page_ctx_t * ctx)
{
    const sensor_meta_t * m = &g_sensors[idx];
    int col = idx % 3;
    int row = idx / 3;

    /* ----- 卡片容器 ----- */
    lv_obj_t * card = lv_obj_create(body);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 12, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 6, 0);
    NO_SCROLL(card);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    ctx->cards[idx].card = card;

    /* ----- 顶行：图标底座 + 名称 + 状态点 ----- */
    lv_obj_t * top = lv_obj_create(card);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 8, 0);
    NO_SCROLL(top);

    /* 图标底座（34×34，状态色 10% 透明背景） */
    lv_obj_t * badge = lv_obj_create(top);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 34, 34);
    lv_obj_set_style_radius(badge, 9, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_10, 0);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(badge);
    ctx->cards[idx].icon_badge = badge;

    lv_obj_t * icon = lv_label_create(badge);
    lv_obj_set_style_text_font(icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_label_set_text(icon, m->icon);
    ctx->cards[idx].icon = icon;

    /* 名称（flex_grow 撑开，把状态点推到右侧） */
    lv_obj_t * name = lv_label_create(top);
    lv_label_set_text(name, m->name);
    lv_obj_set_style_text_font(name, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT2), 0);
    lv_obj_set_size(name, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(name, 1);

    /* 状态圆点 */
    lv_obj_t * dot = lv_obj_create(top);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 9, 9);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    NO_SCROLL(dot);
    ctx->cards[idx].dot = dot;

    /* ----- 数值行：大号数值 + 单位 ----- */
    lv_obj_t * vrow = lv_obj_create(card);
    lv_obj_remove_style_all(vrow);
    lv_obj_set_size(vrow, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(vrow, 4, 0);
    NO_SCROLL(vrow);

    lv_obj_t * val = lv_label_create(vrow);
    lv_obj_set_style_text_font(val, app_font_kaiti_32(), 0);
    lv_obj_set_style_text_color(val, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(val, "--");
    ctx->cards[idx].value = val;

    lv_obj_t * unit = lv_label_create(vrow);
    lv_obj_set_style_text_font(unit, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(COLOR_TEXT3), 0);
    lv_label_set_text(unit, m->unit);

    /* ----- 进度条（按量程映射 0..100） ----- */
    lv_obj_t * bar = lv_bar_create(card);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 6);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    ctx->cards[idx].bar = bar;
}

/* 判定单路传感器状态（基于阈值带） */
static sensor_status_t compute_status(const sensor_meta_t * m, float v)
{
    if(v < m->warn_lo || v > m->warn_hi) return SENSOR_STATUS_ALARM;
    if((v >= m->warn_lo && v < m->safe_lo) || (v > m->safe_hi && v <= m->warn_hi))
        return SENSOR_STATUS_WARN;
    return SENSOR_STATUS_NORMAL;
}

static lv_color_t status_color(sensor_status_t st)
{
    switch(st) {
        case SENSOR_STATUS_WARN:  return lv_color_hex(COLOR_WARN);
        case SENSOR_STATUS_ALARM: return lv_color_hex(COLOR_ALARM);
        default:                  return lv_color_hex(COLOR_ACCENT);
    }
}

/* 刷新所有卡片：读传感器 → 数值/进度条/三态配色/光照发光 */
static void update_sensors(sensor_page_ctx_t * ctx)
{
    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        float v = 0.0f;
        if(!app_action_sensor_read((sensor_idx_t)i, &v)) continue;

        const sensor_meta_t * m = &g_sensors[i];
        sensor_card_t * c = &ctx->cards[i];

        /* 数值文本 */
        char buf[16];
        snprintf(buf, sizeof(buf), "%.*f", m->dec, v);
        lv_label_set_text(c->value, buf);

        /* 进度条：量程映射到 0..100 */
        int pct = (int)((v - m->min) / (m->max - m->min) * 100.0f + 0.5f);
        if(pct < 0) pct = 0;
        if(pct > 100) pct = 100;
        lv_bar_set_value(c->bar, pct, LV_ANIM_OFF);

        /* 三态配色 */
        sensor_status_t st = compute_status(m, v);
        lv_color_t col = status_color(st);

        lv_obj_set_style_bg_color(c->dot, col, 0);
        lv_obj_set_style_bg_color(c->bar, col, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(c->icon_badge, col, 0);
        lv_obj_set_style_text_color(c->icon, col, 0);

        /* 正常时数值为白色、边框为暗色；越限时跟随状态色 */
        lv_obj_set_style_text_color(c->value,
            (st == SENSOR_STATUS_NORMAL) ? lv_color_hex(COLOR_TEXT) : col, 0);
        lv_obj_set_style_border_color(c->card,
            (st == SENSOR_STATUS_NORMAL) ? lv_color_hex(COLOR_BORDER) : col, 0);

        /* 光照卡片：值越大，暖黄光晕越亮（0% 无光 → 100% 强光） */
        if(m->is_light) {
            int opa = (int)(v / 100.0f * 180.0f);
            if(opa < 0) opa = 0;
            if(opa > 255) opa = 255;
            lv_obj_set_style_shadow_color(c->card, lv_color_hex(COLOR_WARN), 0);
            lv_obj_set_style_shadow_width(c->card, 20, 0);
            lv_obj_set_style_shadow_ofs_y(c->card, 0, 0);
            lv_obj_set_style_shadow_opa(c->card, (lv_opa_t)opa, 0);
        }
    }
}

static void timer_cb(lv_timer_t * timer)
{
    sensor_page_ctx_t * ctx = lv_timer_get_user_data(timer);
    if(ctx) update_sensors(ctx);

    /* 驱动 Paho MQTT 接收循环: ARM 上异步线程常不工作, 必须外部 yield。
     * 每 1s 调一次, MQTT 数据 3s 来一帧, 每帧至少 catch 一次。 */
    app_mqtt_yield();

    /* MQTT 断线重连: 每 10 秒检查一次 (1s 定时器计数) */
    static int tick = 0;
    if(++tick >= 10) {
        tick = 0;
        app_mqtt_ensure_connected();
    }
}

static void live_dot_glow_cb(void * var, int32_t v)
{
    lv_obj_set_style_shadow_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void on_back_click(lv_event_t * e)
{
    sensor_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->back_cb) ctx->back_cb();
}

static void on_page_delete(lv_event_t * e)
{
    sensor_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx == NULL) return;
    if(ctx->timer) lv_timer_delete(ctx->timer);
    lv_free(ctx);
}
