/**
 * @file main.c
 * LVGL v9 PC Simulator — Smart Water Aquaculture System
 */

/*********************
 *      INCLUDES
 *********************/

#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE /* needed for usleep() */
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
  #include <Windows.h>
#else
  #include <unistd.h>
  #include <pthread.h>
#endif
#include "lvgl/lvgl.h"
#include "lvgl/examples/lv_examples.h"
#include "lvgl/demos/lv_demos.h"
#include <SDL.h>

#include "hal/hal.h"
#include "src/ui-smart-water/ui.h"

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

#if LV_USE_OS != LV_OS_FREERTOS

int main(int argc, char **argv)
{
  (void)argc; /*Unused*/
  (void)argv; /*Unused*/

  /*Initialize LVGL*/
  lv_init();

  /*Initialize the HAL (display, input devices, tick) for LVGL*/
  sdl_hal_init(800, 480);

  /* Default theme fallback */
  lv_theme_default_init(lv_display_get_default(),
                         lv_palette_main(LV_PALETTE_BLUE),
                         lv_palette_main(LV_PALETTE_RED),
                         true, LV_FONT_DEFAULT);

  /* ===== SINGLE ENTRY POINT =====
   * ui_init() handles: font loading, screen creation,
   * callback wiring, navigation, and loads the login page.
   */
  ui_init();

  /* Main loop */
  while(1) {
    uint32_t sleep_time_ms = lv_timer_handler();
    if(sleep_time_ms == LV_NO_TIMER_READY){
      sleep_time_ms =  LV_DEF_REFR_PERIOD;
    }
#ifdef _MSC_VER
    Sleep(sleep_time_ms);
#else
    usleep(sleep_time_ms * 1000);
#endif
  }

  return 0;
}

#endif
