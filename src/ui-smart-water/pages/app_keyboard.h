// ========== app_keyboard.h ==========
#ifndef APP_KEYBOARD_H
#define APP_KEYBOARD_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/* Show the custom keyboard on the given screen, bound to a textarea.
 * field_label is shown in the display row (e.g. "账号", "密码"). */
void app_keyboard_show(lv_obj_t * screen, lv_obj_t * ta, const char * field_label);

/* Hide the keyboard */
void app_keyboard_hide(void);

/* Check if keyboard is currently visible */
bool app_keyboard_is_showing(void);

/* Register a callback for when the keyboard's "confirm" key is pressed */
void app_keyboard_set_confirm_cb(void (*cb)(void *), void * user_data);

#ifdef __cplusplus
}
#endif
#endif
