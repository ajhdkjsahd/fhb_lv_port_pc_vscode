// ========== app_popup.c ==========
#include "app_popup.h"
#include "app_fonts.h"

/*********************
 *      DEFINES
 *********************/
#define NO_SCROLL(obj) \
    lv_obj_set_scrollbar_mode((obj), LV_SCROLLBAR_MODE_OFF); \
    lv_obj_clear_flag((obj), LV_OBJ_FLAG_SCROLLABLE)

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * g_popup_overlay = NULL;
static lv_timer_t * g_dismiss_timer = NULL;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_overlay_click(lv_event_t * e);
static void on_dismiss_timer(lv_timer_t * tmr);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void app_popup_show(lv_obj_t * screen, const char * text, app_popup_type_t type)
{
    /* Dismiss existing popup first */
    app_popup_dismiss();

    /* Create semi-transparent overlay */
    g_popup_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_popup_overlay);
    lv_obj_set_size(g_popup_overlay, 800, 480);
    lv_obj_align(g_popup_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(g_popup_overlay, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_bg_opa(g_popup_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(g_popup_overlay, 0, 0);
    lv_obj_set_style_radius(g_popup_overlay, 0, 0);
    NO_SCROLL(g_popup_overlay);

    /* Click overlay to dismiss */
    lv_obj_add_event_cb(g_popup_overlay, on_overlay_click, LV_EVENT_CLICKED, NULL);

    /* Card container */
    lv_obj_t * card = lv_obj_create(g_popup_overlay);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 260, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_shadow_ofs_y(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 12, 0);
    NO_SCROLL(card);

    /* Icon circle */
    lv_obj_t * icon_circle = lv_obj_create(card);
    lv_obj_set_size(icon_circle, 48, 48);
    lv_obj_set_style_radius(icon_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(icon_circle, 0, 0);
    lv_obj_set_flex_flow(icon_circle, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_circle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(icon_circle);

    lv_obj_t * icon_lbl = lv_label_create(icon_circle);
    lv_obj_set_style_text_font(icon_lbl, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);

    if(type == APP_POPUP_SUCCESS) {
        lv_obj_set_style_bg_color(icon_circle, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_bg_opa(icon_circle, 38, 0);
        lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0x00D4AA), 0);
        lv_label_set_text(icon_lbl, "\xEF\x80\x8C");  /* fa-check */
    }
    else {
        lv_obj_set_style_bg_color(icon_circle, lv_color_hex(0xFF6B6B), 0);
        lv_obj_set_style_bg_opa(icon_circle, 38, 0);
        lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0xFF6B6B), 0);
        lv_label_set_text(icon_lbl, "\xEF\x80\x8D");  /* fa-xmark */
    }

    /* Main text */
    lv_obj_t * main_lbl = lv_label_create(card);
    lv_label_set_text(main_lbl, text);
    lv_obj_set_style_text_color(main_lbl, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(main_lbl, app_font_kaiti_18(), 0);

    /* Sub text */
    lv_obj_t * sub_lbl = lv_label_create(card);
    lv_label_set_text(sub_lbl, "2秒后自动消失 · 或点击关闭");
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x5A7A72), 0);
    lv_obj_set_style_text_font(sub_lbl, app_font_kaiti_14(), 0);

    /* Auto-dismiss after 2s */
    g_dismiss_timer = lv_timer_create(on_dismiss_timer, 2000, NULL);
    lv_timer_set_repeat_count(g_dismiss_timer, 1);
}

void app_popup_dismiss(void)
{
    if(g_dismiss_timer) {
        lv_timer_delete(g_dismiss_timer);
        g_dismiss_timer = NULL;
    }
    if(g_popup_overlay) {
        lv_obj_delete(g_popup_overlay);
        g_popup_overlay = NULL;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void on_overlay_click(lv_event_t * e)
{
    LV_UNUSED(e);
    app_popup_dismiss();
}

static void on_dismiss_timer(lv_timer_t * tmr)
{
    LV_UNUSED(tmr);
    g_dismiss_timer = NULL;
    if(g_popup_overlay) {
        lv_obj_delete(g_popup_overlay);
        g_popup_overlay = NULL;
    }
}
