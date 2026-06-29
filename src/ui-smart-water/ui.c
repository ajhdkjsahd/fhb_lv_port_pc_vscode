// ========== ui.c ==========
#include "ui.h"
#include "pages/app_fonts.h"
#include "pages/app_keyboard.h"
#include "pages/app_actions.h"

/*********************
 *      DEFINES
 *********************/
/* Image folder — change these to match your deployment layout */
#ifdef __linux__
#define IMAGES_DIR      "A:/root/images"
#define IMAGES_DIR_FMT  "A:/root/images/%s"
#else
#define IMAGES_DIR      "A:../src/ui-smart-water/images"
#define IMAGES_DIR_FMT  "A:../src/ui-smart-water/images/%s"
#endif
#include "pages/register-page/login_page.h"
#include "pages/register-page/register_page.h"
#include "pages/home-page/home_page.h"
#include "pages/video-page/video_page.h"
#include "pages/gallery-page/gallery_page.h"
#include "pages/network-page/network_page.h"
#include "pages/ai-chat-page/ai_chat_page.h"
#include <string.h>
#include <stdio.h>

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * g_login_screen    = NULL;
static lv_obj_t * g_register_screen = NULL;
static lv_obj_t * g_home_screen     = NULL;
static lv_obj_t * g_video_screen    = NULL;
static lv_obj_t * g_gallery_screen  = NULL;
static lv_obj_t * g_network_screen  = NULL;
static lv_obj_t * g_ai_chat_screen  = NULL;

/* Image paths — dynamically scanned from images/ folder.
 * "A:" prefix uses LVGL's STDIO filesystem driver (LV_FS_STDIO_LETTER='A'). */
static char ** g_image_paths = NULL;
static int     g_image_count = 0;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void nav_to_register(void);
static void nav_to_login(void);
static void nav_to_home(void);
static void nav_to_video(void);
static void nav_to_gallery(void);
static void nav_to_network(void);
static void nav_to_ai_chat(void);
static void on_login_success_done(void);

/* Page transition: forward=slide-left, back=slide-right, fade for special */
#define PAGE_FWD   LV_SCR_LOAD_ANIM_MOVE_LEFT,  350, 0, false
#define PAGE_BACK  LV_SCR_LOAD_ANIM_MOVE_RIGHT, 350, 0, false
#define PAGE_FADE  LV_SCR_LOAD_ANIM_FADE_ON,    400, 0, false

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/* Automatically scan the images/ folder for .png files.
 * Uses LVGL's FS STDIO driver with a safety iteration cap.
 * Populates g_image_paths (dynamically allocated) and g_image_count. */
static void scan_images_dir(void)
{
    lv_fs_dir_t dir;
    const char * dir_path = IMAGES_DIR;
    char fn[256];
    int i;

    /* Open directory */
    if(lv_fs_dir_open(&dir, dir_path) != LV_FS_RES_OK) {
        LV_LOG_USER("gallery: cannot open images dir: %s", dir_path);
        g_image_count = 0;
        g_image_paths = NULL;
        return;
    }

    /* First pass: count .png files. SAFETY: max 256 iterations */
    int count = 0;
    for(i = 0; i < 256; i++) {
        if(lv_fs_dir_read(&dir, fn, sizeof(fn)) != LV_FS_RES_OK) break;
        if(fn[0] == '\0') continue;
        size_t len = strlen(fn);
        if(len >= 4 && strcmp(fn + len - 4, ".png") == 0) {
            if(count >= 10) break;  /* cap at 10 images */
            count++;
        }
    }
    lv_fs_dir_close(&dir);

    LV_LOG_USER("gallery: found %d .png files", count);

    if(count == 0) {
        g_image_count = 0;
        g_image_paths = NULL;
        return;
    }

    /* Allocate path array */
    g_image_paths = lv_malloc(sizeof(char *) * count);
    if(g_image_paths == NULL) {
        g_image_count = 0;
        return;
    }
    memset(g_image_paths, 0, sizeof(char *) * count);

    /* Second pass: collect filenames. SAFETY: max 256 iterations */
    if(lv_fs_dir_open(&dir, dir_path) != LV_FS_RES_OK) {
        lv_free(g_image_paths);
        g_image_paths = NULL;
        g_image_count = 0;
        return;
    }

    int idx = 0;
    for(i = 0; i < 256 && idx < count; i++) {
        if(lv_fs_dir_read(&dir, fn, sizeof(fn)) != LV_FS_RES_OK) break;
        if(fn[0] == '\0') continue;
        size_t len = strlen(fn);
        if(len >= 4 && strcmp(fn + len - 4, ".png") == 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path),
                     IMAGES_DIR_FMT, fn);
            g_image_paths[idx] = lv_malloc(strlen(full_path) + 1);
            if(g_image_paths[idx]) {
                strcpy(g_image_paths[idx], full_path);
                LV_LOG_USER("gallery: [%d] %s", idx, fn);
            }
            idx++;
        }
    }
    lv_fs_dir_close(&dir);
    g_image_count = idx;
}

void ui_init(void)
{
    /* 1. Initialize FreeType and load all fonts */
    app_fonts_init();

    /* 1.5. Create default input group (needed for keyboard/encoder nav on board) */
    lv_group_set_default(lv_group_create());

    /* 2. Scan images + videos folders */
    scan_images_dir();
    app_action_video_scan();

    /* 3. Create all screens (callbacks wired to app_actions) */
    g_login_screen    = login_page_create(app_action_login_verify,
                                          nav_to_register,
                                          on_login_success_done);
    g_register_screen = register_page_create(app_action_register_submit,
                                             nav_to_login);
    g_home_screen     = home_page_create(nav_to_video, nav_to_gallery, nav_to_network, nav_to_ai_chat);
    /* Build cover paths from video scan results */
    int vcount = app_action_video_get_count();
    const char ** covers = NULL;
    if(vcount > 0) {
        covers = lv_malloc(sizeof(const char *) * vcount);
        if(covers) {
            for(int i = 0; i < vcount; i++) {
                covers[i] = app_action_video_get_cover(i);
            }
        }
    }
    g_video_screen    = video_page_create(app_action_video_control,
                                          app_action_video_seek,
                                          nav_to_home,
                                          app_action_video_select,
                                          app_action_video_get_paths(),
                                          covers,
                                          vcount);
    g_gallery_screen  = gallery_page_create(nav_to_home,
                                            (const char * const *)g_image_paths,
                                            g_image_count);
    g_network_screen  = network_page_create(nav_to_home,
                                            app_action_network_connect,
                                            app_action_network_disconnect,
                                            app_action_network_send);
    app_action_set_video_screen(g_video_screen);
    app_action_set_network_screen(g_network_screen);

    /* AI Chat page */
    g_ai_chat_screen  = ai_chat_page_create(nav_to_home,
                                            app_action_ai_send,
                                            app_action_ai_stop);
    app_action_ai_set_screen(g_ai_chat_screen);
    app_action_ai_init();

    /* 4. Start on login screen */
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

static void nav_to_home(void)
{
    app_action_video_stop();
    if(g_home_screen) {
        lv_screen_load_anim(g_home_screen, PAGE_BACK);
    }
}

static void nav_to_video(void)
{
    if(g_video_screen) {
        lv_screen_load_anim(g_video_screen, PAGE_FWD);
    }
}

static void nav_to_gallery(void)
{
    if(g_gallery_screen) {
        lv_screen_load_anim(g_gallery_screen, PAGE_FWD);
    }
}

static void nav_to_network(void)
{
    if(g_network_screen) {
        lv_screen_load_anim(g_network_screen, PAGE_FWD);
    }
}

static void nav_to_ai_chat(void)
{
    if(g_ai_chat_screen) {
        lv_screen_load_anim(g_ai_chat_screen, PAGE_FWD);
    }
}

static void on_login_success_done(void)
{
    app_keyboard_hide();
    app_action_login_success();
    lv_screen_load_anim(g_home_screen, PAGE_FADE);
}
