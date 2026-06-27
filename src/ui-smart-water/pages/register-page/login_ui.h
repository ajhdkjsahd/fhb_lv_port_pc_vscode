// ========== login_ui.h ==========
#ifndef LOGIN_UI_H
#define LOGIN_UI_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/* ===== User-provided callbacks ===== */

/* Login verification: return true if credentials are valid */
typedef bool (*login_verify_cb_t)(const char * username, const char * password);

/* Registration: return true if registration succeeds */
typedef bool (*register_submit_cb_t)(const char * username, const char * password);

/* Called after successful login (popup shown, then this navigates away) */
typedef void (*login_success_cb_t)(void);

/* ===== Main entry point ===== */

/* Initialize fonts, create login & register screens, load the login screen.
 *
 * verify_cb:   your login validation — return true on success
 * register_cb:  your registration handler — return true on success
 * success_cb:   called ~1.5s after login success popup, load your main UI here
 */
void app_login_ui_start(login_verify_cb_t    verify_cb,
                        register_submit_cb_t  register_cb,
                        login_success_cb_t    success_cb);

#ifdef __cplusplus
}
#endif
#endif
