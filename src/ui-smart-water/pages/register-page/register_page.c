// ========== register_page.c ==========
#include "register_page.h"
#include "../app_fonts.h"
#include "../app_keyboard.h"
#include "../app_popup.h"
#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#define NO_SCROLL(obj) \
    lv_obj_set_scrollbar_mode((obj), LV_SCROLLBAR_MODE_OFF); \
    lv_obj_clear_flag((obj), LV_OBJ_FLAG_SCROLLABLE)

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t * screen;
    lv_obj_t * user_ta;
    lv_obj_t * pass_ta;
    register_submit_cb_t submit_cb;
    navigate_back_cb_t   back_cb;
} register_page_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_bg_click(lv_event_t * e);
static void on_ta_click(lv_event_t * e);
static void on_register_click(lv_event_t * e);
static void on_back_click(lv_event_t * e);
static void on_page_delete(lv_event_t * e);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * register_page_create(register_submit_cb_t submit_cb, navigate_back_cb_t back_cb)
{
    register_page_ctx_t * ctx = lv_malloc(sizeof(register_page_ctx_t));
    if(ctx == NULL) return NULL;
    memset(ctx, 0, sizeof(register_page_ctx_t));
    ctx->submit_cb = submit_cb;
    ctx->back_cb   = back_cb;

    /* Create screen */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(screen);
    ctx->screen = screen;

    lv_obj_add_event_cb(screen, on_page_delete, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(screen, on_bg_click, LV_EVENT_CLICKED, screen);

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

    /* Logo circle */
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
    lv_label_set_text(logo_icon, "\xEF\x88\xB4");  /* fa-user-plus */

    /* Title */
    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "账号注册");
    lv_obj_set_style_text_font(title, app_font_kaiti_24(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6F2EE), 0);

    /* Subtitle */
    lv_obj_t * sub = lv_label_create(header);
    lv_label_set_text(sub, "CREATE  ACCOUNT");
    lv_obj_set_style_text_font(sub, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x5A7A72), 0);

    /* ===== FORM ===== */
    lv_obj_t * form = lv_obj_create(screen);
    lv_obj_remove_style_all(form);
    lv_obj_set_size(form, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(form, 1);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(form, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(form, 8, 0);
    lv_obj_set_style_pad_row(form, 16, 0);
    NO_SCROLL(form);

    /* ---- Account row ---- */
    lv_obj_t * acc_row = lv_obj_create(form);
    lv_obj_remove_style_all(acc_row);
    lv_obj_set_size(acc_row, 360, 48);
    lv_obj_set_style_bg_color(acc_row, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(acc_row, 1, 0);
    lv_obj_set_style_border_color(acc_row, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_radius(acc_row, 12, 0);
    lv_obj_set_style_pad_hor(acc_row, 16, 0);
    lv_obj_set_flex_flow(acc_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(acc_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(acc_row, 0, 0);
    lv_obj_set_style_pad_column(acc_row, 12, 0);
    NO_SCROLL(acc_row);

    lv_obj_t * acc_icon = lv_label_create(acc_row);
    lv_obj_set_width(acc_icon, 24);
    lv_obj_set_style_text_font(acc_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(acc_icon, lv_color_hex(0x5A7A72), 0);
    lv_label_set_text(acc_icon, "\xEF\x80\x87");  /* fa-user */

    ctx->user_ta = lv_textarea_create(acc_row);
    lv_obj_set_size(ctx->user_ta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ctx->user_ta, 1);
    lv_obj_set_style_border_width(ctx->user_ta, 0, 0);
    lv_obj_set_style_bg_color(ctx->user_ta, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->user_ta, LV_OPA_0, 0);
    lv_obj_set_style_text_color(ctx->user_ta, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(ctx->user_ta, app_font_kaiti_18(), 0);
    lv_obj_set_style_radius(ctx->user_ta, 0, 0);
    lv_obj_set_style_pad_all(ctx->user_ta, 0, 0);
    lv_textarea_set_one_line(ctx->user_ta, true);
    lv_textarea_set_placeholder_text(ctx->user_ta, "设置账号");
    lv_obj_set_style_text_color(ctx->user_ta, lv_color_hex(0x3E524C), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_textarea_set_max_length(ctx->user_ta, 32);
    NO_SCROLL(ctx->user_ta);
    lv_obj_add_event_cb(ctx->user_ta, on_ta_click, LV_EVENT_CLICKED, (void *)"账号");

    /* ---- Password row ---- */
    lv_obj_t * pass_row = lv_obj_create(form);
    lv_obj_remove_style_all(pass_row);
    lv_obj_set_size(pass_row, 360, 48);
    lv_obj_set_style_bg_color(pass_row, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(pass_row, 1, 0);
    lv_obj_set_style_border_color(pass_row, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_radius(pass_row, 12, 0);
    lv_obj_set_style_pad_hor(pass_row, 16, 0);
    lv_obj_set_flex_flow(pass_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pass_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(pass_row, 0, 0);
    lv_obj_set_style_pad_column(pass_row, 12, 0);
    NO_SCROLL(pass_row);

    lv_obj_t * pass_icon = lv_label_create(pass_row);
    lv_obj_set_width(pass_icon, 24);
    lv_obj_set_style_text_font(pass_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(pass_icon, lv_color_hex(0x5A7A72), 0);
    lv_label_set_text(pass_icon, "\xEF\x80\xA3");  /* fa-lock */

    ctx->pass_ta = lv_textarea_create(pass_row);
    lv_obj_set_size(ctx->pass_ta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ctx->pass_ta, 1);
    lv_obj_set_style_border_width(ctx->pass_ta, 0, 0);
    lv_obj_set_style_bg_color(ctx->pass_ta, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->pass_ta, LV_OPA_0, 0);
    lv_obj_set_style_text_color(ctx->pass_ta, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(ctx->pass_ta, app_font_kaiti_18(), 0);
    lv_obj_set_style_radius(ctx->pass_ta, 0, 0);
    lv_obj_set_style_pad_all(ctx->pass_ta, 0, 0);
    lv_textarea_set_one_line(ctx->pass_ta, true);
    lv_textarea_set_password_mode(ctx->pass_ta, true);
    lv_textarea_set_password_bullet(ctx->pass_ta, "*");
    lv_textarea_set_placeholder_text(ctx->pass_ta, "设置密码");
    lv_obj_set_style_text_color(ctx->pass_ta, lv_color_hex(0x3E524C), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_textarea_set_max_length(ctx->pass_ta, 32);
    NO_SCROLL(ctx->pass_ta);
    lv_obj_add_event_cb(ctx->pass_ta, on_ta_click, LV_EVENT_CLICKED, (void *)"密码");

    /* ---- Button row ---- */
    lv_obj_t * btn_row = lv_obj_create(form);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, 360, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 16, 0);
    lv_obj_set_style_pad_top(btn_row, 8, 0);
    NO_SCROLL(btn_row);

    /* Back button */
    lv_obj_t * back_btn2 = lv_button_create(btn_row);
    lv_obj_set_size(back_btn2, 110, 44);
    lv_obj_set_style_radius(back_btn2, 12, 0);
    lv_obj_set_style_bg_color(back_btn2, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(back_btn2, 1, 0);
    lv_obj_set_style_border_color(back_btn2, lv_color_hex(0x1C2E36), 0);
    NO_SCROLL(back_btn2);
    lv_obj_add_event_cb(back_btn2, on_back_click, LV_EVENT_CLICKED, ctx);

    lv_obj_t * back_lbl = lv_label_create(back_btn2);
    lv_label_set_text(back_lbl, "返 回");
    lv_obj_set_style_text_font(back_lbl, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x9AB8B0), 0);

    /* Register button */
    lv_obj_t * reg_btn = lv_button_create(btn_row);
    lv_obj_set_flex_grow(reg_btn, 1);
    lv_obj_set_height(reg_btn, 44);
    lv_obj_set_style_radius(reg_btn, 12, 0);
    lv_obj_set_style_bg_color(reg_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_bg_grad_color(reg_btn, lv_color_hex(0x0288D1), 0);
    lv_obj_set_style_bg_grad_dir(reg_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(reg_btn, 0, 0);
    lv_obj_set_style_shadow_width(reg_btn, 12, 0);
    lv_obj_set_style_shadow_color(reg_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(reg_btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(reg_btn, 4, 0);
    NO_SCROLL(reg_btn);
    lv_obj_add_event_cb(reg_btn, on_register_click, LV_EVENT_CLICKED, ctx);

    lv_obj_t * reg_lbl = lv_label_create(reg_btn);
    lv_label_set_text(reg_lbl, "注 册");
    lv_obj_set_style_text_font(reg_lbl, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(reg_lbl, lv_color_white(), 0);

    /* ---- Hint ---- */
    lv_obj_t * hint = lv_label_create(form);
    lv_label_set_text(hint, "注册成功后返回登录页");
    lv_obj_set_style_text_font(hint, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x3E524C), 0);

    return screen;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void on_bg_click(lv_event_t * e)
{
    lv_obj_t * target = lv_event_get_target(e);
    lv_obj_t * screen = lv_event_get_user_data(e);
    if(target == screen) {
        app_keyboard_hide();
    }
}

static void on_ta_click(lv_event_t * e)
{
    lv_obj_t * ta   = lv_event_get_target(e);
    const char * label = (const char *)lv_event_get_user_data(e);

    /* Walk up to find the screen */
    lv_obj_t * parent = ta;
    while(lv_obj_get_parent(parent) != NULL) {
        parent = lv_obj_get_parent(parent);
    }
    lv_obj_t * screen = parent;
    app_keyboard_show(screen, ta, label);
}

static void on_register_click(lv_event_t * e)
{
    register_page_ctx_t * ctx = lv_event_get_user_data(e);
    app_keyboard_hide();

    const char * username = lv_textarea_get_text(ctx->user_ta);
    const char * password = lv_textarea_get_text(ctx->pass_ta);

    if(username == NULL || username[0] == '\0' ||
       password == NULL || password[0] == '\0')
    {
        app_popup_show(ctx->screen, "账号或密码不能为空", APP_POPUP_ERROR);
        return;
    }

    if(ctx->submit_cb && ctx->submit_cb(username, password)) {
        app_popup_show(ctx->screen, "注册成功", APP_POPUP_SUCCESS);
    }
    else {
        app_popup_show(ctx->screen, "注册失败", APP_POPUP_ERROR);
    }
}

static void on_back_click(lv_event_t * e)
{
    register_page_ctx_t * ctx = lv_event_get_user_data(e);
    app_keyboard_hide();
    if(ctx->back_cb) {
        ctx->back_cb();
    }
}

static void on_page_delete(lv_event_t * e)
{
    register_page_ctx_t * ctx = lv_event_get_user_data(e);
    lv_free(ctx);
}
