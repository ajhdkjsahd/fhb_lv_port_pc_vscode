// ========== register_page.h ==========
#ifndef REGISTER_PAGE_H
#define REGISTER_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/* Callback: called when register button is pressed.
 * Return true if registration succeeds, false otherwise. */
typedef bool (*register_submit_cb_t)(const char * username, const char * password);

/* Callback: called when back button is pressed (navigate to login page) */
typedef void (*navigate_back_cb_t)(void);

/* Create the register page screen. */
lv_obj_t * register_page_create(register_submit_cb_t submit_cb, navigate_back_cb_t back_cb);

#ifdef __cplusplus
}
#endif
#endif
