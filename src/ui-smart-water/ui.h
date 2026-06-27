// ========== ui.h ==========
// Single entry point for the entire UI.
// Call ui_init() once after lv_init() and hal init.
#ifndef UI_H
#define UI_H
#ifdef __cplusplus
extern "C" {
#endif

/** Initialize fonts, create all screens, wire callbacks, load login page.
 *  Call once after lv_init() + sdl_hal_init(). */
void ui_init(void);

#ifdef __cplusplus
}
#endif
#endif
