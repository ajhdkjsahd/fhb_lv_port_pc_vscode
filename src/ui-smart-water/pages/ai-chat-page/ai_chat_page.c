// ========== ai_chat_page.c ==========
#include "ai_chat_page.h"
#include "../app_fonts.h"
#include "../app_keyboard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*********************
 *      DEFINES
 *********************/
#ifndef NO_SCROLL
#define NO_SCROLL(obj) \
    lv_obj_set_scrollbar_mode((obj), LV_SCROLLBAR_MODE_OFF); \
    lv_obj_clear_flag((obj), LV_OBJ_FLAG_SCROLLABLE)
#endif

#define MAX_MESSAGES     100
#define TOPBAR_H          48
#define INPUT_H           56
#define ICON_SIZE         32
#define BUBBLE_MAX_W      520
#define CURSOR_BLINK_MS   530

/* FA6 icon code points — rendered with app_font_fa6_20() */
#define ICON_USER    "\xEF\x80\x87"  /* fa-user */
#define ICON_AI      "\xEF\x95\x84"  /* fa-robot */
#define ICON_THINK   "\xEF\x97\x9C"  /* fa-brain */
#define ICON_SEND    "\xEF\x87\x98"  /* fa-paper-plane */
#define ICON_STOP    "\xEF\x81\x8D"  /* fa-stop */
#define ICON_WATER   "\xEF\x9B\x83"  /* fa-water */

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    lv_obj_t * row;          /* Row container in chat area */
    lv_obj_t * icon_label;   /* Icon label */
    lv_obj_t * body;         /* Column: think + bubble */
    lv_obj_t * think_panel;  /* Think collapsible container (NULL if none) */
    lv_obj_t * think_body;   /* Think text label inside panel */
    lv_obj_t * bubble;       /* Message bubble label */
    lv_obj_t * cursor;       /* Blinking cursor label (streaming only) */
    lv_obj_t * time_label;   /* Timestamp */
    char *     content;      /* Full text content (malloc'd) */
    char *     thinking;     /* Full thinking text (malloc'd, can be NULL) */
    bool       is_user;      /* true = user, false = AI */
} chat_msg_slot_t;

typedef struct {
    lv_obj_t * screen;
    lv_obj_t * chat_area;
    lv_obj_t * msg_input;
    lv_obj_t * send_btn;
    lv_obj_t * stop_btn;
    lv_obj_t * clear_btn;
    lv_obj_t * status_dot;
    lv_obj_t * status_label;
    lv_obj_t * welcome;
    lv_obj_t * avatar;
    lv_obj_t * glow_ring;    /* pulsing ring behind avatar */

    lv_timer_t * cursor_timer;
    lv_anim_t    avatar_anim;

    ai_chat_back_cb_t back_cb;
    ai_chat_send_cb_t send_cb;
    ai_chat_stop_cb_t stop_cb;

    chat_msg_slot_t messages[MAX_MESSAGES];
    int    msg_count;
    int    current_ai_idx;   /* Index of streaming AI message, -1 if none */
    bool   streaming;
    bool   cursor_visible;
} ai_chat_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_back_click(lv_event_t * e);
static void on_send_click(lv_event_t * e);
static void on_send_ready(lv_event_t * e);
static void on_stop_click(lv_event_t * e);
static void on_clear_click(lv_event_t * e);
static void on_input_click(lv_event_t * e);
static void on_quick_click(lv_event_t * e);
static void on_page_delete(lv_event_t * e);
static void on_think_header_click(lv_event_t * e);
static void cursor_blink_cb(lv_timer_t * t);
static void avatar_glow_cb(void * var, int32_t v);
static void scroll_to_bottom(ai_chat_ctx_t * ctx);
static void do_send(ai_chat_ctx_t * ctx);
static void set_streaming_ui(ai_chat_ctx_t * ctx, bool on);
static chat_msg_slot_t * add_msg_slot(ai_chat_ctx_t * ctx, bool is_user);
static lv_obj_t * create_think_panel(lv_obj_t * parent, ai_chat_ctx_t * ctx);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * ai_chat_page_create(ai_chat_back_cb_t  back_cb,
                               ai_chat_send_cb_t  send_cb,
                               ai_chat_stop_cb_t  stop_cb)
{
    ai_chat_ctx_t * ctx = lv_malloc_zeroed(sizeof(ai_chat_ctx_t));
    ctx->back_cb       = back_cb;
    ctx->send_cb       = send_cb;
    ctx->stop_cb       = stop_cb;
    ctx->current_ai_idx = -1;
    ctx->streaming      = false;
    ctx->cursor_visible = true;

    /* ── Screen ── */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x060E14), 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(screen);
    ctx->screen = screen;
    lv_obj_set_user_data(screen, ctx);  /* for thread-safe update functions */
    lv_obj_add_event_cb(screen, on_page_delete, LV_EVENT_DELETE, ctx);

    /* ═══════════ TOP BAR ═══════════ */
    lv_obj_t * topbar = lv_obj_create(screen);
    lv_obj_set_size(topbar, lv_pct(100), TOPBAR_H);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_border_side(topbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(topbar, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_border_width(topbar, 1, 0);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(topbar, 0, 0);
    lv_obj_set_style_pad_row(topbar, 0, 0);
    lv_obj_set_style_pad_left(topbar, 12, 0);
    lv_obj_set_style_pad_right(topbar, 16, 0);
    NO_SCROLL(topbar);

    /* Back button */
    lv_obj_t * back_btn = lv_button_create(topbar);
    lv_obj_set_size(back_btn, 34, 34);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    NO_SCROLL(back_btn);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, ctx);

    lv_obj_t * back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "←");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0x9AB8B0), 0);
    lv_obj_set_style_text_font(back_label, app_font_kaiti_18(), 0);
    lv_obj_center(back_label);

    /* Avatar + glow ring container */
    lv_obj_t * avatar_cont = lv_obj_create(topbar);
    lv_obj_set_size(avatar_cont, 54, 54);   /* extra space for glow ring */
    lv_obj_set_style_bg_color(avatar_cont, lv_color_hex(0x0A1620), 0);  /* blend into topbar */
    lv_obj_set_style_border_width(avatar_cont, 0, 0);
    lv_obj_set_style_pad_all(avatar_cont, 0, 0);
    lv_obj_set_style_clip_corner(avatar_cont, false, 0);  /* allow glow to overflow */
    NO_SCROLL(avatar_cont);

    /* Glow ring — large pulsing circle behind avatar */
    lv_obj_t * glow_ring = lv_obj_create(avatar_cont);
    lv_obj_set_size(glow_ring, 48, 48);
    lv_obj_set_style_radius(glow_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(glow_ring, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_bg_opa(glow_ring, LV_OPA_0, 0);
    lv_obj_set_style_border_width(glow_ring, 0, 0);
    lv_obj_set_style_shadow_width(glow_ring, 16, 0);
    lv_obj_set_style_shadow_color(glow_ring, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(glow_ring, LV_OPA_0, 0);
    lv_obj_center(glow_ring);
    ctx->glow_ring = glow_ring;

    /* Avatar icon (on top of glow ring) */
    lv_obj_t * avatar = lv_obj_create(avatar_cont);
    lv_obj_set_size(avatar, 34, 34);
    lv_obj_set_style_radius(avatar, 10, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(0x0D1F2E), 0);
    lv_obj_set_style_border_width(avatar, 1, 0);
    lv_obj_set_style_border_color(avatar, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_width(avatar, 20, 0);
    lv_obj_set_style_shadow_color(avatar, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(avatar, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(avatar, 0, 0);
    lv_obj_center(avatar);
    ctx->avatar = avatar;

    lv_obj_t * avatar_label = lv_label_create(avatar);
    lv_label_set_text(avatar_label, ICON_AI);
    lv_obj_set_style_text_font(avatar_label, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(avatar_label, lv_color_hex(0x00D4AA), 0);
    lv_obj_center(avatar_label);

    /* Title */
    lv_obj_t * title = lv_label_create(topbar);
    lv_label_set_text(title, "AI 智能助手");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(title, app_font_kaiti_18(), 0);
    lv_obj_set_flex_grow(title, 1);

    /* Status */
    lv_obj_t * dot = lv_obj_create(topbar);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_shadow_width(dot, 6, 0);
    lv_obj_set_style_shadow_color(dot, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(dot, 128, 0);
    NO_SCROLL(dot);
    ctx->status_dot = dot;

    lv_obj_t * status_label = lv_label_create(topbar);
    lv_label_set_text(status_label, "DeepSeek-R1");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_text_font(status_label, app_font_kaiti_14(), 0);
    ctx->status_label = status_label;

    /* ═══════════ CHAT AREA ═══════════ */
    lv_obj_t * chat = lv_obj_create(screen);
    lv_obj_set_size(chat, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(chat, 1);
    lv_obj_set_style_bg_color(chat, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(chat, 0, 0);
    lv_obj_set_style_pad_all(chat, 12, 0);
    lv_obj_set_style_pad_row(chat, 8, 0);
    lv_obj_set_flex_flow(chat, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chat, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(chat, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(chat, LV_OBJ_FLAG_SCROLLABLE);
    ctx->chat_area = chat;

    /* Welcome placeholder */
    lv_obj_t * welcome = lv_obj_create(chat);
    lv_obj_set_size(welcome, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(welcome, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(welcome, 0, 0);
    lv_obj_set_flex_flow(welcome, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(welcome, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(welcome, 40, 0);
    lv_obj_set_style_pad_row(welcome, 8, 0);
    NO_SCROLL(welcome);
    ctx->welcome = welcome;

    lv_obj_t * big_icon = lv_label_create(welcome);
    lv_label_set_text(big_icon, ICON_THINK);  /* fa-brain */
    lv_obj_set_style_text_font(big_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(big_icon, lv_color_hex(0x00D4AA), 0);

    lv_obj_t * wel_title = lv_label_create(welcome);
    lv_label_set_text(wel_title, "智慧水产 AI 助手");
    lv_obj_set_style_text_color(wel_title, lv_color_hex(0x9AB8B0), 0);
    lv_obj_set_style_text_font(wel_title, app_font_kaiti_18(), 0);

    lv_obj_t * wel_hints = lv_label_create(welcome);
    lv_label_set_text(wel_hints,
        "试试问我：\n"
        "· 草鱼适宜的水温是多少？\n"
        "· 溶氧量偏低怎么办？\n"
        "· 如何判断鱼类是否生病？");
    lv_obj_set_style_text_color(wel_hints, lv_color_hex(0x5A7A72), 0);
    lv_obj_set_style_text_font(wel_hints, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_line_space(wel_hints, 4, 0);

    /* ═══════════ QUICK-SEND BAR ═══════════ */
    lv_obj_t * quick_bar = lv_obj_create(screen);
    lv_obj_set_size(quick_bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(quick_bar, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(quick_bar, 0, 0);
    lv_obj_set_flex_flow(quick_bar, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(quick_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(quick_bar, 4, 0);
    lv_obj_set_style_pad_row(quick_bar, 4, 0);
    lv_obj_set_style_pad_column(quick_bar, 6, 0);
    NO_SCROLL(quick_bar);

    static const char * quick_msgs[] = {
        "水温多少合适？",
        "溶氧量偏低怎么办？",
        "如何判断鱼是否生病？",
        "今天该喂多少饲料？",
    };
    for (int qi = 0; qi < 4; qi++) {
        lv_obj_t * qbtn = lv_button_create(quick_bar);
        lv_obj_set_size(qbtn, LV_SIZE_CONTENT, 30);
        lv_obj_set_style_pad_hor(qbtn, 12, 0);
        lv_obj_set_style_radius(qbtn, 14, 0);
        lv_obj_set_style_bg_color(qbtn, lv_color_hex(0x0D1F2E), 0);
        lv_obj_set_style_border_width(qbtn, 1, 0);
        lv_obj_set_style_border_color(qbtn, lv_color_hex(0x1C2E36), 0);
        lv_obj_set_style_shadow_width(qbtn, 0, 0);
        NO_SCROLL(qbtn);
        /* Store the question text as a heap copy in user_data */
        char * qtext = lv_malloc(strlen(quick_msgs[qi]) + 1);
        strcpy(qtext, quick_msgs[qi]);
        lv_obj_add_event_cb(qbtn, on_quick_click, LV_EVENT_CLICKED, qtext);

        lv_obj_t * qlabel = lv_label_create(qbtn);
        lv_label_set_text(qlabel, quick_msgs[qi]);
        lv_obj_set_style_text_color(qlabel, lv_color_hex(0x9AB8B0), 0);
        lv_obj_set_style_text_font(qlabel, app_font_kaiti_14(), 0);
    }

    /* ═══════════ INPUT BAR ═══════════ */
    lv_obj_t * input_bar = lv_obj_create(screen);
    lv_obj_set_size(input_bar, lv_pct(100), INPUT_H);
    lv_obj_set_style_bg_color(input_bar, lv_color_hex(0x0C1822), 0);
    lv_obj_set_style_border_width(input_bar, 0, 0);
    lv_obj_set_style_border_side(input_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(input_bar, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_border_width(input_bar, 1, 0);
    lv_obj_set_flex_flow(input_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(input_bar, 0, 0);
    lv_obj_set_style_pad_row(input_bar, 0, 0);
    lv_obj_set_style_pad_left(input_bar, 8, 0);
    lv_obj_set_style_pad_right(input_bar, 8, 0);
    lv_obj_set_style_pad_column(input_bar, 6, 0);
    NO_SCROLL(input_bar);

    /* Clear button */
    lv_obj_t * clear_btn = lv_button_create(input_bar);
    lv_obj_set_size(clear_btn, 48, 38);
    lv_obj_set_style_radius(clear_btn, 10, 0);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(clear_btn, 1, 0);
    lv_obj_set_style_border_color(clear_btn, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_shadow_width(clear_btn, 0, 0);
    NO_SCROLL(clear_btn);
    lv_obj_add_event_cb(clear_btn, on_clear_click, LV_EVENT_CLICKED, ctx);
    ctx->clear_btn = clear_btn;

    lv_obj_t * clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, "清空");
    lv_obj_set_style_text_color(clear_label, lv_color_hex(0x5A7A72), 0);
    lv_obj_set_style_text_font(clear_label, app_font_kaiti_14(), 0);
    lv_obj_center(clear_label);

    /* Message input */
    lv_obj_t * ta = lv_textarea_create(input_bar);
    lv_obj_set_size(ta, LV_SIZE_CONTENT, 38);
    lv_obj_set_flex_grow(ta, 1);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(ta, app_font_kaiti_18(), 0);
    lv_obj_set_style_pad_all(ta, 8, 0);
    lv_textarea_set_placeholder_text(ta, "输入您的问题...");
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 512);
    lv_obj_set_style_text_color(ta, lv_color_hex(0x5A7A72), LV_PART_TEXTAREA_PLACEHOLDER);
    /* Click to show keyboard */
    lv_obj_add_event_cb(ta, on_input_click, LV_EVENT_CLICKED, ctx);
    /* Ready/Enter → send message */
    lv_obj_add_event_cb(ta, on_send_ready, LV_EVENT_READY, ctx);
    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICKABLE);
    ctx->msg_input = ta;

    /* Send button */
    lv_obj_t * send_btn = lv_button_create(input_bar);
    lv_obj_set_size(send_btn, 42, 38);
    lv_obj_set_style_radius(send_btn, 10, 0);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(0x0A2A24), 0);
    lv_obj_set_style_border_width(send_btn, 1, 0);
    lv_obj_set_style_border_color(send_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_width(send_btn, 0, 0);
    NO_SCROLL(send_btn);
    lv_obj_add_event_cb(send_btn, on_send_click, LV_EVENT_CLICKED, ctx);
    ctx->send_btn = send_btn;

    lv_obj_t * send_label = lv_label_create(send_btn);
    lv_label_set_text(send_label, ICON_SEND);
    lv_obj_set_style_text_color(send_label, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_text_font(send_label, app_font_fa6_20(), 0);
    lv_obj_center(send_label);

    /* Stop button (hidden by default) */
    lv_obj_t * stop_btn = lv_button_create(input_bar);
    lv_obj_set_size(stop_btn, 42, 38);
    lv_obj_set_style_radius(stop_btn, 10, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0x1A0A0A), 0);
    lv_obj_set_style_border_width(stop_btn, 1, 0);
    lv_obj_set_style_border_color(stop_btn, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_shadow_width(stop_btn, 0, 0);
    lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);
    NO_SCROLL(stop_btn);
    lv_obj_add_event_cb(stop_btn, on_stop_click, LV_EVENT_CLICKED, ctx);
    ctx->stop_btn = stop_btn;

    lv_obj_t * stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, ICON_STOP);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(stop_label, app_font_fa6_20(), 0);
    lv_obj_center(stop_label);

    return screen;
}

/* ── Thread-safe updates (called via lv_async_call) ── */

void ai_chat_page_begin_response(lv_obj_t * screen)
{
    ai_chat_ctx_t * ctx = lv_obj_get_user_data(screen);
    LV_LOG_USER("AI: begin_response screen=%p ctx=%p", (void*)screen, (void*)ctx);
    if (ctx == NULL) return;

    /* Hide welcome */
    if (ctx->welcome) {
        lv_obj_add_flag(ctx->welcome, LV_OBJ_FLAG_HIDDEN);
    }

    if (ctx->current_ai_idx >= 0) {
        /* Repurpose the thinking placeholder slot — clear its text */
        chat_msg_slot_t * slot = &ctx->messages[ctx->current_ai_idx];
        LV_LOG_USER("AI: repurpose thinking slot idx=%d", ctx->current_ai_idx);
        if (slot->bubble) lv_label_set_text(slot->bubble, "");
        if (slot->content) { lv_free(slot->content); slot->content = NULL; }
    } else {
        /* Fallback: create a new AI message slot */
        chat_msg_slot_t * slot = add_msg_slot(ctx, false);
        LV_LOG_USER("AI: slot=%p idx=%d", (void*)slot, ctx->msg_count - 1);
        ctx->current_ai_idx = ctx->msg_count - 1;
    }
    ctx->streaming = true;

    /* Start avatar glow (may already be running from thinking phase) */
    if (ctx->avatar_anim.var == NULL) {
        lv_anim_init(&ctx->avatar_anim);
        lv_anim_set_var(&ctx->avatar_anim, ctx);
        lv_anim_set_exec_cb(&ctx->avatar_anim, avatar_glow_cb);
        lv_anim_set_values(&ctx->avatar_anim, 0, 255);
        lv_anim_set_duration(&ctx->avatar_anim, 500);
        lv_anim_set_playback_duration(&ctx->avatar_anim, 500);
        lv_anim_set_repeat_count(&ctx->avatar_anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&ctx->avatar_anim, lv_anim_path_ease_in_out);
        lv_anim_start(&ctx->avatar_anim);
    }

    /* Start cursor blink timer */
    if (ctx->cursor_timer == NULL) {
        ctx->cursor_timer = lv_timer_create(cursor_blink_cb, CURSOR_BLINK_MS, ctx);
    }

    set_streaming_ui(ctx, true);
}

void ai_chat_page_append_thinking(lv_obj_t * screen, const char * text)
{
    ai_chat_ctx_t * ctx = lv_obj_get_user_data(screen);
    if (ctx == NULL || ctx->current_ai_idx < 0) return;
    chat_msg_slot_t * slot = &ctx->messages[ctx->current_ai_idx];

    /* Allocate or realloc thinking buffer */
    if (slot->thinking == NULL) {
        slot->thinking = lv_malloc(strlen(text) + 1);
        if (slot->thinking) strcpy(slot->thinking, text);
    } else {
        size_t old_len = strlen(slot->thinking);
        size_t new_len = old_len + strlen(text) + 1;
        char * new_buf = lv_malloc(new_len);
        if (new_buf) {
            strcpy(new_buf, slot->thinking);
            strcat(new_buf, text);
            lv_free(slot->thinking);
            slot->thinking = new_buf;
        }
    }

    /* Create think panel on first thinking text */
    if (slot->think_panel == NULL) {
        slot->think_panel = create_think_panel(slot->body, ctx);
        /* Reorder: think panel goes above bubble */
        if (slot->think_panel && slot->bubble) {
            lv_obj_move_to_index(slot->think_panel, 0);
        }
    }

    /* Update think body text */
    if (slot->think_body && slot->thinking) {
        lv_label_set_text(slot->think_body, slot->thinking);
    }

    scroll_to_bottom(ctx);
}

void ai_chat_page_finish_thinking(lv_obj_t * screen)
{
    ai_chat_ctx_t * ctx = lv_obj_get_user_data(screen);
    if (ctx == NULL || ctx->current_ai_idx < 0) return;
    chat_msg_slot_t * slot = &ctx->messages[ctx->current_ai_idx];

    /* Collapse the think panel */
    if (slot->think_panel) {
        /* Find the body container inside the think panel and hide it */
        uint32_t cnt = lv_obj_get_child_cnt(slot->think_panel);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t * child = lv_obj_get_child(slot->think_panel, i);
            /* The body is our think_body's parent (the content area) */
            if (child != lv_obj_get_child(slot->think_panel, 0)) {
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                /* Update chevron */
                lv_obj_t * header = lv_obj_get_child(slot->think_panel, 0);
                if (header) {
                    lv_obj_t * chev = lv_obj_get_child(header,
                        lv_obj_get_child_cnt(header) - 1);
                    if (chev) lv_label_set_text(chev, "▶");
                }
            }
        }
    }
}

void ai_chat_page_append_answer(lv_obj_t * screen, const char * text)
{
    ai_chat_ctx_t * ctx = lv_obj_get_user_data(screen);
    LV_LOG_USER("AI: append_answer screen=%p ctx=%p idx=%d text_len=%zu",
                (void*)screen, (void*)ctx, ctx ? ctx->current_ai_idx : -2,
                text ? strlen(text) : 0);
    if (ctx == NULL || ctx->current_ai_idx < 0) return;
    chat_msg_slot_t * slot = &ctx->messages[ctx->current_ai_idx];

    /* Allocate or realloc content buffer */
    if (slot->content == NULL) {
        slot->content = lv_malloc(strlen(text) + 1);
        if (slot->content) strcpy(slot->content, text);
    } else {
        size_t old_len = strlen(slot->content);
        size_t new_len = old_len + strlen(text) + 1;
        char * new_buf = lv_malloc(new_len);
        if (new_buf) {
            strcpy(new_buf, slot->content);
            strcat(new_buf, text);
            lv_free(slot->content);
            slot->content = new_buf;
        }
    }

    /* Update bubble label (append cursor char) */
    if (slot->bubble && slot->content) {
        char display[1024];
        snprintf(display, sizeof(display), "%s▌", slot->content);
        lv_label_set_text(slot->bubble, display);
    }

    scroll_to_bottom(ctx);
}

void ai_chat_page_finish_response(lv_obj_t * screen)
{
    ai_chat_ctx_t * ctx = lv_obj_get_user_data(screen);
    LV_LOG_USER("AI: finish_response screen=%p ctx=%p idx=%d",
                (void*)screen, (void*)ctx, ctx ? ctx->current_ai_idx : -2);
    if (ctx == NULL || ctx->current_ai_idx < 0) return;
    chat_msg_slot_t * slot = &ctx->messages[ctx->current_ai_idx];

    /* Remove cursor from bubble text */
    if (slot->bubble && slot->content) {
        lv_label_set_text(slot->bubble, slot->content ? slot->content : "");
        /* Force layout update so the bubble resizes to fit full text */
        lv_obj_update_layout(slot->bubble);
    }

    /* Remove cursor object */
    if (slot->cursor) {
        lv_obj_delete(slot->cursor);
        slot->cursor = NULL;
    }

    /* Stop avatar glow */
    lv_anim_delete(ctx, avatar_glow_cb);
    lv_obj_set_style_shadow_opa(ctx->avatar, 0, 0);
    lv_obj_set_style_bg_opa(ctx->glow_ring, LV_OPA_0, 0);
    lv_obj_set_style_shadow_opa(ctx->glow_ring, LV_OPA_0, 0);

    /* Stop cursor timer */
    if (ctx->cursor_timer) {
        lv_timer_delete(ctx->cursor_timer);
        ctx->cursor_timer = NULL;
    }

    ctx->streaming = false;
    ctx->current_ai_idx = -1;
    set_streaming_ui(ctx, false);

    scroll_to_bottom(ctx);
}

void ai_chat_page_show_error(lv_obj_t * screen, const char * msg)
{
    ai_chat_ctx_t * ctx = lv_obj_get_user_data(screen);
    if (ctx == NULL) return;

    /* Hide welcome */
    if (ctx->welcome) {
        lv_obj_add_flag(ctx->welcome, LV_OBJ_FLAG_HIDDEN);
    }

    /* Remove thinking placeholder if it exists */
    if (ctx->current_ai_idx >= 0) {
        chat_msg_slot_t * slot = &ctx->messages[ctx->current_ai_idx];
        if (slot->row)  lv_obj_delete(slot->row);
        if (slot->content)  { lv_free(slot->content);  slot->content  = NULL; }
        if (slot->thinking) { lv_free(slot->thinking); slot->thinking = NULL; }
        memset(slot, 0, sizeof(*slot));
        ctx->msg_count--;
        ctx->current_ai_idx = -1;
    }

    /* Create a system-style error message */
    lv_obj_t * row = lv_obj_create(ctx->chat_area);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 4, 0);
    NO_SCROLL(row);

    lv_obj_t * err_label = lv_label_create(row);
    char buf[256];
    snprintf(buf, sizeof(buf), "[!] %s", msg);
    lv_label_set_text(err_label, buf);
    lv_obj_set_style_text_color(err_label, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(err_label, app_font_kaiti_14(), 0);

    /* Stop streaming state */
    /* Stop avatar glow */
    lv_anim_delete(ctx, avatar_glow_cb);
    lv_obj_set_style_shadow_opa(ctx->avatar, 0, 0);
    lv_obj_set_style_bg_opa(ctx->glow_ring, LV_OPA_0, 0);
    lv_obj_set_style_shadow_opa(ctx->glow_ring, LV_OPA_0, 0);

    /* Stop cursor timer */
    if (ctx->cursor_timer) {
        lv_timer_delete(ctx->cursor_timer);
        ctx->cursor_timer = NULL;
    }

    ctx->streaming = false;
    ctx->current_ai_idx = -1;
    set_streaming_ui(ctx, false);

    scroll_to_bottom(ctx);
}

void ai_chat_page_set_online(lv_obj_t * screen, bool online)
{
    ai_chat_ctx_t * ctx = lv_obj_get_user_data(screen);
    if (ctx == NULL) return;

    if (online) {
        lv_obj_set_style_bg_color(ctx->status_dot, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_shadow_color(ctx->status_dot, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_shadow_opa(ctx->status_dot, 128, 0);
        lv_label_set_text(ctx->status_label, "DeepSeek-R1");
        lv_obj_set_style_text_color(ctx->status_label, lv_color_hex(0x00D4AA), 0);
    } else {
        lv_obj_set_style_bg_color(ctx->status_dot, lv_color_hex(0xFF6B6B), 0);
        lv_obj_set_style_shadow_color(ctx->status_dot, lv_color_hex(0xFF6B6B), 0);
        lv_obj_set_style_shadow_opa(ctx->status_dot, 128, 0);
        lv_label_set_text(ctx->status_label, "离线");
        lv_obj_set_style_text_color(ctx->status_label, lv_color_hex(0xFF6B6B), 0);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void on_back_click(lv_event_t * e)
{
    ai_chat_ctx_t * ctx = lv_event_get_user_data(e);
    app_keyboard_hide();
    if (ctx->back_cb) ctx->back_cb();
}

static void do_send(ai_chat_ctx_t * ctx)
{
    if (ctx->streaming) {
        LV_LOG_USER("AI: do_send blocked (streaming)");
        return;
    }
    const char * text = lv_textarea_get_text(ctx->msg_input);
    LV_LOG_USER("AI: do_send text='%s' send_cb=%p", text ? text : "(null)", (void*)ctx->send_cb);
    if (text == NULL || text[0] == '\0') return;

    /* Hide welcome */
    if (ctx->welcome) {
        lv_obj_add_flag(ctx->welcome, LV_OBJ_FLAG_HIDDEN);
    }

    /* Add user message bubble */
    chat_msg_slot_t * slot = add_msg_slot(ctx, true);
    if (slot->content) lv_free(slot->content);
    slot->content = lv_malloc(strlen(text) + 1);
    if (slot->content) strcpy(slot->content, text);
    if (slot->bubble) lv_label_set_text(slot->bubble, text);

    /* Save text BEFORE clearing textarea — textarea owns the buffer! */
    char msg_copy[512];
    strncpy(msg_copy, text, sizeof(msg_copy) - 1);
    msg_copy[sizeof(msg_copy) - 1] = '\0';

    /* ── Immediately show "thinking" placeholder + disable send ── */
    set_streaming_ui(ctx, true);
    ctx->streaming = true;

    /* Create thinking placeholder slot */
    chat_msg_slot_t * ai_slot = add_msg_slot(ctx, false);
    ctx->current_ai_idx = ctx->msg_count - 1;
    if (ai_slot->bubble) {
        lv_label_set_text(ai_slot->bubble, "AI 正在思考...");
    }

    /* Start avatar glow early (during thinking wait) */
    lv_anim_init(&ctx->avatar_anim);
    lv_anim_set_var(&ctx->avatar_anim, ctx);
    lv_anim_set_exec_cb(&ctx->avatar_anim, avatar_glow_cb);
    lv_anim_set_values(&ctx->avatar_anim, 0, 255);
    lv_anim_set_duration(&ctx->avatar_anim, 500);
    lv_anim_set_playback_duration(&ctx->avatar_anim, 500);
    lv_anim_set_repeat_count(&ctx->avatar_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&ctx->avatar_anim, lv_anim_path_ease_in_out);
    lv_anim_start(&ctx->avatar_anim);

    scroll_to_bottom(ctx);

    /* Clear input */
    lv_textarea_set_text(ctx->msg_input, "");
    app_keyboard_hide();

    /* Notify callback with saved copy (starts background HTTP thread) */
    if (ctx->send_cb) ctx->send_cb(msg_copy);
}

static void on_send_click(lv_event_t * e)
{
    ai_chat_ctx_t * ctx = lv_event_get_user_data(e);
    LV_LOG_USER("AI: send button clicked");
    do_send(ctx);
}

static void on_send_ready(lv_event_t * e)
{
    /* Enter/✔️ key → just close keyboard, don't send */
    (void)e;
    app_keyboard_hide();
}

static void on_stop_click(lv_event_t * e)
{
    ai_chat_ctx_t * ctx = lv_event_get_user_data(e);
    if (ctx->stop_cb) ctx->stop_cb();
}

static void on_clear_click(lv_event_t * e)
{
    ai_chat_ctx_t * ctx = lv_event_get_user_data(e);
    if (ctx->streaming) return;

    /* Delete all message slot objects and free strings */
    for (int i = 0; i < ctx->msg_count; i++) {
        if (ctx->messages[i].row)  lv_obj_delete(ctx->messages[i].row);
        if (ctx->messages[i].content)  lv_free(ctx->messages[i].content);
        if (ctx->messages[i].thinking) lv_free(ctx->messages[i].thinking);
    }
    memset(ctx->messages, 0, sizeof(ctx->messages));
    ctx->msg_count = 0;
    ctx->current_ai_idx = -1;

    /* Show welcome again */
    if (ctx->welcome) {
        lv_obj_clear_flag(ctx->welcome, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(ctx->welcome, 0);
    }
}

static void on_input_click(lv_event_t * e)
{
    ai_chat_ctx_t * ctx = lv_event_get_user_data(e);
    app_keyboard_show(ctx->screen, ctx->msg_input, "输入消息");
}

static void on_quick_click(lv_event_t * e)
{
    /* Quick-send button: fill textarea with the question, then send immediately */
    char * qtext = lv_event_get_user_data(e);
    if (qtext == NULL) return;

    /* Walk up to the screen to get the context */
    lv_obj_t * screen = lv_obj_get_screen(lv_event_get_target_obj(e));
    ai_chat_ctx_t * ctx = lv_obj_get_user_data(screen);
    if (ctx == NULL) return;

    LV_LOG_USER("AI: quick-send '%s'", qtext);

    lv_textarea_set_text(ctx->msg_input, qtext);
    do_send(ctx);
}

static void on_page_delete(lv_event_t * e)
{
    ai_chat_ctx_t * ctx = lv_event_get_user_data(e);
    /* Free all message strings */
    for (int i = 0; i < ctx->msg_count; i++) {
        if (ctx->messages[i].content)  lv_free(ctx->messages[i].content);
        if (ctx->messages[i].thinking) lv_free(ctx->messages[i].thinking);
    }
    /* Stop cursor timer */
    if (ctx->cursor_timer) {
        lv_timer_delete(ctx->cursor_timer);
    }
    /* Stop avatar animation */
    lv_anim_delete(ctx, avatar_glow_cb);
    lv_free(ctx);
}

static void on_think_header_click(lv_event_t * e)
{
    lv_obj_t * header = lv_event_get_target_obj(e);
    lv_obj_t * panel  = lv_obj_get_parent(header);
    ai_chat_ctx_t * ctx = lv_event_get_user_data(e);

    /* Toggle: find the content body (second child) and toggle visibility */
    uint32_t cnt = lv_obj_get_child_cnt(panel);
    for (uint32_t i = 1; i < cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(panel, i);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
            /* Update chevron */
            lv_obj_t * chev = lv_obj_get_child(header,
                lv_obj_get_child_cnt(header) - 1);
            if (chev) lv_label_set_text(chev, "▼");
        } else {
            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t * chev = lv_obj_get_child(header,
                lv_obj_get_child_cnt(header) - 1);
            if (chev) lv_label_set_text(chev, "▶");
        }
    }
}

static void cursor_blink_cb(lv_timer_t * t)
{
    ai_chat_ctx_t * ctx = lv_timer_get_user_data(t);
    if (ctx->current_ai_idx < 0) return;
    chat_msg_slot_t * slot = &ctx->messages[ctx->current_ai_idx];

    ctx->cursor_visible = !ctx->cursor_visible;

    if (slot->bubble && slot->content) {
        if (ctx->cursor_visible) {
            char display[1024];
            snprintf(display, sizeof(display), "%s▌", slot->content);
            lv_label_set_text(slot->bubble, display);
        } else {
            lv_label_set_text(slot->bubble, slot->content);
        }
    }
}

static void avatar_glow_cb(void * var, int32_t v)
{
    ai_chat_ctx_t * ctx = (ai_chat_ctx_t *)var;
    lv_opa_t opa = (lv_opa_t)v;
    /* Pulse glow ring — background + shadow */
    lv_obj_set_style_bg_opa(ctx->glow_ring, opa, 0);
    lv_obj_set_style_shadow_opa(ctx->glow_ring, opa, 0);
    /* Pulse avatar shadow */
    lv_obj_set_style_shadow_opa(ctx->avatar, opa, 0);
}

static void scroll_to_bottom(ai_chat_ctx_t * ctx)
{
    /* Force layout recalculation so new children have proper sizes */
    lv_obj_update_layout(ctx->chat_area);
    uint32_t cnt = lv_obj_get_child_cnt(ctx->chat_area);
    if (cnt > 0) {
        lv_obj_t * last = lv_obj_get_child(ctx->chat_area, cnt - 1);
        if (last) {
            lv_obj_scroll_to_view_recursive(last, LV_ANIM_OFF);
        }
    }
}

static void set_streaming_ui(ai_chat_ctx_t * ctx, bool on)
{
    if (on) {
        lv_obj_add_flag(ctx->send_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->stop_btn, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_placeholder_text(ctx->msg_input, "AI 正在回复...");
        lv_obj_add_state(ctx->msg_input, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_flag(ctx->send_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->stop_btn, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_placeholder_text(ctx->msg_input, "输入您的问题...");
        lv_obj_clear_state(ctx->msg_input, LV_STATE_DISABLED);
    }
}

static chat_msg_slot_t * add_msg_slot(ai_chat_ctx_t * ctx, bool is_user)
{
    if (ctx->msg_count >= MAX_MESSAGES) {
        /* Remove oldest message */
        if (ctx->messages[0].row) lv_obj_delete(ctx->messages[0].row);
        if (ctx->messages[0].content) lv_free(ctx->messages[0].content);
        if (ctx->messages[0].thinking) lv_free(ctx->messages[0].thinking);
        memmove(&ctx->messages[0], &ctx->messages[1],
                sizeof(chat_msg_slot_t) * (MAX_MESSAGES - 1));
        ctx->msg_count = MAX_MESSAGES - 1;
    }

    chat_msg_slot_t * slot = &ctx->messages[ctx->msg_count];
    memset(slot, 0, sizeof(*slot));
    slot->is_user = is_user;
    ctx->msg_count++;

    /* ── Row container ── */
    lv_obj_t * row = lv_obj_create(ctx->chat_area);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_row(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    NO_SCROLL(row);

    if (is_user) {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
    } else {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
    }
    slot->row = row;

    /* ── Icon ── */
    lv_obj_t * icon = lv_obj_create(row);
    lv_obj_set_size(icon, ICON_SIZE, ICON_SIZE);
    lv_obj_set_style_radius(icon, 8, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    NO_SCROLL(icon);

    if (is_user) {
        lv_obj_set_style_bg_color(icon, lv_color_hex(0x0A2A24), 0);
    } else {
        lv_obj_set_style_bg_color(icon, lv_color_hex(0x0D1F2E), 0);
    }

    lv_obj_t * icon_label = lv_label_create(icon);
    lv_label_set_text(icon_label, is_user ? ICON_USER : ICON_AI);
    lv_obj_set_style_text_font(icon_label, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(0xD4A017), 0);  /* gold */
    lv_obj_center(icon_label);
    slot->icon_label = icon_label;

    /* ── Body column (think + bubble) ── */
    lv_obj_t * body = lv_obj_create(row);
    lv_obj_set_size(body, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START,
                          is_user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_row(body, 4, 0);
    NO_SCROLL(body);
    slot->body = body;

    /* Set max width for body so bubbles don't span entire screen */
    lv_obj_set_style_max_width(body, BUBBLE_MAX_W, 0);

    /* ── Bubble ── */
    lv_obj_t * bubble = lv_label_create(body);
    lv_label_set_text(bubble, "");
    lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
    /* CRITICAL: set width so WRAP mode knows where to break lines */
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, BUBBLE_MAX_W, 0);
    lv_obj_set_style_min_width(bubble, 40, 0);
    lv_obj_set_style_text_color(bubble, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(bubble, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_line_space(bubble, 3, 0);
    lv_obj_set_style_pad_all(bubble, 10, 0);
    lv_obj_set_style_radius(bubble, 12, 0);
    if (is_user) {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x0A2A24), 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        lv_obj_set_style_border_side(bubble, LV_BORDER_SIDE_RIGHT, 0);
        lv_obj_set_style_border_width(bubble, 2, 0);
        lv_obj_set_style_border_color(bubble, lv_color_hex(0x00D4AA), 0);
        lv_obj_set_style_radius(bubble, 12, 0);  /* Will be overridden per-side */
        /* LVGL doesn't do per-corner radius easily; keep simple */
    } else {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x0D1F2E), 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        lv_obj_set_style_border_side(bubble, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(bubble, 2, 0);
        lv_obj_set_style_border_color(bubble, lv_color_hex(0x0288D1), 0);
    }
    slot->bubble = bubble;

    return slot;
}

static lv_obj_t * create_think_panel(lv_obj_t * parent, ai_chat_ctx_t * ctx)
{
    lv_obj_t * panel = lv_obj_create(parent);
    lv_obj_set_size(panel, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0B1A24), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xD4A017), 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_pad_row(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    NO_SCROLL(panel);

    /* Header */
    lv_obj_t * header = lv_obj_create(panel);
    lv_obj_set_size(header, lv_pct(100), 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0B1A24), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(header, 10, 0);
    lv_obj_set_style_pad_right(header, 10, 0);
    lv_obj_set_style_pad_row(header, 0, 0);
    lv_obj_set_style_pad_column(header, 6, 0);
    NO_SCROLL(header);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, on_think_header_click, LV_EVENT_CLICKED, ctx);

    lv_obj_t * think_icon = lv_label_create(header);
    lv_label_set_text(think_icon, ICON_THINK);
    lv_obj_set_style_text_font(think_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(think_icon, lv_color_hex(0xD4A017), 0);

    lv_obj_t * think_title = lv_label_create(header);
    lv_label_set_text(think_title, "思考过程");
    lv_obj_set_style_text_color(think_title, lv_color_hex(0xD4A017), 0);
    lv_obj_set_style_text_font(think_title, app_font_kaiti_14(), 0);

    /* Spacer */
    lv_obj_t * spacer = lv_obj_create(header);
    lv_obj_set_size(spacer, LV_SIZE_CONTENT, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_color(spacer, lv_color_hex(0x0B1A24), 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    NO_SCROLL(spacer);

    lv_obj_t * chevron = lv_label_create(header);
    lv_label_set_text(chevron, "▼");
    lv_obj_set_style_text_color(chevron, lv_color_hex(0x5A7A72), 0);
    lv_obj_set_style_text_font(chevron, app_font_kaiti_14(), 0);

    /* Body */
    lv_obj_t * body = lv_obj_create(panel);
    lv_obj_set_size(body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x0B1A24), 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_border_side(body, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_pad_all(body, 10, 0);
    NO_SCROLL(body);

    lv_obj_t * body_text = lv_label_create(body);
    lv_label_set_text(body_text, "");
    lv_label_set_long_mode(body_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(body_text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(body_text, BUBBLE_MAX_W - 20, 0);
    lv_obj_set_style_min_width(body_text, 40, 0);
    lv_obj_set_style_text_color(body_text, lv_color_hex(0x5A7A72), 0);
    lv_obj_set_style_text_font(body_text, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_line_space(body_text, 3, 0);

    /* Store reference for later text updates (append_thinking) */
    if (ctx->current_ai_idx >= 0 && ctx->current_ai_idx < MAX_MESSAGES) {
        ctx->messages[ctx->current_ai_idx].think_body = body_text;
    }

    return panel;
}
