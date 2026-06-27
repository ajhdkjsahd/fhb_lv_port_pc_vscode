// ========== network_page.c ==========
#include "network_page.h"
#include "../app_fonts.h"
#include "../app_keyboard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/*********************
 *      DEFINES
 *********************/
#ifndef NO_SCROLL
#define NO_SCROLL(obj) \
    lv_obj_set_scrollbar_mode((obj), LV_SCROLLBAR_MODE_OFF); \
    lv_obj_clear_flag((obj), LV_OBJ_FLAG_SCROLLABLE)
#endif

#define MAX_MSG_LINES     200    /* Max log lines before trimming oldest */
#define MAX_MSG_CHARS     256    /* Max chars per message line */
#define MSG_CONTAINER_PAD 8      /* Inner padding for message container */

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t * screen;

    /* Config row */
    lv_obj_t * ip_ta;
    lv_obj_t * port_ta;

    /* Connection state */
    lv_obj_t * conn_dot;
    lv_obj_t * conn_label;
    lv_obj_t * conn_btn;
    lv_obj_t * conn_btn_icon;   /* FA6 icon label */
    lv_obj_t * conn_btn_label;  /* Chinese text label */
    bool       is_connected;

    /* Message area */
    lv_obj_t * msg_container;   /* Scrollable flex column */
    int        msg_count;

    /* Input row */
    lv_obj_t * msg_input;
    lv_obj_t * send_btn;

    /* Callbacks */
    network_back_cb_t       back_cb;
    network_connect_cb_t    connect_cb;
    network_disconnect_cb_t disconnect_cb;
    network_send_cb_t       send_cb;
} network_page_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_back_click(lv_event_t * e);
static void on_conn_btn_click(lv_event_t * e);
static void on_send_click(lv_event_t * e);
static void on_clear_click(lv_event_t * e);
static void on_input_focused(lv_event_t * e);
static void on_page_delete(lv_event_t * e);
static void update_conn_ui(network_page_ctx_t * ctx, bool connected);
static void trim_oldest_msgs(network_page_ctx_t * ctx);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * network_page_create(network_back_cb_t       back_cb,
                               network_connect_cb_t    connect_cb,
                               network_disconnect_cb_t disconnect_cb,
                               network_send_cb_t       send_cb)
{
    network_page_ctx_t * ctx = lv_malloc(sizeof(network_page_ctx_t));
    if (ctx == NULL) return NULL;
    memset(ctx, 0, sizeof(network_page_ctx_t));
    ctx->back_cb       = back_cb;
    ctx->connect_cb    = connect_cb;
    ctx->disconnect_cb = disconnect_cb;
    ctx->send_cb       = send_cb;
    ctx->is_connected  = false;

    /* ===== SCREEN ===== */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(screen);
    ctx->screen = screen;

    /* Store ctx on screen directly for public API functions */
    lv_obj_set_user_data(screen, ctx);

    lv_obj_add_event_cb(screen, on_page_delete, LV_EVENT_DELETE, ctx);

    /* ===== TOP BAR ===== */
    {
        lv_obj_t * topbar = lv_obj_create(screen);
        lv_obj_remove_style_all(topbar);
        lv_obj_set_size(topbar, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(topbar, 12, 0);
        lv_obj_set_style_pad_hor(topbar, 16, 0);
        lv_obj_set_style_pad_column(topbar, 10, 0);
        NO_SCROLL(topbar);

        /* Back button */
        lv_obj_t * back_btn = lv_button_create(topbar);
        lv_obj_set_size(back_btn, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_pad_hor(back_btn, 14, 0);
        lv_obj_set_style_radius(back_btn, 8, 0);
        lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x0A1620), 0);
        lv_obj_set_style_border_width(back_btn, 1, 0);
        lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1C2E36), 0);
        lv_obj_set_flex_flow(back_btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(back_btn, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(back_btn, 6, 0);
        NO_SCROLL(back_btn);
        lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, ctx);

        lv_obj_t * back_icon = lv_label_create(back_btn);
        lv_obj_set_style_text_font(back_icon, app_font_fa6_20(), 0);
        lv_obj_set_style_text_color(back_icon, lv_color_hex(0x9AB8B0), 0);
        lv_label_set_text(back_icon, "\xEF\x81\x93");  /* fa-arrow-left */

        lv_obj_t * back_text = lv_label_create(back_btn);
        lv_label_set_text(back_text, "返回首页");
        lv_obj_set_style_text_font(back_text, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(back_text, lv_color_hex(0x9AB8B0), 0);

        /* Title — icon + text split across two fonts */
        {
            lv_obj_t * title_row = lv_obj_create(topbar);
            lv_obj_remove_style_all(title_row);
            lv_obj_set_size(title_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(title_row, 6, 0);
            NO_SCROLL(title_row);

            lv_obj_t * title_icon = lv_label_create(title_row);
            lv_obj_set_style_text_font(title_icon, app_font_fa6_20(), 0);
            lv_obj_set_style_text_color(title_icon, lv_color_hex(0x00D4AA), 0);
            lv_label_set_text(title_icon, "\xEF\x82\xAC");  /* fa-globe */

            lv_obj_t * title_text = lv_label_create(title_row);
            lv_label_set_text(title_text, "网络通讯");
            lv_obj_set_style_text_font(title_text, app_font_kaiti_18(), 0);
            lv_obj_set_style_text_color(title_text, lv_color_hex(0xE0E0E0), 0);
        }

        /* Spacer to push status to right */
        lv_obj_t * spacer = lv_obj_create(topbar);
        lv_obj_remove_style_all(spacer);
        lv_obj_set_size(spacer, LV_SIZE_CONTENT, 1);
        lv_obj_set_flex_grow(spacer, 1);
        NO_SCROLL(spacer);

        /* Connection dot (small circle) */
        ctx->conn_dot = lv_obj_create(topbar);
        lv_obj_set_size(ctx->conn_dot, 10, 10);
        lv_obj_set_style_radius(ctx->conn_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ctx->conn_dot, lv_color_hex(0xFF4455), 0);
        lv_obj_set_style_border_width(ctx->conn_dot, 0, 0);
        NO_SCROLL(ctx->conn_dot);

        /* Connection label */
        ctx->conn_label = lv_label_create(topbar);
        lv_label_set_text(ctx->conn_label, "未连接");
        lv_obj_set_style_text_font(ctx->conn_label, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(ctx->conn_label, lv_color_hex(0xFF4455), 0);
    }

    /* ===== CONFIG ROW: IP + Port + Connect btn ===== */
    {
        lv_obj_t * row = lv_obj_create(screen);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_hor(row, 20, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        NO_SCROLL(row);

        /* IP label */
        lv_obj_t * ip_label = lv_label_create(row);
        lv_label_set_text(ip_label, "IP地址");
        lv_obj_set_style_text_font(ip_label, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(ip_label, lv_color_hex(0x9AB8B0), 0);

        /* IP textarea */
        ctx->ip_ta = lv_textarea_create(row);
        lv_obj_set_size(ctx->ip_ta, 156, 34);
        lv_textarea_set_one_line(ctx->ip_ta, true);
        lv_textarea_set_max_length(ctx->ip_ta, 32);
        lv_textarea_set_text(ctx->ip_ta, "192.168.137.1");
        lv_obj_set_style_bg_color(ctx->ip_ta, lv_color_hex(0x0D1F2D), 0);
        lv_obj_set_style_bg_color(ctx->ip_ta, lv_color_hex(0x0D1F2D),
                                  LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_color(ctx->ip_ta, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_text_color(ctx->ip_ta, lv_color_hex(0x5A7A72),
                                    LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_font(ctx->ip_ta, app_font_kaiti_14(), 0);
        lv_obj_set_style_border_width(ctx->ip_ta, 1, 0);
        lv_obj_set_style_border_color(ctx->ip_ta, lv_color_hex(0x1C2E36), 0);
        lv_obj_set_style_radius(ctx->ip_ta, 8, 0);
        lv_obj_set_style_pad_all(ctx->ip_ta, 6, 0);
        NO_SCROLL(ctx->ip_ta);
        lv_obj_add_event_cb(ctx->ip_ta, on_input_focused, LV_EVENT_CLICKED, ctx);

        /* Port label */
        lv_obj_t * port_label = lv_label_create(row);
        lv_label_set_text(port_label, "端口号");
        lv_obj_set_style_text_font(port_label, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(port_label, lv_color_hex(0x9AB8B0), 0);

        /* Port textarea */
        ctx->port_ta = lv_textarea_create(row);
        lv_obj_set_size(ctx->port_ta, 72, 34);
        lv_textarea_set_one_line(ctx->port_ta, true);
        lv_textarea_set_max_length(ctx->port_ta, 8);
        lv_textarea_set_text(ctx->port_ta, "8888");
        lv_obj_set_style_bg_color(ctx->port_ta, lv_color_hex(0x0D1F2D), 0);
        lv_obj_set_style_bg_color(ctx->port_ta, lv_color_hex(0x0D1F2D),
                                  LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_color(ctx->port_ta, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_text_color(ctx->port_ta, lv_color_hex(0x5A7A72),
                                    LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_font(ctx->port_ta, app_font_kaiti_14(), 0);
        lv_obj_set_style_border_width(ctx->port_ta, 1, 0);
        lv_obj_set_style_border_color(ctx->port_ta, lv_color_hex(0x1C2E36), 0);
        lv_obj_set_style_radius(ctx->port_ta, 8, 0);
        lv_obj_set_style_pad_all(ctx->port_ta, 6, 0);
        NO_SCROLL(ctx->port_ta);
        lv_obj_add_event_cb(ctx->port_ta, on_input_focused, LV_EVENT_CLICKED, ctx);

        /* Connect/Disconnect button */
        ctx->conn_btn = lv_button_create(row);
        lv_obj_set_size(ctx->conn_btn, LV_SIZE_CONTENT, 34);
        lv_obj_set_style_pad_hor(ctx->conn_btn, 18, 0);
        lv_obj_set_style_radius(ctx->conn_btn, 8, 0);
        lv_obj_set_style_bg_color(ctx->conn_btn, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_bg_grad_color(ctx->conn_btn, lv_color_hex(0x0288D1), 0);
        lv_obj_set_style_bg_grad_dir(ctx->conn_btn, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(ctx->conn_btn, 1, 0);
        lv_obj_set_style_border_color(ctx->conn_btn, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_shadow_width(ctx->conn_btn, 6, 0);
        lv_obj_set_style_shadow_color(ctx->conn_btn, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_shadow_opa(ctx->conn_btn, LV_OPA_30, 0);
        lv_obj_set_flex_flow(ctx->conn_btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(ctx->conn_btn, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(ctx->conn_btn, 6, 0);
        NO_SCROLL(ctx->conn_btn);
        lv_obj_add_event_cb(ctx->conn_btn, on_conn_btn_click,
                            LV_EVENT_CLICKED, ctx);

        /* Conn button icon + text (two labels because of two fonts) */
        {
            ctx->conn_btn_icon = lv_label_create(ctx->conn_btn);
            lv_obj_set_style_text_font(ctx->conn_btn_icon, app_font_fa6_20(), 0);
            lv_obj_set_style_text_color(ctx->conn_btn_icon, lv_color_white(), 0);
            lv_label_set_text(ctx->conn_btn_icon, "\xEF\x83\x81");  /* fa-link */

            ctx->conn_btn_label = lv_label_create(ctx->conn_btn);
            lv_label_set_text(ctx->conn_btn_label, "连接");
            lv_obj_set_style_text_font(ctx->conn_btn_label, app_font_kaiti_14(), 0);
            lv_obj_set_style_text_color(ctx->conn_btn_label, lv_color_white(), 0);
        }
    }

    /* ===== COMMAND HINT BOX ===== */
    {
        lv_obj_t * hint = lv_obj_create(screen);
        lv_obj_remove_style_all(hint);
        lv_obj_set_size(hint, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(hint, lv_color_hex(0x0A1620), 0);
        lv_obj_set_style_border_width(hint, 1, 0);
        lv_obj_set_style_border_color(hint, lv_color_hex(0x1C2E36), 0);
        lv_obj_set_style_radius(hint, 8, 0);
        lv_obj_set_style_pad_all(hint, 8, 0);
        lv_obj_set_style_pad_hor(hint, 14, 0);
        lv_obj_set_style_margin_hor(hint, 20, 0);
        lv_obj_set_style_margin_top(hint, 2, 0);
        lv_obj_set_style_margin_bottom(hint, 2, 0);
        lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(hint, 4, 0);
        lv_obj_set_style_pad_column(hint, 14, 0);
        NO_SCROLL(hint);

        /* Icon + title */
        lv_obj_t * hint_icon = lv_label_create(hint);
        lv_obj_set_style_text_font(hint_icon, app_font_fa6_20(), 0);
        lv_obj_set_style_text_color(hint_icon, lv_color_hex(0x00D4AA), 0);
        lv_label_set_text(hint_icon, "\xEF\x81\x9A");  /* fa-circle-info */

        /* Command items */
        const char * cmds[] = {
            "@list", "@name 名", "@all 消息",
            "@目标 消息", "普通→日志"
        };
        for (int i = 0; i < 5; i++) {
            lv_obj_t * item = lv_label_create(hint);
            lv_obj_set_style_text_font(item, app_font_kaiti_14(), 0);
            lv_obj_set_style_text_color(item, lv_color_hex(0x5A7A72), 0);
            lv_label_set_text(item, cmds[i]);
        }
    }

    /* ===== MESSAGE DISPLAY AREA (flex-grow) ===== */
    {
        lv_obj_t * wrapper = lv_obj_create(screen);
        lv_obj_remove_style_all(wrapper);
        lv_obj_set_size(wrapper, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(wrapper, lv_color_hex(0x0A1620), 0);
        lv_obj_set_style_border_width(wrapper, 1, 0);
        lv_obj_set_style_border_color(wrapper, lv_color_hex(0x1C2E36), 0);
        lv_obj_set_style_radius(wrapper, 12, 0);
        lv_obj_set_style_pad_all(wrapper, 0, 0);
        lv_obj_set_style_margin_hor(wrapper, 20, 0);
        lv_obj_set_style_margin_ver(wrapper, 4, 0);
        lv_obj_set_style_clip_corner(wrapper, true, 0);
        NO_SCROLL(wrapper);

        /* Header */
        {
            lv_obj_t * hdr = lv_obj_create(wrapper);
            lv_obj_remove_style_all(hdr);
            lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(hdr, 8, 0);
            lv_obj_set_style_pad_hor(hdr, 14, 0);
            lv_obj_set_style_border_width(hdr, 0, 0);
            lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(hdr, lv_color_hex(0x1C2E36), 0);
            lv_obj_set_style_border_width(hdr, 1, 0);
            lv_obj_set_style_pad_column(hdr, 6, 0);
            NO_SCROLL(hdr);

            lv_obj_t * hdr_dot = lv_obj_create(hdr);
            lv_obj_set_size(hdr_dot, 8, 8);
            lv_obj_set_style_radius(hdr_dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(hdr_dot, lv_color_hex(0x00FF88), 0);
            lv_obj_set_style_border_width(hdr_dot, 0, 0);
            NO_SCROLL(hdr_dot);

            lv_obj_t * hdr_label = lv_label_create(hdr);
            lv_label_set_text(hdr_label, "终端消息");
            lv_obj_set_style_text_font(hdr_label, app_font_kaiti_14(), 0);
            lv_obj_set_style_text_color(hdr_label, lv_color_hex(0x5A7A72), 0);
        }

        /* Scrollable message container — this one IS scrollable */
        ctx->msg_container = lv_obj_create(wrapper);
        lv_obj_remove_style_all(ctx->msg_container);
        lv_obj_set_size(ctx->msg_container, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(ctx->msg_container, 1);
        lv_obj_set_flex_flow(ctx->msg_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ctx->msg_container, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(ctx->msg_container, MSG_CONTAINER_PAD, 0);
        lv_obj_set_style_pad_row(ctx->msg_container, 2, 0);
        /* Scrollable — the exception to NO_SCROLL */
        lv_obj_set_scrollbar_mode(ctx->msg_container, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_add_flag(ctx->msg_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(ctx->msg_container, LV_DIR_VER);
    }

    /* ===== INPUT ROW ===== */
    {
        lv_obj_t * row = lv_obj_create(screen);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(row, 2, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);
        lv_obj_set_style_pad_hor(row, 20, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        NO_SCROLL(row);

        /* Message input textarea */
        ctx->msg_input = lv_textarea_create(row);
        lv_obj_set_size(ctx->msg_input, LV_SIZE_CONTENT, 36);
        lv_obj_set_flex_grow(ctx->msg_input, 1);
        lv_textarea_set_one_line(ctx->msg_input, true);
        lv_textarea_set_max_length(ctx->msg_input, 512);
        lv_textarea_set_placeholder_text(ctx->msg_input, "输入要发送的消息内容...");
        lv_obj_set_style_bg_color(ctx->msg_input, lv_color_hex(0x0D1F2D), 0);
        lv_obj_set_style_bg_color(ctx->msg_input, lv_color_hex(0x0D1F2D),
                                  LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_color(ctx->msg_input, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_text_color(ctx->msg_input, lv_color_hex(0x5A7A72),
                                    LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_font(ctx->msg_input, app_font_kaiti_14(), 0);
        lv_obj_set_style_border_width(ctx->msg_input, 1, 0);
        lv_obj_set_style_border_color(ctx->msg_input, lv_color_hex(0x1C2E36), 0);
        lv_obj_set_style_radius(ctx->msg_input, 8, 0);
        lv_obj_set_style_pad_all(ctx->msg_input, 6, 0);
        NO_SCROLL(ctx->msg_input);
        lv_obj_add_event_cb(ctx->msg_input, on_input_focused,
                            LV_EVENT_CLICKED, ctx);
        /* Note: LV_EVENT_READY for ✔️ key is handled by app_keyboard
         * (on_kb_ready → hide keyboard). Do NOT add on_msg_ready here
         * or Enter/✔️ will both close keyboard AND send message. */

        /* Disable input until connected */
        lv_obj_add_state(ctx->msg_input, LV_STATE_DISABLED);

        /* Send button */
        ctx->send_btn = lv_button_create(row);
        lv_obj_set_size(ctx->send_btn, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_pad_hor(ctx->send_btn, 18, 0);
        lv_obj_set_style_radius(ctx->send_btn, 8, 0);
        lv_obj_set_style_bg_color(ctx->send_btn, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_bg_grad_color(ctx->send_btn, lv_color_hex(0x0288D1), 0);
        lv_obj_set_style_bg_grad_dir(ctx->send_btn, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(ctx->send_btn, 0, 0);
        lv_obj_set_style_shadow_width(ctx->send_btn, 6, 0);
        lv_obj_set_style_shadow_color(ctx->send_btn, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_shadow_opa(ctx->send_btn, LV_OPA_30, 0);
        lv_obj_set_flex_flow(ctx->send_btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(ctx->send_btn, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(ctx->send_btn, 6, 0);
        NO_SCROLL(ctx->send_btn);
        lv_obj_add_event_cb(ctx->send_btn, on_send_click, LV_EVENT_CLICKED, ctx);

        lv_obj_t * send_icon = lv_label_create(ctx->send_btn);
        lv_obj_set_style_text_font(send_icon, app_font_fa6_20(), 0);
        lv_obj_set_style_text_color(send_icon, lv_color_white(), 0);
        lv_label_set_text(send_icon, "\xEF\x87\x98");  /* fa-paper-plane */

        lv_obj_t * send_text = lv_label_create(ctx->send_btn);
        lv_label_set_text(send_text, "发送");
        lv_obj_set_style_text_font(send_text, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(send_text, lv_color_white(), 0);

        /* Disable send until connected */
        lv_obj_add_state(ctx->send_btn, LV_STATE_DISABLED);

        /* Clear button */
        lv_obj_t * clear_btn = lv_button_create(row);
        lv_obj_set_size(clear_btn, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_pad_hor(clear_btn, 14, 0);
        lv_obj_set_style_radius(clear_btn, 8, 0);
        lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0x0A1620), 0);
        lv_obj_set_style_border_width(clear_btn, 1, 0);
        lv_obj_set_style_border_color(clear_btn, lv_color_hex(0x1C2E36), 0);
        lv_obj_set_flex_flow(clear_btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(clear_btn, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(clear_btn, 4, 0);
        NO_SCROLL(clear_btn);
        lv_obj_add_event_cb(clear_btn, on_clear_click, LV_EVENT_CLICKED, ctx);

        lv_obj_t * clear_icon = lv_label_create(clear_btn);
        lv_obj_set_style_text_font(clear_icon, app_font_fa6_20(), 0);
        lv_obj_set_style_text_color(clear_icon, lv_color_hex(0x9AB8B0), 0);
        lv_label_set_text(clear_icon, "\xEF\x87\xB8");  /* fa-trash-can */

        lv_obj_t * clear_text = lv_label_create(clear_btn);
        lv_label_set_text(clear_text, "清除");
        lv_obj_set_style_text_font(clear_text, app_font_kaiti_14(), 0);
        lv_obj_set_style_text_color(clear_text, lv_color_hex(0x9AB8B0), 0);
    }

    /* Initial "system ready" message */
    network_page_append_message(screen, NETWORK_MSG_INFO,
                                "系统就绪，请填写 IP 和端口后点击连接");

    return screen;
}

/* ---- Public: append a timestamped message ---- */
void network_page_append_message(lv_obj_t * screen, network_msg_type_t type,
                                 const char * msg)
{
    if (screen == NULL || msg == NULL || msg[0] == '\0') return;

    /* Retrieve ctx stored via lv_obj_set_user_data during create() */
    network_page_ctx_t * ctx = lv_obj_get_user_data(screen);
    if (ctx == NULL) return;

    /* Trim oldest lines if at limit */
    if (ctx->msg_count >= MAX_MSG_LINES) {
        trim_oldest_msgs(ctx);
    }

    /* Build timestamp */
    time_t now = time(NULL);
    struct tm * tm_info = localtime(&now);
    char time_str[16];
    if (tm_info) {
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    } else {
        uint32_t tick_sec = lv_tick_get() / 1000;
        snprintf(time_str, sizeof(time_str), "%02lu:%02lu",
                 (unsigned long)(tick_sec / 60) % 60,
                 (unsigned long)(tick_sec % 60));
    }

    /* Pick color by type */
    lv_color_t color;
    switch (type) {
        case NETWORK_MSG_SEND:  color = lv_color_hex(0x00D4AA); break;
        case NETWORK_MSG_RECV:  color = lv_color_hex(0x0288D1); break;
        case NETWORK_MSG_ERROR: color = lv_color_hex(0xFF6B6B); break;
        default:                color = lv_color_hex(0x9AB8B0); break;
    }

    /* Create label for this message */
    char line_buf[MAX_MSG_CHARS];
    snprintf(line_buf, sizeof(line_buf), "[%s] %s", time_str, msg);

    lv_obj_t * line = lv_label_create(ctx->msg_container);
    lv_label_set_text(line, line_buf);
    lv_label_set_long_mode(line, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(line, lv_pct(100));
    lv_obj_set_style_text_font(line, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(line, color, 0);

    ctx->msg_count++;

    /* Scroll to bottom */
    lv_obj_scroll_to_view(line, LV_ANIM_OFF);
}

/* ---- Public: update connection state ---- */
void network_page_set_connected(lv_obj_t * screen, bool connected)
{
    if (screen == NULL) return;
    network_page_ctx_t * ctx = lv_obj_get_user_data(screen);
    if (ctx == NULL) return;

    ctx->is_connected = connected;
    update_conn_ui(ctx, connected);
}

/* ---- Public: clear all messages ---- */
void network_page_clear_messages(lv_obj_t * screen)
{
    if (screen == NULL) return;
    network_page_ctx_t * ctx = lv_obj_get_user_data(screen);
    if (ctx == NULL) return;

    /* Delete all message label children */
    lv_obj_clean(ctx->msg_container);
    ctx->msg_count = 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void update_conn_ui(network_page_ctx_t * ctx, bool connected)
{
    if (connected) {
        /* Dot → green */
        lv_obj_set_style_bg_color(ctx->conn_dot, lv_color_hex(0x00FF88), 0);

        /* Label → "已连接" green */
        lv_label_set_text(ctx->conn_label, "已连接");
        lv_obj_set_style_text_color(ctx->conn_label, lv_color_hex(0x00FF88), 0);

        /* Button → Disconnect style (red border) */
        lv_label_set_text(ctx->conn_btn_icon, "\xEF\x84\xA7");  /* fa-chain-broken */
        lv_label_set_text(ctx->conn_btn_label, "断开");
        lv_obj_set_style_bg_color(ctx->conn_btn, lv_color_hex(0x0A1620), 0);
        lv_obj_set_style_bg_grad_color(ctx->conn_btn, lv_color_hex(0x0A1620), 0);
        lv_obj_set_style_border_color(ctx->conn_btn, lv_color_hex(0xFF4455), 0);
        lv_obj_set_style_shadow_color(ctx->conn_btn, lv_color_hex(0xFF4455), 0);

        /* Enable input + send */
        lv_obj_clear_state(ctx->msg_input, LV_STATE_DISABLED);
        lv_obj_clear_state(ctx->send_btn, LV_STATE_DISABLED);
    } else {
        /* Dot → red */
        lv_obj_set_style_bg_color(ctx->conn_dot, lv_color_hex(0xFF4455), 0);

        /* Label → "未连接" red */
        lv_label_set_text(ctx->conn_label, "未连接");
        lv_obj_set_style_text_color(ctx->conn_label, lv_color_hex(0xFF4455), 0);

        /* Button → Connect style (gradient) */
        lv_label_set_text(ctx->conn_btn_icon, "\xEF\x83\x81");  /* fa-link */
        lv_label_set_text(ctx->conn_btn_label, "连接");
        lv_obj_set_style_bg_color(ctx->conn_btn, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_bg_grad_color(ctx->conn_btn, lv_color_hex(0x0288D1), 0);
        lv_obj_set_style_border_color(ctx->conn_btn, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_shadow_color(ctx->conn_btn, lv_color_hex(0x00D4AA), 0);

        /* Disable input + send */
        lv_obj_add_state(ctx->msg_input, LV_STATE_DISABLED);
        lv_obj_add_state(ctx->send_btn, LV_STATE_DISABLED);
    }
}

/* Remove the oldest message label to keep count under MAX_MSG_LINES */
static void trim_oldest_msgs(network_page_ctx_t * ctx)
{
    if (ctx->msg_count < MAX_MSG_LINES) return;

    /* Delete the first few children to make room */
    int to_remove = ctx->msg_count - MAX_MSG_LINES + 10;  /* batch delete */
    uint32_t child_count = lv_obj_get_child_count(ctx->msg_container);
    for (int i = 0; i < to_remove && i < (int)child_count; i++) {
        lv_obj_t * child = lv_obj_get_child(ctx->msg_container, 0);
        if (child) {
            lv_obj_delete(child);
            ctx->msg_count--;
        }
    }
}

/* ---- Event handlers ---- */

static void on_back_click(lv_event_t * e)
{
    network_page_ctx_t * ctx = lv_event_get_user_data(e);
    app_keyboard_hide();
    if (ctx->back_cb) ctx->back_cb();
}

static void on_conn_btn_click(lv_event_t * e)
{
    network_page_ctx_t * ctx = lv_event_get_user_data(e);
    app_keyboard_hide();

    if (ctx->is_connected) {
        /* Disconnect */
        network_page_append_message(ctx->screen, NETWORK_MSG_INFO, "正在断开连接…");
        if (ctx->disconnect_cb) ctx->disconnect_cb();
    } else {
        /* Connect */
        const char * ip   = lv_textarea_get_text(ctx->ip_ta);
        const char * port = lv_textarea_get_text(ctx->port_ta);

        /* Validate */
        if (ip[0] == '\0' || port[0] == '\0') {
            network_page_append_message(ctx->screen, NETWORK_MSG_ERROR,
                                        "请填写 IP 地址和端口号");
            return;
        }

        network_page_append_message(ctx->screen, NETWORK_MSG_INFO,
                                    "正在连接…");
        if (ctx->connect_cb) ctx->connect_cb(ip, port);
    }
}

static void on_send_click(lv_event_t * e)
{
    network_page_ctx_t * ctx = lv_event_get_user_data(e);
    app_keyboard_hide();

    if (!ctx->is_connected) return;

    const char * msg = lv_textarea_get_text(ctx->msg_input);
    if (msg[0] == '\0') return;

    /* Call user's send callback */
    if (ctx->send_cb) ctx->send_cb(msg);

    /* Clear input */
    lv_textarea_set_text(ctx->msg_input, "");
}

static void on_clear_click(lv_event_t * e)
{
    network_page_ctx_t * ctx = lv_event_get_user_data(e);
    app_keyboard_hide();
    network_page_clear_messages(ctx->screen);
}

static void on_input_focused(lv_event_t * e)
{
    network_page_ctx_t * ctx = lv_event_get_user_data(e);
    lv_obj_t * ta = lv_event_get_target(e);

    const char * label = "";
    if (ta == ctx->ip_ta)       label = "IP地址";
    else if (ta == ctx->port_ta) label = "端口号";
    else if (ta == ctx->msg_input) label = "发送消息";

    app_keyboard_show(ctx->screen, ta, label);
}

static void on_page_delete(lv_event_t * e)
{
    network_page_ctx_t * ctx = lv_event_get_user_data(e);
    lv_obj_clean(ctx->msg_container);
    lv_free(ctx);
}
