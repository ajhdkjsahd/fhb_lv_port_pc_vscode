// ========== home_page.c ==========
// 首页 —— 海洋沉浸式导航枢纽（深海蓝 + 青色霓虹）
// 元素：深海渐变背景 / 顶部光晕 / 玻璃欢迎卡 / 上浮气泡 / 游动鱼影 /
//       双层 canvas 波浪 / 浮动导航 dock（5 药丸）/ 顶部状态条（网络在线检测）
#include "home_page.h"
#include "../app_fonts.h"
#include "../app_actions.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/*********************
 *      DEFINES
 *********************/
#ifndef NO_SCROLL
#define NO_SCROLL(obj) \
    lv_obj_set_scrollbar_mode((obj), LV_SCROLLBAR_MODE_OFF); \
    lv_obj_clear_flag((obj), LV_OBJ_FLAG_SCROLLABLE)
#endif

/* 深海 + 霓虹配色 */
#define C_ABYSS1   0x02060E   /* 底部最深 */
#define C_ABYSS2   0x051428
#define C_ABYSS3   0x0A2640   /* 顶部较亮 */
#define C_NEON     0x00E5FF
#define C_TEAL     0x00D4AA
#define C_BLUE     0x0288D1
#define C_WARN     0xFFB800
#define C_ALARM    0xFF4455
#define C_TEXT     0xE6F2EE
#define C_TEXT2    0x8FB8C0
#define C_TEXT3    0x4A6A72

#define WAVE_W     800
#define WAVE_H     80
#define BUBBLE_N   12

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t * screen;
    lv_obj_t * status_dot;        /* 顶部状态点（颜色随网络状态） */
    lv_obj_t * status_text;       /* 顶部状态文字 */
    lv_obj_t * wave_canvas;       /* 底部波浪画布 */
    lv_timer_t * wifi_timer;      /* 网络状态轮询定时器 */
    lv_timer_t * wave_timer;      /* 波浪重绘定时器 */
    bool        wifi_first_done;
    home_nav_to_video_cb_t    nav_to_video_cb;
    home_nav_to_gallery_cb_t  nav_to_gallery_cb;
    home_nav_to_network_cb_t  nav_to_network_cb;
    home_nav_to_ai_chat_cb_t  nav_to_ai_chat_cb;
    home_nav_to_sensor_cb_t   nav_to_sensor_cb;
    home_nav_to_trend_cb_t    nav_to_trend_cb;
    home_nav_to_control_cb_t  nav_to_control_cb;
} home_page_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_video_btn_click(lv_event_t * e);
static void on_gallery_btn_click(lv_event_t * e);
static void on_network_btn_click(lv_event_t * e);
static void on_ai_chat_btn_click(lv_event_t * e);
static void on_sensor_btn_click(lv_event_t * e);
static void on_trend_btn_click(lv_event_t * e);
static void on_control_btn_click(lv_event_t * e);
static void on_page_delete(lv_event_t * e);
static void on_screen_load(lv_event_t * e);
static void on_screen_unload(lv_event_t * e);
static void wifi_check_timer_cb(lv_timer_t * timer);
static void wave_timer_cb(lv_timer_t * timer);
static void wave_redraw(lv_obj_t * canvas, int phase1, int phase2);
static void bubble_anim_cb(void * var, int32_t v);
static void fish_anim_cb(void * var, int32_t v);
static void logo_float_cb(void * var, int32_t v);
static void status_dot_pulse_cb(void * var, int32_t v);
static lv_obj_t * create_nav_pill(lv_obj_t * parent, const char * icon,
                                  const char * label, lv_event_cb_t cb,
                                  home_page_ctx_t * ctx);

/**********************
 *  STATIC VARIABLES
 **********************/
static uint8_t s_wave_buf[WAVE_W * WAVE_H * 4];   /* canvas 像素缓冲（ARGB8888） */
static int     s_phase1 = 0;                       /* 后层波浪相位 */
static int     s_phase2 = 0;                       /* 前层波浪相位 */

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * home_page_create(home_nav_to_video_cb_t    nav_to_video_cb,
                            home_nav_to_gallery_cb_t  nav_to_gallery_cb,
                            home_nav_to_network_cb_t  nav_to_network_cb,
                            home_nav_to_ai_chat_cb_t  nav_to_ai_chat_cb,
                            home_nav_to_sensor_cb_t   nav_to_sensor_cb,
                            home_nav_to_trend_cb_t    nav_to_trend_cb,
                            home_nav_to_control_cb_t  nav_to_control_cb)
{
    home_page_ctx_t * ctx = lv_malloc(sizeof(home_page_ctx_t));
    if(ctx == NULL) return NULL;
    memset(ctx, 0, sizeof(home_page_ctx_t));
    ctx->nav_to_video_cb    = nav_to_video_cb;
    ctx->nav_to_gallery_cb  = nav_to_gallery_cb;
    ctx->nav_to_network_cb  = nav_to_network_cb;
    ctx->nav_to_ai_chat_cb  = nav_to_ai_chat_cb;
    ctx->nav_to_sensor_cb   = nav_to_sensor_cb;
    ctx->nav_to_trend_cb    = nav_to_trend_cb;
    ctx->nav_to_control_cb  = nav_to_control_cb;

    /* ===== SCREEN（深海垂直渐变：上亮下暗，模拟光从水面射入） ===== */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(C_ABYSS3), 0);     /* 顶 */
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(C_ABYSS1), 0);/* 底 */
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    NO_SCROLL(screen);
    ctx->screen = screen;
    lv_obj_add_event_cb(screen, on_page_delete, LV_EVENT_DELETE, ctx);
    /* 屏幕生命周期：离开首页时暂停波浪/网络定时器，进入时恢复 */
    lv_obj_add_event_cb(screen, on_screen_load, LV_EVENT_SCREEN_LOAD_START, ctx);
    lv_obj_add_event_cb(screen, on_screen_unload, LV_EVENT_SCREEN_UNLOAD_START, ctx);

    /* ===== 顶部光晕（大圆，青色低透明，模拟光源） ===== */
    {
        lv_obj_t * glow = lv_obj_create(screen);
        lv_obj_remove_style_all(glow);
        lv_obj_set_size(glow, 360, 360);
        lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(glow, lv_color_hex(C_NEON), 0);
        lv_obj_set_style_bg_opa(glow, LV_OPA_10, 0);
        lv_obj_align(glow, LV_ALIGN_TOP_MID, 0, -200);  /* 一半在屏幕外 */
    }

    /* ===== 鱼（3 条，先于卡片/波浪创建 → 位于其后方） =====
     *  ⚠ 不使用 transform_scale：ARM 板 fbdev 渲染缩放的 label 字形时
     *     内部会产生越界坐标 → lv_draw_buf 访问 x=-1 → 段错误。
     *     改为同尺寸（fa6_20 天然大小），靠颜色/透明度/速度/深度区分。 */
    {
        struct { int y; int opa; uint32_t color; int dur; } fish_cfg[3] = {
            { 120, 140, C_TEAL, 18000 },
            { 220,  90, C_NEON, 26000 },
            { 305, 100, C_TEAL, 32000 },
        };
        for(int i = 0; i < 3; i++) {
            lv_obj_t * f = lv_label_create(screen);
            lv_obj_set_style_text_font(f, app_font_fa6_20(), 0);
            lv_obj_set_style_text_color(f, lv_color_hex(fish_cfg[i].color), 0);
            lv_obj_set_style_opa(f, fish_cfg[i].opa, 0);
            lv_label_set_text(f, "\xEF\x95\xB8");  /* fa-fish */
            lv_obj_set_user_data(f, (void *)(intptr_t)fish_cfg[i].y);  /* base_y */

            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, f);
            lv_anim_set_exec_cb(&a, fish_anim_cb);
            lv_anim_set_values(&a, 0, 1000);
            lv_anim_set_duration(&a, fish_cfg[i].dur);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_path_cb(&a, lv_anim_path_linear);
            lv_anim_start(&a);
        }
    }

    /* ===== 气泡（12 个，从底部上浮 + 淡出） ===== */
    {
        /* 用固定参数表保证可复现（LVGL 里无 rand 种子需求） */
        static const struct { int x; int size; int dur; int delay; } bcfg[BUBBLE_N] = {
            { 60,  6,  7000,    0}, {130, 10,  9000, 1200}, {200,  5,  6500,  600},
            {270,  8, 11000, 3000}, {340, 12,  8000, 1800}, {410,  6,  9500, 2400},
            {480,  9,  7200,  900}, {550,  5, 10500, 3600}, {620, 11,  8500, 1500},
            {690,  7,  9800, 2700}, {740,  6,  6800,  450}, {100,  9,  8800, 2100},
        };
        for(int i = 0; i < BUBBLE_N; i++) {
            lv_obj_t * b = lv_obj_create(screen);
            lv_obj_remove_style_all(b);
            lv_obj_set_size(b, bcfg[i].size, bcfg[i].size);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(b, 38, 0);  /* 15% */
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(C_NEON), 0);
            lv_obj_set_style_border_opa(b, LV_OPA_50, 0);
            lv_obj_set_style_shadow_width(b, 6, 0);
            lv_obj_set_style_shadow_color(b, lv_color_hex(C_NEON), 0);
            lv_obj_set_style_shadow_opa(b, LV_OPA_30, 0);
            lv_obj_set_pos(b, bcfg[i].x, 480);

            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, b);
            lv_anim_set_exec_cb(&a, bubble_anim_cb);
            lv_anim_set_values(&a, 0, 1000);
            lv_anim_set_duration(&a, bcfg[i].dur);
            lv_anim_set_delay(&a, bcfg[i].delay);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_path_cb(&a, lv_anim_path_linear);
            lv_anim_start(&a);
        }
    }

    /* ===== 玻璃欢迎卡 ===== */
    {
        lv_obj_t * card = lv_obj_create(screen);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 560, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(card, 22, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(C_ABYSS2), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_60, 0);
        lv_obj_set_style_bg_grad_color(card, lv_color_hex(C_ABYSS1), 0);
        lv_obj_set_style_bg_grad_opa(card, LV_OPA_50, 0);
        lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_NEON), 0);
        lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
        lv_obj_set_style_shadow_width(card, 50, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(C_NEON), 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
        lv_obj_set_style_pad_hor(card, 32, 0);
        lv_obj_set_style_pad_ver(card, 24, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(card, 8, 0);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 70);
        NO_SCROLL(card);

        /* logo 圆形（呼吸浮动） */
        lv_obj_t * logo = lv_obj_create(card);
        lv_obj_remove_style_all(logo);
        lv_obj_set_size(logo, 64, 64);
        lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(logo, lv_color_hex(C_TEAL), 0);
        lv_obj_set_style_bg_grad_color(logo, lv_color_hex(C_BLUE), 0);
        lv_obj_set_style_bg_grad_dir(logo, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(logo, 28, 0);
        lv_obj_set_style_shadow_color(logo, lv_color_hex(C_TEAL), 0);
        lv_obj_set_style_shadow_opa(logo, LV_OPA_50, 0);
        lv_obj_set_flex_flow(logo, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(logo, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        NO_SCROLL(logo);

        lv_obj_t * logo_icon = lv_label_create(logo);
        lv_obj_set_style_text_font(logo_icon, app_font_fa6_20(), 0);
        lv_obj_set_style_text_color(logo_icon, lv_color_white(), 0);
        lv_label_set_text(logo_icon, "\xEF\x95\xB8");  /* fa-fish */

        /* logo 浮动动画 */
        {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, logo);
            lv_anim_set_exec_cb(&a, logo_float_cb);
            lv_anim_set_values(&a, -5, 5);
            lv_anim_set_duration(&a, 2000);
            lv_anim_set_playback_duration(&a, 2000);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
            lv_anim_start(&a);
        }

        /* 标题 */
        lv_obj_t * title = lv_label_create(card);
        lv_label_set_text(title, "智能水产养殖系统");
        lv_obj_set_style_text_font(title, app_font_kaiti_24(), 0);
        lv_obj_set_style_text_color(title, lv_color_hex(C_TEXT), 0);

        /* 副标题 */
        lv_obj_t * sub = lv_label_create(card);
        lv_label_set_text(sub, "AQUACULTURE  CONTROL  SYSTEM");
        lv_obj_set_style_text_font(sub, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(C_TEXT3), 0);

        /* 分割线（青色渐变细线） */
        lv_obj_t * div = lv_obj_create(card);
        lv_obj_remove_style_all(div);
        lv_obj_set_size(div, 120, 2);
        lv_obj_set_style_radius(div, 1, 0);
        lv_obj_set_style_bg_color(div, lv_color_hex(C_NEON), 0);
        lv_obj_set_style_bg_opa(div, LV_OPA_50, 0);

        /* tagline 行：霓虹短语 + 暗点分隔 */
        lv_obj_t * tag_row = lv_obj_create(card);
        lv_obj_remove_style_all(tag_row);
        lv_obj_set_size(tag_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(tag_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tag_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(tag_row, 8, 0);
        NO_SCROLL(tag_row);

        const char * phrases[3] = { "实时监控", "智能预警", "远程控制" };
        for(int i = 0; i < 3; i++) {
            if(i > 0) {
                lv_obj_t * dot = lv_label_create(tag_row);
                lv_label_set_text(dot, "·");
                lv_obj_set_style_text_font(dot, app_font_kaiti_14(), 0);
                lv_obj_set_style_text_color(dot, lv_color_hex(C_TEXT3), 0);
            }
            lv_obj_t * ph = lv_label_create(tag_row);
            lv_label_set_text(ph, phrases[i]);
            lv_obj_set_style_text_font(ph, app_font_kaiti_14(), 0);
            lv_obj_set_style_text_color(ph, lv_color_hex(C_NEON), 0);
        }
    }

    /* ===== 底部双层波浪（canvas） ===== */
    {
        lv_obj_t * canvas = lv_canvas_create(screen);
        lv_canvas_set_buffer(canvas, s_wave_buf, WAVE_W, WAVE_H, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_align(canvas, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
        ctx->wave_canvas = canvas;
        wave_redraw(canvas, s_phase1, s_phase2);  /* 首帧 */

        ctx->wave_timer = lv_timer_create(wave_timer_cb, 40, ctx);
        lv_timer_pause(ctx->wave_timer);   /* 首页初始不可见，暂停波浪 */
    }

    /* ===== 浮动导航 dock（5 药丸） ===== */
    {
        lv_obj_t * dock = lv_obj_create(screen);
        lv_obj_remove_style_all(dock);
        lv_obj_set_size(dock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(dock, 18, 0);
        lv_obj_set_style_bg_color(dock, lv_color_hex(C_ABYSS1), 0);
        lv_obj_set_style_bg_opa(dock, LV_OPA_50, 0);
        lv_obj_set_style_border_width(dock, 1, 0);
        lv_obj_set_style_border_color(dock, lv_color_hex(C_NEON), 0);
        lv_obj_set_style_border_opa(dock, LV_OPA_20, 0);
        lv_obj_set_style_shadow_width(dock, 24, 0);
        lv_obj_set_style_shadow_color(dock, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(dock, LV_OPA_40, 0);
        lv_obj_set_style_pad_all(dock, 10, 0);
        lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(dock, 8, 0);
        lv_obj_align(dock, LV_ALIGN_BOTTOM_MID, 0, -100);  /* 浮在波浪上方 */
        NO_SCROLL(dock);

        create_nav_pill(dock, "\xEF\x81\x8B",            "视频监控", on_video_btn_click,   ctx); /* fa-play */
        create_nav_pill(dock, "\xEF\x80\x83",            "图片浏览", on_gallery_btn_click, ctx); /* fa-image */
        create_nav_pill(dock, "\xEF\x9B\xBF",            "网络通讯", on_network_btn_click, ctx); /* fa-network-wired */
        create_nav_pill(dock, "\xEF\x95\x84",            "AI助手",  on_ai_chat_btn_click, ctx); /* fa-robot */
        create_nav_pill(dock, "\xEF\x98\xA4",            "传感器",  on_sensor_btn_click,  ctx); /* fa-gauge-high */
        create_nav_pill(dock, "\xEF\x88\x81",            "趋势报表", on_trend_btn_click,   ctx); /* fa-chart-line */
        create_nav_pill(dock, "\xEF\x82\x85",            "闭环控制", on_control_btn_click, ctx); /* fa-gears */
    }

    /* ===== 顶部状态条 ===== */
    {
        /* 左：品牌缩写 */
        lv_obj_t * brand = lv_obj_create(screen);
        lv_obj_remove_style_all(brand);
        lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(brand, 6, 0);
        lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 22, 18);
        NO_SCROLL(brand);

        lv_obj_t * brand_icon = lv_label_create(brand);
        lv_obj_set_style_text_font(brand_icon, app_font_fa6_20(), 0);
        lv_obj_set_style_text_color(brand_icon, lv_color_hex(C_NEON), 0);
        lv_label_set_text(brand_icon, "\xEF\x9B\x83");  /* fa-water */

        lv_obj_t * brand_txt = lv_label_create(brand);
        lv_label_set_text(brand_txt, "SMART WATER");
        lv_obj_set_style_text_font(brand_txt, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(brand_txt, lv_color_hex(C_TEXT2), 0);

        /* 右：系统状态药丸（颜色随网络状态） */
        lv_obj_t * pill = lv_obj_create(screen);
        lv_obj_remove_style_all(pill);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, 26);
        lv_obj_set_style_pad_hor(pill, 12, 0);
        lv_obj_set_style_radius(pill, 13, 0);
        lv_obj_set_style_bg_color(pill, lv_color_hex(C_ABYSS2), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_60, 0);
        lv_obj_set_style_border_width(pill, 1, 0);
        lv_obj_set_style_border_color(pill, lv_color_hex(C_NEON), 0);
        lv_obj_set_style_border_opa(pill, LV_OPA_30, 0);
        lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pill, 6, 0);
        lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -22, 16);
        NO_SCROLL(pill);

        ctx->status_dot = lv_obj_create(pill);
        lv_obj_remove_style_all(ctx->status_dot);
        lv_obj_set_size(ctx->status_dot, 7, 7);
        lv_obj_set_style_radius(ctx->status_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ctx->status_dot, lv_color_hex(C_TEAL), 0);
        lv_obj_set_style_bg_opa(ctx->status_dot, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(ctx->status_dot, 8, 0);
        lv_obj_set_style_shadow_color(ctx->status_dot, lv_color_hex(C_TEAL), 0);
        lv_obj_set_style_shadow_opa(ctx->status_dot, 0, 0);

        ctx->status_text = lv_label_create(pill);
        lv_label_set_text(ctx->status_text, "检测中");
        lv_obj_set_style_text_font(ctx->status_text, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(ctx->status_text, lv_color_hex(C_NEON), 0);

        /* 状态点脉冲 */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ctx->status_dot);
        lv_anim_set_exec_cb(&a, status_dot_pulse_cb);
        lv_anim_set_values(&a, 0, 179);
        lv_anim_set_duration(&a, 1200);
        lv_anim_set_playback_duration(&a, 1200);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    /* ===== 网络状态定时器（首次 2s，之后 15s） ===== */
    ctx->wifi_first_done = false;
    ctx->wifi_timer = lv_timer_create(wifi_check_timer_cb, 2000, ctx);
    lv_timer_pause(ctx->wifi_timer);    /* 首页初始不可见，暂停网络检测 */

    return screen;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* 创建一个导航药丸（图标 + 文字，悬停上浮发光） */
static lv_obj_t * create_nav_pill(lv_obj_t * parent, const char * icon,
                                  const char * label, lv_event_cb_t cb,
                                  home_page_ctx_t * ctx)
{
    lv_obj_t * p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 108, 60);
    lv_obj_set_style_radius(p, 12, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(C_TEAL), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_10, 0);
    lv_obj_set_style_bg_grad_color(p, lv_color_hex(C_BLUE), 0);
    lv_obj_set_style_bg_grad_opa(p, LV_OPA_10, 0);
    lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(C_NEON), 0);
    lv_obj_set_style_border_opa(p, LV_OPA_40, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 4, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    /* 悬停：上浮 + 边框加亮 + 发光 */
    lv_obj_set_style_translate_y(p, -3, LV_STATE_HOVERED);
    lv_obj_set_style_border_opa(p, LV_OPA_COVER, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(p, LV_OPA_20, LV_STATE_HOVERED);
    lv_obj_set_style_shadow_width(p, 16, LV_STATE_HOVERED);
    lv_obj_set_style_shadow_color(p, lv_color_hex(C_NEON), LV_STATE_HOVERED);
    lv_obj_set_style_shadow_opa(p, LV_OPA_50, LV_STATE_HOVERED);
    /* 按下：加深 */
    lv_obj_set_style_bg_opa(p, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(p, 0, LV_STATE_PRESSED);

    lv_obj_add_event_cb(p, cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t * ic = lv_label_create(p);
    lv_obj_set_style_text_font(ic, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(C_NEON), 0);
    lv_label_set_text(ic, icon);

    lv_obj_t * txt = lv_label_create(p);
    lv_label_set_text(txt, label);
    lv_obj_set_style_text_font(txt, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(txt, lv_color_hex(C_TEXT), 0);

    return p;
}

/* 波浪重绘：直接写 ARGB8888 像素，两层正弦波叠加 */
static void wave_redraw(lv_obj_t * canvas, int phase1, int phase2)
{
    lv_color32_t * px = (lv_color32_t *)lv_canvas_get_buf(canvas);
    int W = WAVE_W, H = WAVE_H;

    /* 清空为透明 */
    for(int i = 0; i < W * H; i++) {
        px[i].red = 0; px[i].green = 0; px[i].blue = 0; px[i].alpha = 0;
    }

    /* 后层波浪（蓝 #0288D1，alpha 160） */
    for(int x = 0; x < W; x++) {
        int y = (int)(H * 0.50 + 7.0 * sin((x + phase1) * 0.020) + 3.0 * sin((x + phase1) * 0.055));
        if(y < 0) y = 0;
        if(y >= H) y = H - 1;
        for(int yy = y; yy < H; yy++) {
            px[yy * W + x].blue  = 0xD1;
            px[yy * W + x].green = 0x88;
            px[yy * W + x].red   = 0x02;
            px[yy * W + x].alpha = 160;
        }
    }

    /* 前层波浪（青 #00D4AA，alpha 220，更低更前） */
    for(int x = 0; x < W; x++) {
        int y = (int)(H * 0.64 + 9.0 * sin((x + phase2) * 0.028) + 4.0 * sin((x + phase2) * 0.060));
        if(y < 0) y = 0;
        if(y >= H) y = H - 1;
        for(int yy = y; yy < H; yy++) {
            px[yy * W + x].blue  = 0xAA;
            px[yy * W + x].green = 0xD4;
            px[yy * W + x].red   = 0x00;
            px[yy * W + x].alpha = 220;
        }
    }

    lv_obj_invalidate(canvas);
}

static void wave_timer_cb(lv_timer_t * timer)
{
    home_page_ctx_t * ctx = lv_timer_get_user_data(timer);
    if(ctx == NULL || ctx->wave_canvas == NULL) return;
    s_phase1 += 5;     /* 向右流动 */
    s_phase2 -= 7;     /* 反向，制造交错感 */
    wave_redraw(ctx->wave_canvas, s_phase1, s_phase2);
}

/* 气泡：v=0..1000，从底部上浮到顶部，淡入淡出 */
static void bubble_anim_cb(void * var, int32_t v)
{
    lv_obj_t * b = (lv_obj_t *)var;
    int y = 480 - (int)((v / 1000.0) * 480);   /* 480 → 0，始终在屏幕内 */
    if(y < -20) y = -20;                        /* 顶部余量防 clip 越界 */
    if(y > 520) y = 520;
    lv_obj_set_y(b, y);

    lv_opa_t opa;
    if(v < 100) opa = (lv_opa_t)(v * 2);                 /* 淡入 */
    else if(v > 850) opa = (lv_opa_t)((1000 - v) * 1.3); /* 淡出 */
    else opa = 200;
    if(opa > 200) opa = 200;
    lv_obj_set_style_opa(b, opa, 0);
}

/* 鱼：v=0..1000，从左游到右，带轻微上下摆尾 */
static void fish_anim_cb(void * var, int32_t v)
{
    lv_obj_t * f = (lv_obj_t *)var;
    int x = (int)((v / 1000.0) * 780);            /* 0 → 780，不越屏 */
    intptr_t base_y = (intptr_t)lv_obj_get_user_data(f);
    int y = (int)base_y + (int)(4.0 * sin(v * 0.015));
    lv_obj_set_pos(f, x, y);
}

static void logo_float_cb(void * var, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void status_dot_pulse_cb(void * var, int32_t v)
{
    lv_obj_set_style_shadow_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void wifi_check_timer_cb(lv_timer_t * timer)
{
    home_page_ctx_t * ctx = lv_timer_get_user_data(timer);

    wifi_status_t status = app_action_check_wifi();
    lv_color_t color;
    const char * txt;
    switch(status) {
        case WIFI_STATUS_GREEN:  color = lv_color_hex(C_TEAL);  txt = "系统在线"; break;
        case WIFI_STATUS_YELLOW: color = lv_color_hex(C_WARN);  txt = "局域网";   break;
        case WIFI_STATUS_RED:    color = lv_color_hex(C_ALARM); txt = "网络离线"; break;
        default:                 color = lv_color_hex(C_TEXT3); txt = "检测中";   break;
    }
    lv_obj_set_style_bg_color(ctx->status_dot, color, 0);
    lv_obj_set_style_shadow_color(ctx->status_dot, color, 0);
    lv_obj_set_style_text_color(ctx->status_text, color, 0);
    lv_label_set_text(ctx->status_text, txt);

    /* 首次后降频到 15s，避免阻塞主循环 */
    if(!ctx->wifi_first_done) {
        ctx->wifi_first_done = true;
        lv_timer_set_period(timer, 15000);
    }
}

/* 首页变为活跃屏幕 → 恢复波浪重绘 + 网络检测 */
static void on_screen_load(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx == NULL) return;
    if(ctx->wave_timer) lv_timer_resume(ctx->wave_timer);
    if(ctx->wifi_timer) lv_timer_resume(ctx->wifi_timer);
}

/* 首页离开活跃状态 → 暂停波浪重绘 + 网络检测（板子省电，PC 也不空转） */
static void on_screen_unload(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx == NULL) return;
    if(ctx->wave_timer) lv_timer_pause(ctx->wave_timer);
    if(ctx->wifi_timer) lv_timer_pause(ctx->wifi_timer);
}

static void on_video_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->nav_to_video_cb) ctx->nav_to_video_cb();
}

static void on_gallery_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->nav_to_gallery_cb) ctx->nav_to_gallery_cb();
}

static void on_network_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->nav_to_network_cb) ctx->nav_to_network_cb();
}

static void on_ai_chat_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->nav_to_ai_chat_cb) ctx->nav_to_ai_chat_cb();
}

static void on_sensor_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->nav_to_sensor_cb) ctx->nav_to_sensor_cb();
}

static void on_trend_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->nav_to_trend_cb) ctx->nav_to_trend_cb();
}

static void on_control_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx && ctx->nav_to_control_cb) ctx->nav_to_control_cb();
}

static void on_page_delete(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx == NULL) return;
    if(ctx->wifi_timer) lv_timer_delete(ctx->wifi_timer);
    if(ctx->wave_timer) lv_timer_delete(ctx->wave_timer);
    lv_free(ctx);
}
