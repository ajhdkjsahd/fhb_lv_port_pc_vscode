// ========== login_ui.c ==========
#include "login_ui.h"
#include "login_page.h"
#include "register_page.h"
#include "../app_fonts.h"
#include "../app_keyboard.h"

/*********************
 *      DEFINES
 *********************/
#define PAGE_FWD   LV_SCR_LOAD_ANIM_MOVE_LEFT,  350, 0, false
#define PAGE_BACK  LV_SCR_LOAD_ANIM_MOVE_RIGHT, 350, 0, false
#define PAGE_FADE  LV_SCR_LOAD_ANIM_FADE_ON,    400, 0, false

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * g_login_screen    = NULL;
static lv_obj_t * g_register_screen = NULL;
static login_success_cb_t g_login_success_cb = NULL;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void nav_to_register(void);
static void nav_to_login(void);
static void on_login_success(void);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void app_login_ui_start(login_verify_cb_t    verify_cb,
                        register_submit_cb_t  register_cb,
                        login_success_cb_t    success_cb)
{
    g_login_success_cb = success_cb;

    /* 1. Initialize FreeType and load all fonts */
    app_fonts_init();

    /* 2. Create screens */
    g_login_screen    = login_page_create(verify_cb, nav_to_register, on_login_success);
    g_register_screen = register_page_create(register_cb, nav_to_login);

    /* 3. Load login screen */
    lv_screen_load_anim(g_login_screen, PAGE_FADE);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void nav_to_register(void)
{
    app_keyboard_hide();
    if(g_register_screen) {
        lv_screen_load_anim(g_register_screen, PAGE_FWD);
    }
}

static void nav_to_login(void)
{
    app_keyboard_hide();
    if(g_login_screen) {
        lv_screen_load_anim(g_login_screen, PAGE_BACK);
    }
}

static void on_login_success(void)
{
    /* Called after the success popup timer fires.
     * Dismiss keyboard, then call the user's success handler
     * which should load the main application screen. */
    app_keyboard_hide();

    if(g_login_success_cb) {
        g_login_success_cb();
    }
}
