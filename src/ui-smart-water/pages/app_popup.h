// ========== app_popup.h ==========
#ifndef APP_POPUP_H
#define APP_POPUP_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/* Popup type */
typedef enum {
    APP_POPUP_SUCCESS = 0,
    APP_POPUP_ERROR   = 1,
} app_popup_type_t;

/* Show a toast popup on the given screen.
 * Auto-dismisses after 2000ms, or on click. */
void app_popup_show(lv_obj_t * screen, const char * text, app_popup_type_t type);

/* Dismiss the popup immediately if showing */
void app_popup_dismiss(void);

#ifdef __cplusplus
}
#endif
#endif
