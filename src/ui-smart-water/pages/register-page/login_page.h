// ========== login_page.h ==========
#ifndef LOGIN_PAGE_H
#define LOGIN_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/* Callback: called when login button is pressed.
 * username/password are the textarea contents.
 * Return true if login is valid, false otherwise. */
typedef bool (*login_verify_cb_t)(const char * username, const char * password);

/* Callback: called when register button is pressed (navigate to register page) */
typedef void (*navigate_to_register_cb_t)(void);

/* Callback: called after success popup timer fires (for navigating to main app) */
typedef void (*post_success_cb_t)(void);

/* Create the login page screen.
 * verify_cb:      user login validation (return true = success)
 * nav_cb:         called when "注册" button is clicked
 * post_success_cb: called ~1.5s after login success popup (load main UI here) */
lv_obj_t * login_page_create(login_verify_cb_t       verify_cb,
                             navigate_to_register_cb_t nav_cb,
                             post_success_cb_t         post_success_cb);

#ifdef __cplusplus
}
#endif
#endif
