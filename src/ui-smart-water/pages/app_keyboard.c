// ========== app_keyboard.c ==========
#include "app_keyboard.h"
#include "app_fonts.h"
#include "pinyin-ime/lv_100ask_pinyin_ime.h"
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#define NO_SCROLL(obj) \
    lv_obj_set_scrollbar_mode((obj), LV_SCROLLBAR_MODE_OFF); \
    lv_obj_clear_flag((obj), LV_OBJ_FLAG_SCROLLABLE)

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * g_kb_screen   = NULL;
static lv_obj_t * g_pinyin_ime  = NULL;  /* Pinyin IME composite */
static lv_obj_t * g_kb          = NULL;  /* Underlying keyboard */
static lv_obj_t * g_cand_panel  = NULL;  /* Candidate panel */
static lv_obj_t * g_bound_ta    = NULL;

static void (*g_confirm_cb)(void *) = NULL;
static void * g_confirm_data = NULL;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void on_kb_ready(lv_event_t * e);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void app_keyboard_show(lv_obj_t * screen, lv_obj_t * ta, const char * field_label)
{
    (void)field_label;
    if (ta == NULL) return;

    /* Already bound? Just ensure visible */
    if (g_bound_ta == ta && g_pinyin_ime != NULL) {
        lv_obj_clear_flag(g_pinyin_ime, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Unbind previous */
    if (g_bound_ta) {
        lv_obj_remove_event_cb(g_bound_ta, on_kb_ready);
    }

    g_bound_ta = ta;
    g_kb_screen = screen;

    /* Create pinyin IME once. It manages kb + cand_panel internally.
     * Follows the original 100ask demo pattern exactly. */
    if (g_pinyin_ime == NULL) {
        /* Create on layer_top so it floats above all screens */
        g_pinyin_ime = lv_100ask_pinyin_ime_create(lv_layer_top());
        /* Constructor sets obj=PCT(100)xPCT(55), BOTTOM_MID.
         * Override: zero-height anchor at absolute bottom so kb sits flush. */
        lv_obj_set_size(g_pinyin_ime, 800, 0);
        lv_obj_align(g_pinyin_ime, LV_ALIGN_BOTTOM_MID, 0, 0);

        g_kb = lv_100ask_pinyin_ime_get_kb(g_pinyin_ime);
        g_cand_panel = lv_100ask_pinyin_ime_get_cand_panel(g_pinyin_ime);

        /* Set Chinese font on candidate panel */
        lv_obj_set_style_text_font(g_cand_panel, app_font_kaiti_18(),
                                   LV_PART_ITEMS);

        /* Keyboard: flush to bottom, full width */
        lv_obj_set_size(g_kb, 800, 200);
        lv_keyboard_set_mode(g_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        /* Re-align kb + cand_panel now that anchor has final 0-size */
        lv_obj_align_to(g_kb, g_pinyin_ime, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_align_to(g_cand_panel, g_kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    }

    /* Bind keyboard to textarea */
    lv_keyboard_set_textarea(g_kb, ta);
    lv_obj_add_event_cb(ta, on_kb_ready, LV_EVENT_READY, NULL);

    /* Show */
    lv_obj_clear_flag(g_pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    /* cand_panel is shown/hidden by pinyin IME internally */
}

void app_keyboard_hide(void)
{
    if (g_pinyin_ime) lv_obj_add_flag(g_pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    if (g_kb)         lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    if (g_cand_panel) lv_obj_add_flag(g_cand_panel, LV_OBJ_FLAG_HIDDEN);

    if (g_kb) {
        /* Clear internal ta ref so re-bind works */
        lv_keyboard_set_textarea(g_kb, NULL);
    }
    if (g_bound_ta) {
        lv_obj_remove_event_cb(g_bound_ta, on_kb_ready);
        /* Clear focus state — otherwise next click won't fire LV_EVENT_FOCUSED */
        lv_obj_clear_state(g_bound_ta, LV_STATE_FOCUSED);
        g_bound_ta = NULL;
    }
}

bool app_keyboard_is_showing(void)
{
    return g_pinyin_ime != NULL
        && !lv_obj_has_flag(g_pinyin_ime, LV_OBJ_FLAG_HIDDEN);
}

void app_keyboard_set_confirm_cb(void (*cb)(void *), void * user_data)
{
    g_confirm_cb = cb;
    g_confirm_data = user_data;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void on_kb_ready(lv_event_t * e)
{
    (void)e;
    app_keyboard_hide();
    if (g_confirm_cb) {
        g_confirm_cb(g_confirm_data);
    }
}
