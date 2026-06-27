// ========== gallery_page.c ==========
#include "gallery_page.h"
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

#define SWIPE_THRESHOLD  60   /* Minimum horizontal drag distance for a swipe */

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t * screen;
    lv_obj_t * image_frame;     /* The image container (for size calcs + swipe) */
    lv_obj_t * image_obj;       /* lv_image showing current photo */
    lv_obj_t * counter_label;   /* "1 / 3" label */
    lv_obj_t * dots_label;      /* single label: "●  ○  ○  ●" style indicators */
    lv_obj_t * empty_label;     /* "暂无图片" placeholder */
    const char * const * image_paths;
    int         image_count;
    int         current_index;
    int         preload_idx;    /* Next image index to pre-decode */
    lv_timer_t * preload_timer; /* Timer for background pre-decode */
    lv_coord_t  press_x;        /* For manual swipe detection */
    lv_coord_t  press_y;
    gallery_back_cb_t back_cb;
} gallery_page_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_back_click(lv_event_t * e);
static void on_frame_pressed(lv_event_t * e);
static void on_frame_released(lv_event_t * e);
static void on_frame_size_changed(lv_event_t * e);
static void on_page_delete(lv_event_t * e);
static void update_display(gallery_page_ctx_t * ctx);
static void show_image(gallery_page_ctx_t * ctx, int index);
static void recalc_scale(gallery_page_ctx_t * ctx);
static void preload_timer_cb(lv_timer_t * timer);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * gallery_page_create(gallery_back_cb_t   back_cb,
                               const char * const  image_paths[],
                               int                 image_count)
{
    gallery_page_ctx_t * ctx = lv_malloc(sizeof(gallery_page_ctx_t));
    if(ctx == NULL) return NULL;
    memset(ctx, 0, sizeof(gallery_page_ctx_t));
    ctx->back_cb      = back_cb;
    ctx->image_paths  = image_paths;
    ctx->image_count  = image_count;
    ctx->current_index = 0;

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

    /* ===== TOP BAR ===== */
    lv_obj_t * topbar = lv_obj_create(screen);
    lv_obj_remove_style_all(topbar);
    lv_obj_set_size(topbar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(topbar, 12, 0);
    lv_obj_set_style_pad_hor(topbar, 16, 0);
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

    /* Back icon (fa-arrow-left) */
    lv_obj_t * back_icon = lv_label_create(back_btn);
    lv_obj_set_style_text_font(back_icon, app_font_fa6_20(), 0);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0x9AB8B0), 0);
    lv_label_set_text(back_icon, "\xEF\x81\x93");  /* fa-arrow-left */

    /* Back text */
    lv_obj_t * back_text = lv_label_create(back_btn);
    lv_label_set_text(back_text, "返回首页");
    lv_obj_set_style_text_font(back_text, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(back_text, lv_color_hex(0x9AB8B0), 0);

    /* Title */
    lv_obj_t * title = lv_label_create(topbar);
    lv_label_set_text(title, "图片浏览");
    lv_obj_set_style_text_font(title, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);

    /* Counter (right) */
    ctx->counter_label = lv_label_create(topbar);
    lv_obj_set_style_text_font(ctx->counter_label, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_color(ctx->counter_label, lv_color_hex(0x00D4AA), 0);
    if(image_count > 0) {
        lv_label_set_text_fmt(ctx->counter_label, "1 / %d", image_count);
    } else {
        lv_label_set_text(ctx->counter_label, "0 / 0");
    }

    /* ===== IMAGE AREA (flex-grow fills remaining space) ===== */
    lv_obj_t * image_area = lv_obj_create(screen);
    lv_obj_remove_style_all(image_area);
    lv_obj_set_size(image_area, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(image_area, 1);
    lv_obj_set_flex_flow(image_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(image_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(image_area, 12, 0);
    lv_obj_set_style_pad_hor(image_area, 20, 0);
    NO_SCROLL(image_area);

    /* Inner frame — swipe detection happens here */
    ctx->image_frame = lv_obj_create(image_area);
    lv_obj_remove_style_all(ctx->image_frame);
    lv_obj_set_size(ctx->image_frame, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->image_frame, lv_color_hex(0x0A1620), 0);
    lv_obj_set_style_border_width(ctx->image_frame, 2, 0);
    lv_obj_set_style_border_color(ctx->image_frame, lv_color_hex(0x1C2E36), 0);
    lv_obj_set_style_radius(ctx->image_frame, 16, 0);
    lv_obj_set_style_shadow_width(ctx->image_frame, 20, 0);
    lv_obj_set_style_shadow_color(ctx->image_frame, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->image_frame, LV_OPA_40, 0);
    lv_obj_set_style_shadow_ofs_y(ctx->image_frame, 8, 0);
    lv_obj_set_style_clip_corner(ctx->image_frame, true, 0);
    lv_obj_set_flex_flow(ctx->image_frame, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->image_frame, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    NO_SCROLL(ctx->image_frame);

    /* Manual swipe tracking on the image frame */
    lv_obj_add_event_cb(ctx->image_frame, on_frame_pressed,  LV_EVENT_PRESSED,  ctx);
    lv_obj_add_event_cb(ctx->image_frame, on_frame_released, LV_EVENT_RELEASED, ctx);
    /* Recalculate scale when frame gets its final size */
    lv_obj_add_event_cb(ctx->image_frame, on_frame_size_changed, LV_EVENT_SIZE_CHANGED, ctx);

    /* Image widget */
    ctx->image_obj = lv_image_create(ctx->image_frame);
    lv_obj_set_size(ctx->image_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(ctx->image_obj, 8, 0);
    lv_obj_set_style_clip_corner(ctx->image_obj, true, 0);
    /* Inner align: center the (scaled) image within the widget */
    lv_image_set_inner_align(ctx->image_obj, LV_IMAGE_ALIGN_CENTER);

    /* Empty state label (hidden when images exist) */
    ctx->empty_label = lv_label_create(ctx->image_frame);
    lv_label_set_text(ctx->empty_label, "暂无图片");
    lv_obj_set_style_text_font(ctx->empty_label, app_font_kaiti_18(), 0);
    lv_obj_set_style_text_color(ctx->empty_label, lv_color_hex(0x5A7A72), 0);
    lv_obj_center(ctx->empty_label);

    /* ===== DOTS INDICATOR — single label with ● ○ chars ===== */
    ctx->dots_label = lv_label_create(screen);
    lv_obj_set_style_text_font(ctx->dots_label, app_font_kaiti_14(), 0);
    lv_obj_set_style_text_align(ctx->dots_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ctx->dots_label, lv_pct(100));
    lv_obj_set_style_pad_ver(ctx->dots_label, 10, 0);
    NO_SCROLL(ctx->dots_label);

    /* Build initial dot string */
    if(image_count > 0) {
        char buf[256];
        int pos = 0;
        for(int i = 0; i < image_count; i++) {
            if(pos >= (int)sizeof(buf) - 8) break;
            if(i == 0) {
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                                "\xE2\x97\x8F");  /* ● U+25CF — active */
            } else {
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                                "  \xE2\x97\x8B"); /* ○ U+25CB — inactive with 2-space gap */
            }
        }
        lv_label_set_text(ctx->dots_label, buf);
    } else {
        lv_label_set_text(ctx->dots_label, "");
    }

    /* Show first image or empty state */
    update_display(ctx);

    /* Pre-decode remaining images in background (200ms apart, one at a time).
     * Each creates a hidden image widget to force LVGL to decode+render.
     * After decode, the bitmap is cached for instant reuse on swipe. */
    if(image_count > 1) {
        ctx->preload_idx = 1;
        ctx->preload_timer = lv_timer_create(preload_timer_cb, 200, ctx);
    } else {
        ctx->preload_timer = NULL;
    }

    return screen;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void on_back_click(lv_event_t * e)
{
    gallery_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->back_cb) {
        ctx->back_cb();
    }
}

/* Track press position for manual swipe detection */
static void on_frame_pressed(lv_event_t * e)
{
    gallery_page_ctx_t * ctx = lv_event_get_user_data(e);
    lv_indev_t * indev = lv_event_get_indev(e);
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    ctx->press_x = pt.x;
    ctx->press_y = pt.y;
}

/* Compare release position to detect swipe direction */
static void on_frame_released(lv_event_t * e)
{
    gallery_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->image_count == 0) return;

    lv_indev_t * indev = lv_event_get_indev(e);
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    lv_coord_t dx = pt.x - ctx->press_x;
    lv_coord_t dy = pt.y - ctx->press_y;

    /* Only treat as swipe if horizontal drag > vertical AND exceeds threshold */
    if(LV_ABS(dx) > LV_ABS(dy) && LV_ABS(dx) > SWIPE_THRESHOLD) {
        if(dx < 0) {
            /* Swipe left → next image */
            int next = (ctx->current_index + 1) % ctx->image_count;
            show_image(ctx, next);
        } else {
            /* Swipe right → previous image */
            int prev = (ctx->current_index - 1 + ctx->image_count) % ctx->image_count;
            show_image(ctx, prev);
        }
    }
}

/* Recalculate image scale when frame size is known */
static void on_frame_size_changed(lv_event_t * e)
{
    gallery_page_ctx_t * ctx = lv_event_get_user_data(e);
    recalc_scale(ctx);
}

static void recalc_scale(gallery_page_ctx_t * ctx)
{
    if(ctx->image_count == 0) return;

    lv_coord_t fw = lv_obj_get_content_width(ctx->image_frame);
    lv_coord_t fh = lv_obj_get_content_height(ctx->image_frame);
    if(fw < 10 || fh < 10) return;

    /* Get the decoded image's natural size from the image object */
    lv_image_header_t header;
    /* Try to read header from current source */
    if(lv_image_decoder_get_info(lv_image_get_src(ctx->image_obj), &header) != LV_RESULT_OK) {
        return;
    }
    if(header.w < 1 || header.h < 1) return;

    /* Target: fit image within 92% of frame */
    lv_coord_t tw = fw * 92 / 100;
    lv_coord_t th = fh * 92 / 100;

    int scale_x = (tw * 256) / header.w;
    int scale_y = (th * 256) / header.h;
    int scale = LV_MIN(scale_x, scale_y);
    if(scale < 8)  scale = 8;
    if(scale > 512) scale = 512;

    lv_image_set_scale(ctx->image_obj, (uint16_t)scale);
}

static void update_display(gallery_page_ctx_t * ctx)
{
    if(ctx->image_count == 0) {
        lv_obj_add_flag(ctx->image_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->empty_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ctx->counter_label, "0 / 0");
    } else {
        lv_obj_remove_flag(ctx->image_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->empty_label, LV_OBJ_FLAG_HIDDEN);
        show_image(ctx, ctx->current_index);
    }
}

static void show_image(gallery_page_ctx_t * ctx, int index)
{
    if(index < 0 || index >= ctx->image_count) return;

    ctx->current_index = index;

    /* Update image source — scale recalc is async via on_frame_size_changed */
    lv_image_set_src(ctx->image_obj, ctx->image_paths[index]);

    /* Update counter */
    lv_label_set_text_fmt(ctx->counter_label, "%d / %d",
                          index + 1, ctx->image_count);

    /* Update dots label */
    if(ctx->dots_label && ctx->image_count > 0) {
        char buf[256];
        int pos = 0;
        for(int i = 0; i < ctx->image_count; i++) {
            if(pos >= (int)sizeof(buf) - 8) break;
            if(i > 0) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "  "); /* 2-space gap */
            }
            if(i == index) {
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                                "\xE2\x97\x8F");  /* ● U+25CF — active */
            } else {
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                                "\xE2\x97\x8B");  /* ○ U+25CB — inactive */
            }
        }
        lv_label_set_text(ctx->dots_label, buf);
    }
}

/* Background timer: pre-decode images one by one so swipes feel instant.
 * Creates a tiny hidden lv_image to trigger decode, then deletes it
 * after the next render — the decoded bitmap stays in LVGL's image cache. */
static void preload_timer_cb(lv_timer_t * timer)
{
    gallery_page_ctx_t * ctx = lv_timer_get_user_data(timer);
    if(ctx->preload_idx >= ctx->image_count) {
        lv_timer_delete(ctx->preload_timer);
        ctx->preload_timer = NULL;
        LV_LOG_USER("gallery: all %d images pre-decoded", ctx->image_count);
        return;
    }

    /* Create a tiny hidden image widget on the gallery screen to trigger
     * LVGL to decode the PNG. Once decoded, the bitmap goes into LVGL's
     * image cache (LV_CACHE_DEF_SIZE controls how many stay cached).
     * The 1×1 hidden widget itself has negligible overhead. */
    lv_obj_t * tmp = lv_image_create(ctx->screen);
    lv_obj_set_size(tmp, 1, 1);
    lv_obj_add_flag(tmp, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(tmp, ctx->image_paths[ctx->preload_idx]);

    LV_LOG_USER("gallery: pre-decode queued [%d/%d]",
                ctx->preload_idx + 1, ctx->image_count);
    ctx->preload_idx++;
}

static void on_page_delete(lv_event_t * e)
{
    gallery_page_ctx_t * ctx = lv_event_get_user_data(e);
    if(ctx->preload_timer) {
        lv_timer_delete(ctx->preload_timer);
    }
    lv_free(ctx);
}
