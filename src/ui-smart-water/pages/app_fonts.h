// ========== app_fonts.h ==========
#ifndef APP_FONTS_H
#define APP_FONTS_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/* Initialize freetype and load all fonts.
 * Call once before creating any UI. */
void app_fonts_init(void);

/* Get loaded fonts */
const lv_font_t * app_font_kaiti_14(void);
const lv_font_t * app_font_kaiti_18(void);
const lv_font_t * app_font_kaiti_24(void);
const lv_font_t * app_font_fa6_20(void);

#ifdef __cplusplus
}
#endif
#endif
