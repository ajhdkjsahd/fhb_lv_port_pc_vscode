// ========== video_page.c ==========
#include "video_page.h"
#include "../app_fonts.h"
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

#define VIDEO_X      16
#define VIDEO_Y      65
#define VIDEO_W      768
#define VIDEO_H      305
#define SWIPE_THRESHOLD  60

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t * screen;
    lv_obj_t * video_area;       /* Video hole (mplayer rendering region) */
    lv_obj_t * cover_image;      /* Cover / first-frame image */
    lv_obj_t * play_overlay;     /* Big play icon overlay (visible when paused/stopped) */
    lv_obj_t * progress_bar;
    lv_obj_t * time_current;
    lv_obj_t * time_total;
    lv_obj_t * play_icon;        /* Play/pause button icon */
    lv_obj_t * dots_label;       /* ● ○ indicators */
    lv_obj_t * counter_label;    /* "1 / 3" label */
    lv_obj_t * no_video_label;   /* "暂无视频" placeholder */
    bool       is_playing;
    int32_t    duration_sec;
    int        current_index;
    int        video_count;
    lv_coord_t press_x;
    lv_coord_t press_y;
    video_control_cb_t  control_cb;
    video_seek_cb_t     seek_cb;
    video_back_cb_t     back_cb;
    video_select_cb_t   select_cb;
    const char * const * video_paths;
    const char * const * cover_paths;
} video_page_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_back_click(lv_event_t * e);
static void on_control_click(lv_event_t * e);
static void on_progress_changed(lv_event_t * e);
static void on_progress_released(lv_event_t * e);
static void on_video_pressed(lv_event_t * e);
static void on_video_released(lv_event_t * e);
static void on_page_delete(lv_event_t * e);
static void update_dots(video_page_ctx_t * ctx);
static void update_cover(video_page_ctx_t * ctx);
static void format_time(int32_t sec, char * buf, size_t buf_size);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * video_page_create(video_control_cb_t  control_cb,
                             video_seek_cb_t     seek_cb,
                             video_back_cb_t     back_cb,
                             video_select_cb_t   select_cb,
                             const char * const  video_paths[],
                             const char * const  cover_paths[],
                             int                 video_count)
{
    video_page_ctx_t * ctx = lv_malloc(sizeof(video_page_ctx_t));
    if(ctx == NULL) return NULL;
    memset(ctx, 0, sizeof(video_page_ctx_t));
    ctx->control_cb   = control_cb;
    ctx->seek_cb      = seek_cb;
    ctx->back_cb      = back_cb;
    ctx->select_cb    = select_cb;
    ctx->video_paths  = video_paths;
    ctx->cover_paths  = cover_paths;
    ctx->video_count  = video_count;
    ctx->current_index = 0;
    ctx->is_playing   = false;
    ctx->duration_sec = 0;

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
    lv_obj_set_user_data(screen, ctx);

    /* ===== TOP BAR ===== */
    lv_obj_t * topbar = lv_obj_create(screen);
    lv_obj_remove_style_all(topbar);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_COVER, 0);
    lv_obj_set_size(topbar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(topbar, 8, 0);
    lv_obj_set_style_pad_hor(topbar, 16, 0);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
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
    lv_obj_set_flex_align(back_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(back_btn, 6, 0);
    NO_SCROLL(back_btn);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, ctx);

    lv_obj_t * back_icon = lv_label_create(back_btn);
    lv_obj_set_style_text_font(back_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0x9AB8B0), 0);
    lv_label_set_text(back_icon, "\xEF\x80\x95");  /* fa-house */

    lv_obj_t * back_text = lv_label_create(back_btn);
    lv_label_set_text(back_text, "返回首页");
    lv_obj_set_style_text_font(back_text, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(back_text, lv_color_hex(0x9AB8B0), 0);

    /* Title */
    lv_obj_t * title = lv_label_create(topbar);
    lv_label_set_text(title, "视频监控");
    lv_obj_set_style_text_font(title, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);

    /* Counter */
    ctx->counter_label = lv_label_create(topbar);
    lv_obj_set_style_text_font(ctx->counter_label, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(ctx->counter_label, lv_color_hex(0x00D4AA), 0);
    if(video_count > 0) {
        lv_label_set_text_fmt(ctx->counter_label, "1 / %d", video_count);
    } else {
        lv_label_set_text(ctx->counter_label, "0 / 0");
    }

    /* ===== VIDEO AREA ===== */
    ctx->video_area = lv_obj_create(screen);
    lv_obj_remove_style_all(ctx->video_area);
    lv_obj_set_size(ctx->video_area, VIDEO_W, VIDEO_H);
    lv_obj_align(ctx->video_area, LV_ALIGN_TOP_LEFT, VIDEO_X, VIDEO_Y);
    lv_obj_set_style_bg_color(ctx->video_area, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->video_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->video_area, 2, 0);
    lv_obj_set_style_border_color(ctx->video_area, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_radius(ctx->video_area, 12, 0);
    lv_obj_set_style_clip_corner(ctx->video_area, true, 0);
    lv_obj_set_flex_flow(ctx->video_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->video_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(ctx->video_area);

    /* Swipe detection on video area */
    lv_obj_add_event_cb(ctx->video_area, on_video_pressed,  LV_EVENT_PRESSED,  ctx);
    lv_obj_add_event_cb(ctx->video_area, on_video_released, LV_EVENT_RELEASED, ctx);

    /* Cover image (behind overlay, fills video area) */
    ctx->cover_image = lv_image_create(ctx->video_area);
    lv_obj_set_size(ctx->cover_image, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(ctx->cover_image, 10, 0);
    lv_obj_set_style_clip_corner(ctx->cover_image, true, 0);
    lv_image_set_inner_align(ctx->cover_image, LV_IMAGE_ALIGN_CENTER);

    /* No-video placeholder */
    ctx->no_video_label = lv_label_create(ctx->video_area);
    lv_label_set_text(ctx->no_video_label, "暂无视频");
    lv_obj_set_style_text_font(ctx->no_video_label, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(ctx->no_video_label, lv_color_hex(0x5A7A72), 0);
    lv_obj_center(ctx->no_video_label);

    /* Big play overlay icon (visible when paused/stopped) */
    ctx->play_overlay = lv_label_create(ctx->video_area);
    lv_obj_set_style_text_font(ctx->play_overlay, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(ctx->play_overlay, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_bg_color(ctx->play_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->play_overlay, LV_OPA_50, 0);
    lv_obj_set_style_radius(ctx->play_overlay, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(ctx->play_overlay, 16, 0);
    lv_label_set_text(ctx->play_overlay, "\xEF\x81\x8B");  /* fa-play */
    lv_obj_center(ctx->play_overlay);

    /* Show/hide based on video count */
    if(video_count == 0) {
        lv_obj_add_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->play_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->no_video_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->no_video_label, LV_OBJ_FLAG_HIDDEN);
        update_cover(ctx);
    }

    /* ===== PROGRESS BAR ===== */
    lv_obj_t * progress_wrap = lv_obj_create(screen);
    lv_obj_remove_style_all(progress_wrap);
    lv_obj_set_style_bg_color(progress_wrap, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_bg_opa(progress_wrap, LV_OPA_COVER, 0);
    lv_obj_add_flag(progress_wrap, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(progress_wrap, LV_ALIGN_TOP_LEFT, 0, VIDEO_Y + VIDEO_H + 6);
    lv_obj_set_size(progress_wrap, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(progress_wrap, 16, 0);
    lv_obj_set_style_pad_top(progress_wrap, 0, 0);
    lv_obj_set_style_pad_bottom(progress_wrap, 4, 0);
    lv_obj_set_flex_flow(progress_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(progress_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(progress_wrap, 4, 0);
    NO_SCROLL(progress_wrap);

    ctx->progress_bar = lv_slider_create(progress_wrap);
    lv_obj_set_size(ctx->progress_bar, lv_pct(100), 18);
    lv_obj_set_style_bg_color(ctx->progress_bar, lv_color_hex(0x0A1620), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->progress_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->progress_bar, lv_color_hex(0x1C2E36), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->progress_bar, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->progress_bar, lv_color_hex(0x00D4AA), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ctx->progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ctx->progress_bar, 9, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(ctx->progress_bar, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_color(ctx->progress_bar, lv_color_hex(0xE0FFF8), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(ctx->progress_bar, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(ctx->progress_bar, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(ctx->progress_bar, lv_color_hex(0x00D4AA), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(ctx->progress_bar, 10, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(ctx->progress_bar, lv_color_hex(0x00D4AA), LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(ctx->progress_bar, LV_OPA_30, LV_PART_KNOB);
    lv_slider_set_range(ctx->progress_bar, 0, 100);
    lv_slider_set_value(ctx->progress_bar, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(ctx->progress_bar, on_progress_changed, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(ctx->progress_bar, on_progress_released, LV_EVENT_RELEASED, ctx);

    /* Time row */
    lv_obj_t * time_row = lv_obj_create(progress_wrap);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_size(time_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(time_row);

    ctx->time_current = lv_label_create(time_row);
    lv_label_set_text(ctx->time_current, "00:00");
    lv_obj_set_style_text_font(ctx->time_current, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(ctx->time_current, lv_color_hex(0x5A7A72), 0);

    ctx->time_total = lv_label_create(time_row);
    lv_label_set_text(ctx->time_total, "??:??");
    lv_obj_set_style_text_font(ctx->time_total, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(ctx->time_total, lv_color_hex(0x5A7A72), 0);

    /* ===== DOTS INDICATOR ===== */
    ctx->dots_label = lv_label_create(screen);
    lv_obj_add_flag(ctx->dots_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ctx->dots_label, LV_ALIGN_TOP_LEFT, 0, VIDEO_Y + VIDEO_H + 8 + 18 + 4 + 14 + 4);
    lv_obj_set_width(ctx->dots_label, lv_pct(100));
    lv_obj_set_style_text_align(ctx->dots_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ctx->dots_label, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(ctx->dots_label, lv_color_hex(0x3A5A52), 0);
    update_dots(ctx);

    /* ===== CONTROLS ===== */
    lv_obj_t * ctrl_wrap = lv_obj_create(screen);
    lv_obj_remove_style_all(ctrl_wrap);
    lv_obj_set_style_bg_color(ctrl_wrap, lv_color_hex(0x060E14), 0);
    lv_obj_set_style_bg_opa(ctrl_wrap, LV_OPA_COVER, 0);
    lv_obj_add_flag(ctrl_wrap, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ctrl_wrap, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(ctrl_wrap, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(ctrl_wrap, 6, 0);
    lv_obj_set_style_pad_bottom(ctrl_wrap, 12, 0);
    lv_obj_set_flex_flow(ctrl_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_wrap, 14, 0);
    NO_SCROLL(ctrl_wrap);

    /* ---- Volume down ---- */
    lv_obj_t * vol_down_btn = lv_button_create(ctrl_wrap);
    lv_obj_set_size(vol_down_btn, 42, 42);
    lv_obj_set_style_radius(vol_down_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(vol_down_btn, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(vol_down_btn, 1, 0);
    lv_obj_set_style_border_color(vol_down_btn, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_flex_flow(vol_down_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol_down_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(vol_down_btn);
    lv_obj_add_event_cb(vol_down_btn, on_control_click, LV_EVENT_CLICKED, (void *)(intptr_t)VIDEO_ACTION_VOLUME_DOWN);

    lv_obj_t * vol_down_icon = lv_label_create(vol_down_btn);
    lv_obj_set_style_text_font(vol_down_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(vol_down_icon, lv_color_hex(0xE0E0E0), 0);
    lv_label_set_text(vol_down_icon, "\xEF\x80\xA7");  /* fa-volume-low */

    /* ---- Volume up ---- */
    lv_obj_t * vol_up_btn = lv_button_create(ctrl_wrap);
    lv_obj_set_size(vol_up_btn, 42, 42);
    lv_obj_set_style_radius(vol_up_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(vol_up_btn, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(vol_up_btn, 1, 0);
    lv_obj_set_style_border_color(vol_up_btn, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_flex_flow(vol_up_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol_up_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(vol_up_btn);
    lv_obj_add_event_cb(vol_up_btn, on_control_click, LV_EVENT_CLICKED, (void *)(intptr_t)VIDEO_ACTION_VOLUME_UP);

    lv_obj_t * vol_up_icon = lv_label_create(vol_up_btn);
    lv_obj_set_style_text_font(vol_up_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(vol_up_icon, lv_color_hex(0xE0E0E0), 0);
    lv_label_set_text(vol_up_icon, "\xEF\x80\xA8");  /* fa-volume-high */

    /* Spacer */
    lv_obj_t * spacer1 = lv_obj_create(ctrl_wrap);
    lv_obj_remove_style_all(spacer1);
    lv_obj_set_size(spacer1, 16, 1);
    NO_SCROLL(spacer1);

    /* ---- Rewind ---- */
    lv_obj_t * rewind_btn = lv_button_create(ctrl_wrap);
    lv_obj_set_size(rewind_btn, 42, 42);
    lv_obj_set_style_radius(rewind_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(rewind_btn, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(rewind_btn, 1, 0);
    lv_obj_set_style_border_color(rewind_btn, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_flex_flow(rewind_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rewind_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(rewind_btn);
    lv_obj_add_event_cb(rewind_btn, on_control_click, LV_EVENT_CLICKED, (void *)(intptr_t)VIDEO_ACTION_REWIND);

    lv_obj_t * rewind_icon = lv_label_create(rewind_btn);
    lv_obj_set_style_text_font(rewind_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(rewind_icon, lv_color_hex(0xE0E0E0), 0);
    lv_label_set_text(rewind_icon, "\xEF\x81\x8A");  /* fa-backward */

    /* ---- Play/Pause (accented, larger) ---- */
    lv_obj_t * play_btn = lv_button_create(ctrl_wrap);
    lv_obj_set_size(play_btn, 52, 52);
    lv_obj_set_style_radius(play_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_border_width(play_btn, 0, 0);
    lv_obj_set_style_shadow_width(play_btn, 12, 0);
    lv_obj_set_style_shadow_color(play_btn, lv_color_hex(0x00D4AA), 0);
    lv_obj_set_style_shadow_opa(play_btn, LV_OPA_30, 0);
    lv_obj_set_flex_flow(play_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(play_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(play_btn);
    lv_obj_add_event_cb(play_btn, on_control_click, LV_EVENT_CLICKED, (void *)(intptr_t)VIDEO_ACTION_PLAY_PAUSE);

    ctx->play_icon = lv_label_create(play_btn);
    lv_obj_set_style_text_font(ctx->play_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(ctx->play_icon, lv_color_hex(0x000000), 0);
    lv_label_set_text(ctx->play_icon, "\xEF\x81\x8B");  /* fa-play */

    /* ---- Fast forward ---- */
    lv_obj_t * ff_btn = lv_button_create(ctrl_wrap);
    lv_obj_set_size(ff_btn, 42, 42);
    lv_obj_set_style_radius(ff_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ff_btn, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(ff_btn, 1, 0);
    lv_obj_set_style_border_color(ff_btn, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_flex_flow(ff_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ff_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(ff_btn);
    lv_obj_add_event_cb(ff_btn, on_control_click, LV_EVENT_CLICKED, (void *)(intptr_t)VIDEO_ACTION_FAST_FORWARD);

    lv_obj_t * ff_icon = lv_label_create(ff_btn);
    lv_obj_set_style_text_font(ff_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(ff_icon, lv_color_hex(0xE0E0E0), 0);
    lv_label_set_text(ff_icon, "\xEF\x81\x8E");  /* fa-forward */

    return screen;
}

/**********************
 *   PUBLIC API
 **********************/

void video_page_update_progress(lv_obj_t * screen,
                                int32_t position,
                                const char * current_time,
                                const char * total_time)
{
    if(screen == NULL) return;
    video_page_ctx_t * ctx = lv_obj_get_user_data(screen);
    if(ctx == NULL) return;

    if(position < 0) position = 0;
    if(ctx->duration_sec > 0 && position > ctx->duration_sec) position = ctx->duration_sec;

    if(!lv_obj_has_state(ctx->progress_bar, LV_STATE_PRESSED)) {
        lv_slider_set_value(ctx->progress_bar, position, LV_ANIM_OFF);
    }

    if(current_time) lv_label_set_text(ctx->time_current, current_time);
    if(total_time)   lv_label_set_text(ctx->time_total, total_time);
}

void video_page_set_play_state(lv_obj_t * screen, bool is_playing)
{
    if(screen == NULL) return;
    video_page_ctx_t * ctx = lv_obj_get_user_data(screen);
    if(ctx == NULL) return;

    ctx->is_playing = is_playing;
    if(is_playing) {
        lv_label_set_text(ctx->play_icon, "\xEF\x81\x8C");  /* fa-pause */
        lv_obj_add_flag(ctx->play_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(ctx->play_icon, "\xEF\x81\x8B");  /* fa-play */
        /* Don't show overlay on pause — only show when video is fully stopped */
    }
}

void video_page_set_video_active(lv_obj_t * screen, bool active)
{
    if(screen == NULL) return;
    video_page_ctx_t * ctx = lv_obj_get_user_data(screen);
    if(ctx == NULL) return;

    if(active) {
        lv_obj_set_style_bg_opa(ctx->video_area, LV_OPA_0, 0);
        lv_obj_set_style_border_opa(ctx->video_area, LV_OPA_0, 0);
        lv_obj_add_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->play_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_opa(ctx->video_area, LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(ctx->video_area, LV_OPA_COVER, 0);
        update_cover(ctx);
        if(!ctx->is_playing && ctx->video_count > 0) {
            lv_obj_remove_flag(ctx->play_overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void video_page_set_duration(lv_obj_t * screen, int32_t duration_sec)
{
    if(screen == NULL || duration_sec < 1) return;
    video_page_ctx_t * ctx = lv_obj_get_user_data(screen);
    if(ctx == NULL) return;

    ctx->duration_sec = duration_sec;
    lv_slider_set_range(ctx->progress_bar, 0, duration_sec);

    char total[16];
    format_time(duration_sec, total, sizeof(total));
    lv_label_set_text(ctx->time_total, total);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void on_back_click(lv_event_t * e)
{
    video_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->back_cb) {
        ctx->back_cb();
    }
}

static void on_video_pressed(lv_event_t * e)
{
    video_page_ctx_t * ctx = lv_event_get_user_data(e);
    lv_indev_t * indev = lv_event_get_indev(e);
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    ctx->press_x = pt.x;
    ctx->press_y = pt.y;
}

static void on_video_released(lv_event_t * e)
{
    video_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->video_count <= 1) return;

    lv_indev_t * indev = lv_event_get_indev(e);
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    lv_coord_t dx = pt.x - ctx->press_x;
    lv_coord_t dy = pt.y - ctx->press_y;

    if(LV_ABS(dx) > LV_ABS(dy) && LV_ABS(dx) > SWIPE_THRESHOLD) {
        int new_index = ctx->current_index;
        if(dx < 0) {
            /* Swipe left → next video */
            new_index = (ctx->current_index + 1) % ctx->video_count;
        } else {
            /* Swipe right → previous video */
            new_index = (ctx->current_index - 1 + ctx->video_count) % ctx->video_count;
        }

        if(new_index != ctx->current_index) {
            ctx->current_index = new_index;

            /* Update UI */
            lv_label_set_text_fmt(ctx->counter_label, "%d / %d",
                                  ctx->current_index + 1, ctx->video_count);
            update_dots(ctx);
            update_cover(ctx);

            /* Reset progress */
            lv_slider_set_value(ctx->progress_bar, 0, LV_ANIM_OFF);
            lv_label_set_text(ctx->time_current, "00:00");
            lv_label_set_text(ctx->time_total, "??:??");
            ctx->duration_sec = 0;

            /* Notify callback — it handles stop + state reset */
            if(ctx->select_cb) {
                ctx->select_cb(ctx->current_index);
            }
        }
    }
}

static void update_dots(video_page_ctx_t * ctx)
{
    if(ctx->video_count <= 1) {
        lv_label_set_text(ctx->dots_label, "");
        return;
    }

    char buf[256];
    int pos = 0;
    for(int i = 0; i < ctx->video_count; i++) {
        if(pos >= (int)sizeof(buf) - 8) break;
        if(i > 0) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "  ");
        }
        if(i == ctx->current_index) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "\xE2\x97\x8F");  /* ● active */
        } else {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "\xE2\x97\x8B");  /* ○ inactive */
        }
    }
    lv_label_set_text(ctx->dots_label, buf);
}

static void update_cover(video_page_ctx_t * ctx)
{
    if(ctx->video_count == 0) return;

    if(ctx->cover_paths && ctx->cover_paths[ctx->current_index]) {
        lv_image_set_src(ctx->cover_image, ctx->cover_paths[ctx->current_index]);
        lv_obj_remove_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* No cover — show black background (already set on video_area) */
        lv_obj_add_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_control_click(lv_event_t * e)
{
    video_action_t action = (video_action_t)(intptr_t)lv_event_get_user_data(e);

    /* Walk up to find the screen, then get ctx */
    lv_obj_t * obj = lv_event_get_target(e);
    while(lv_obj_get_parent(obj) != NULL) {
        obj = lv_obj_get_parent(obj);
    }
    video_page_ctx_t * ctx = lv_obj_get_user_data(obj);

    if(ctx && ctx->control_cb) {
        ctx->control_cb(action);
    }
}

static void on_progress_changed(lv_event_t * e)
{
    video_page_ctx_t * ctx = lv_event_get_user_data(e);
    int32_t value = lv_slider_get_value(ctx->progress_bar);
    char buf[16];
    format_time(value, buf, sizeof(buf));
    lv_label_set_text(ctx->time_current, buf);
}

static void on_progress_released(lv_event_t * e)
{
    video_page_ctx_t * ctx = lv_event_get_user_data(e);
    int32_t value = lv_slider_get_value(ctx->progress_bar);
    if(ctx->seek_cb) {
        ctx->seek_cb(value);
    }
}

static void on_page_delete(lv_event_t * e)
{
    video_page_ctx_t * ctx = lv_event_get_user_data(e);
    lv_free(ctx);
}

static void format_time(int32_t sec, char * buf, size_t buf_size)
{
    if(sec < 0) sec = 0;
    int32_t min = sec / 60;
    int32_t rem = sec % 60;
    snprintf(buf, buf_size, "%02ld:%02ld", (long)min, (long)rem);
}
