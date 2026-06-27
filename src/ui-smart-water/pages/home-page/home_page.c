// ========== home_page.c ==========
#include "home_page.h"
#include "../app_fonts.h"
#include "../app_actions.h"
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

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t * screen;
    lv_obj_t * wifi_label;     /* WiFi status icon, top-right */
    lv_timer_t * wifi_timer;   /* Periodic network check timer */
    bool        wifi_first_done; /* Switches period after first check */
    home_nav_to_video_cb_t    nav_to_video_cb;
    home_nav_to_gallery_cb_t  nav_to_gallery_cb;
    home_nav_to_network_cb_t  nav_to_network_cb;
} home_page_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_video_btn_click(lv_event_t * e);
static void on_gallery_btn_click(lv_event_t * e);
static void on_network_btn_click(lv_event_t * e);
static void on_page_delete(lv_event_t * e);
static void wifi_check_timer_cb(lv_timer_t * timer);
static void wifi_glow_anim_cb(void * var, int32_t v);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * home_page_create(home_nav_to_video_cb_t    nav_to_video_cb,
                            home_nav_to_gallery_cb_t  nav_to_gallery_cb,
                            home_nav_to_network_cb_t  nav_to_network_cb)
{
    home_page_ctx_t * ctx = lv_malloc(sizeof(home_page_ctx_t));
    if(ctx == NULL) return NULL;
    memset(ctx, 0, sizeof(home_page_ctx_t));
    ctx->nav_to_video_cb    = nav_to_video_cb;
    ctx->nav_to_gallery_cb  = nav_to_gallery_cb;
    ctx->nav_to_network_cb  = nav_to_network_cb;

    /* ===== SCREEN ===== */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(screen);
    ctx->screen = screen;

    lv_obj_add_event_cb(screen, on_page_delete, LV_EVENT_DELETE, ctx);

    /* ===== HEADER ===== */
    lv_obj_t * header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(header, 6, 0);
    lv_obj_set_style_pad_top(header, 36, 0);
    lv_obj_set_style_pad_bottom(header, 16, 0);
    NO_SCROLL(header);

    /* ----- WiFi badge (top-right, equal margins, animated glow) ----- */
    {
        lv_obj_t * badge = lv_obj_create(screen);
        lv_obj_set_size(badge, 38, 38);
        lv_obj_set_style_radius(badge, 12, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(0x0A1620), 0);
        lv_obj_set_style_border_width(badge, 1, 0);
        lv_obj_set_style_border_color(badge, lv_color_hex(0x1C2E36), 0);
        /* Glow shadow — animated by wifi_glow_anim_cb */
        lv_obj_set_style_shadow_width(badge, 16, 0);
        lv_obj_set_style_shadow_color(badge, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_shadow_opa(badge, 0, 0);
        lv_obj_set_style_shadow_ofs_y(badge, 0, 0);
        lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        /* 16px from top, 16px from right */
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -16, 16);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_FLOATING);
        NO_SCROLL(badge);

        ctx->wifi_label = lv_label_create(badge);
        lv_obj_set_style_text_font(ctx->wifi_label, app_font_fa6_20(), 0);
        lv_obj_set_style_text_color(ctx->wifi_label, lv_color_hex(0x5A5A5A), 0);
        lv_label_set_text(ctx->wifi_label, "\xEF\x87\xAB");  /* fa-wifi */

        /* Pulse animation on shadow opacity: 0 → 70 → 0 → ... */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, badge);
        lv_anim_set_exec_cb(&a, wifi_glow_anim_cb);
        lv_anim_set_values(&a, 0, 179);        /* LV_OPA_0 → LV_OPA_70 */
        lv_anim_set_duration(&a, 1200);
        lv_anim_set_playback_duration(&a, 1200);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    /* Logo circle (fish) */
    lv_obj_t * logo = lv_obj_create(header);
    lv_obj_set_size(logo, 56, 56);
    lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(logo, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_bg_grad_color(logo, lv_color_hex(0x0288D1), 0);
    lv_obj_set_style_bg_grad_dir(logo, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(logo, 0, 0);
    lv_obj_set_style_shadow_width(logo, 24, 0);
    lv_obj_set_style_shadow_color(logo, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(logo, LV_OPA_40, 0);
    lv_obj_set_flex_flow(logo, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(logo, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(logo);

    lv_obj_t * logo_icon = lv_label_create(logo);
    lv_obj_set_style_text_font(logo_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(logo_icon, lv_color_white(), 0);
    lv_label_set_text(logo_icon, "\xEF\x95\xB8");  /* fa-fish */

    /* Title */
    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "智能水产养殖系统");
    lv_obj_set_style_text_font(title, app_font_kaiti_24(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6F2EE), 0);

    /* Subtitle */
    lv_obj_t * sub = lv_label_create(header);
    lv_label_set_text(sub, "AQUACULTURE  CONTROL");
    lv_obj_set_style_text_font(sub, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x5A7A72), 0);

    /* ===== BODY (flex-grow fills remaining space) ===== */
    lv_obj_t * body = lv_obj_create(screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 28, 0);
    NO_SCROLL(body);

    /* Welcome card */
    lv_obj_t * card = lv_obj_create(body);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 32, 0);
    lv_obj_set_style_pad_hor(card, 48, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 12, 0);
    NO_SCROLL(card);

    /* Wave emoji as icon */
    lv_obj_t * wave_icon = lv_label_create(card);
    lv_obj_set_style_text_font(wave_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(wave_icon, lv_color_hex(0x00D4AA), 0);
    lv_label_set_text(wave_icon, "\xEF\x9B\x83");  /* fa-water */

    /* Welcome text */
    lv_obj_t * welcome = lv_label_create(card);
    lv_label_set_text(welcome, "欢迎使用智慧水产管理系统");
    lv_obj_set_style_text_font(welcome, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(welcome, lv_color_hex(0xE0E0E0), 0);

    /* Sub text */
    lv_obj_t * welcome_sub = lv_label_create(card);
    lv_label_set_text(welcome_sub, "实时监控 · 智能预警 · 远程控制");
    lv_obj_set_style_text_font(welcome_sub, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(welcome_sub, lv_color_hex(0x5A7A72), 0);

    /* Button row */
    lv_obj_t * btn_row = lv_obj_create(body);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn_row, 16, 0);
    lv_obj_set_style_pad_column(btn_row, 16, 0);
    NO_SCROLL(btn_row);

    /* Video button */
    lv_obj_t * video_btn = lv_button_create(btn_row);
    lv_obj_set_size(video_btn, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_pad_hor(video_btn, 36, 0);
    lv_obj_set_style_radius(video_btn, 12, 0);
    lv_obj_set_style_bg_color(video_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_bg_grad_color(video_btn, lv_color_hex(0x0288D1), 0);
    lv_obj_set_style_bg_grad_dir(video_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(video_btn, 0, 0);
    lv_obj_set_style_shadow_width(video_btn, 12, 0);
    lv_obj_set_style_shadow_color(video_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(video_btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(video_btn, 4, 0);
    lv_obj_set_flex_flow(video_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(video_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(video_btn, 10, 0);
    NO_SCROLL(video_btn);
    lv_obj_add_event_cb(video_btn, on_video_btn_click, LV_EVENT_CLICKED, ctx);

    /* Play icon on button */
    lv_obj_t * play_icon = lv_label_create(video_btn);
    lv_obj_set_style_text_font(play_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(play_icon, lv_color_white(), 0);
    lv_label_set_text(play_icon, "\xEF\x81\x8B");  /* fa-play */

    /* Button text */
    lv_obj_t * btn_text = lv_label_create(video_btn);
    lv_label_set_text(btn_text, "视频监控");
    lv_obj_set_style_text_font(btn_text, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(btn_text, lv_color_white(), 0);

    /* Gallery button */
    lv_obj_t * gallery_btn = lv_button_create(btn_row);
    lv_obj_set_size(gallery_btn, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_pad_hor(gallery_btn, 36, 0);
    lv_obj_set_style_radius(gallery_btn, 12, 0);
    lv_obj_set_style_bg_color(gallery_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_bg_grad_color(gallery_btn, lv_color_hex(0x0288D1), 0);
    lv_obj_set_style_bg_grad_dir(gallery_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(gallery_btn, 0, 0);
    lv_obj_set_style_shadow_width(gallery_btn, 12, 0);
    lv_obj_set_style_shadow_color(gallery_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(gallery_btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(gallery_btn, 4, 0);
    lv_obj_set_flex_flow(gallery_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gallery_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(gallery_btn, 10, 0);
    NO_SCROLL(gallery_btn);
    lv_obj_add_event_cb(gallery_btn, on_gallery_btn_click, LV_EVENT_CLICKED, ctx);

    /* Image icon on button */
    lv_obj_t * img_icon = lv_label_create(gallery_btn);
    lv_obj_set_style_text_font(img_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(img_icon, lv_color_white(), 0);
    lv_label_set_text(img_icon, "\xEF\x80\x83");  /* fa-image */

    /* Button text */
    lv_obj_t * gallery_text = lv_label_create(gallery_btn);
    lv_label_set_text(gallery_text, "图片浏览");
    lv_obj_set_style_text_font(gallery_text, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(gallery_text, lv_color_white(), 0);

    /* Network button */
    lv_obj_t * network_btn = lv_button_create(btn_row);
    lv_obj_set_size(network_btn, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_pad_hor(network_btn, 36, 0);
    lv_obj_set_style_radius(network_btn, 12, 0);
    lv_obj_set_style_bg_color(network_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_bg_grad_color(network_btn, lv_color_hex(0x0288D1), 0);
    lv_obj_set_style_bg_grad_dir(network_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(network_btn, 0, 0);
    lv_obj_set_style_shadow_width(network_btn, 12, 0);
    lv_obj_set_style_shadow_color(network_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(network_btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(network_btn, 4, 0);
    lv_obj_set_flex_flow(network_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(network_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(network_btn, 10, 0);
    NO_SCROLL(network_btn);
    lv_obj_add_event_cb(network_btn, on_network_btn_click, LV_EVENT_CLICKED, ctx);

    /* Network icon on button */
    lv_obj_t * net_icon = lv_label_create(network_btn);
    lv_obj_set_style_text_font(net_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(net_icon, lv_color_white(), 0);
    lv_label_set_text(net_icon, "\xEF\x9B\xBF");  /* fa-network-wired */

    /* Button text */
    lv_obj_t * network_text = lv_label_create(network_btn);
    lv_label_set_text(network_text, "网络通讯");
    lv_obj_set_style_text_font(network_text, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(network_text, lv_color_white(), 0);

    /* ===== FOOTER ===== */
    lv_obj_t * footer = lv_obj_create(screen);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(footer, 10, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(footer);

    lv_obj_t * footer_text = lv_label_create(footer);
    lv_label_set_text(footer_text, "Smart Water Aquaculture System v1.0");
    lv_obj_set_style_text_font(footer_text, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(footer_text, lv_color_hex(0x5A7A72), 0);

    /* ----- WiFi status checker -----
     * First check at 2 s, then every 15 s to keep UI freezes minimal.
     * Each ping has a 2-s deadline so worst-case block is ~2 s per check. */
    ctx->wifi_first_done = false;
    ctx->wifi_timer = lv_timer_create(wifi_check_timer_cb, 2000, ctx);

    return screen;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* Animation callback: pulse the WiFi badge shadow glow */
static void wifi_glow_anim_cb(void * var, int32_t v)
{
    lv_obj_set_style_shadow_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void wifi_check_timer_cb(lv_timer_t * timer)
{
    home_page_ctx_t * ctx = lv_timer_get_user_data(timer);

    wifi_status_t status = app_action_check_wifi();

    lv_color_t color;
    switch (status) {
        case WIFI_STATUS_GREEN:
            color = lv_color_hex(0x00FF88);
            break;
        case WIFI_STATUS_YELLOW:
            color = lv_color_hex(0xFFB800);
            break;
        case WIFI_STATUS_RED:
            color = lv_color_hex(0xFF4455);
            break;
        default:
            /* Unknown — keep the initial gray */
            color = lv_color_hex(0x5A5A5A);
            break;
    }

    lv_obj_set_style_text_color(ctx->wifi_label, color, 0);

    /* After the first check, slow down to every 15 seconds */
    if (!ctx->wifi_first_done) {
        ctx->wifi_first_done = true;
        lv_timer_set_period(timer, 15000);
    }
}

static void on_video_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->nav_to_video_cb) {
        ctx->nav_to_video_cb();
    }
}

static void on_gallery_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->nav_to_gallery_cb) {
        ctx->nav_to_gallery_cb();
    }
}

static void on_network_btn_click(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->nav_to_network_cb) {
        ctx->nav_to_network_cb();
    }
}

static void on_page_delete(lv_event_t * e)
{
    home_page_ctx_t * ctx = lv_event_get_user_data(e);
    if (ctx->wifi_timer) {
        lv_timer_delete(ctx->wifi_timer);
    }
    lv_free(ctx);
}
