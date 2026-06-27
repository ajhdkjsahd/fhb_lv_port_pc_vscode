// ========== app_fonts.c ==========
#include "app_fonts.h"
#include <stdio.h>

/*********************
 *      DEFINES
 *********************/
#ifdef __linux__
/* ARM Linux board — fonts in current directory */
#define FONT_SIMKAI  "/root/fonts/SIMKAI.TTF"
#define FONT_FA6     "/root/fonts/FA6-Free-Solid-900.otf"
#else
/* Windows PC — fonts relative to bin/ working directory */
#define FONT_SIMKAI  "../src/ui-smart-water/fonts/SIMKAI.TTF"
#define FONT_FA6     "../src/ui-smart-water/fonts/FA6-Free-Solid-900.otf"
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_font_t * kaiti_14 = NULL;
static lv_font_t * kaiti_18 = NULL;
static lv_font_t * kaiti_24 = NULL;
static lv_font_t * fa6_20   = NULL;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void app_fonts_init(void)
{
    /* Initialize freetype library.
     * Note: lv_init() may already have called lv_freetype_init() internally,
     * so failure here is not fatal — freetype is already up. */
    lv_result_t res = lv_freetype_init(512);
    if(res != LV_RESULT_OK) {
        LV_LOG_WARN("FreeType init returned non-OK (may already be initialized)");
        /* Continue anyway — freetype is likely already running */
    }

    /* Load SIMKAI.TTF at multiple sizes for Chinese text */
    kaiti_14 = lv_freetype_font_create(FONT_SIMKAI,
                                       LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                       14,
                                       LV_FREETYPE_FONT_STYLE_NORMAL);
    if(kaiti_14) LV_LOG_USER("Loaded kaiti_14");

    kaiti_18 = lv_freetype_font_create(FONT_SIMKAI,
                                       LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                       18,
                                       LV_FREETYPE_FONT_STYLE_NORMAL);
    if(kaiti_18) LV_LOG_USER("Loaded kaiti_18");

    kaiti_24 = lv_freetype_font_create(FONT_SIMKAI,
                                       LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                       24,
                                       LV_FREETYPE_FONT_STYLE_NORMAL);
    if(kaiti_24) LV_LOG_USER("Loaded kaiti_24");

    /* Load FA6 icon font */
    fa6_20 = lv_freetype_font_create(FONT_FA6,
                                     LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                     20,
                                     LV_FREETYPE_FONT_STYLE_NORMAL);
    if(fa6_20) LV_LOG_USER("Loaded fa6_20");
}

const lv_font_t * app_font_kaiti_14(void) { return kaiti_14; }
const lv_font_t * app_font_kaiti_18(void) { return kaiti_18; }
const lv_font_t * app_font_kaiti_24(void) { return kaiti_24; }
const lv_font_t * app_font_fa6_20(void)   { return fa6_20; }
