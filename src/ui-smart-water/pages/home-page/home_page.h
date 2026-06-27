// ========== home_page.h ==========
#ifndef HOME_PAGE_H
#define HOME_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/** Callback: called when the "视频监控" button is clicked */
typedef void (*home_nav_to_video_cb_t)(void);

/** Callback: called when the "图片浏览" button is clicked */
typedef void (*home_nav_to_gallery_cb_t)(void);

/** Callback: called when the "网络通讯" button is clicked */
typedef void (*home_nav_to_network_cb_t)(void);

/** Create the home (dashboard) page screen.
 *  nav_to_video_cb:    called when user clicks "视频监控" button
 *  nav_to_gallery_cb:  called when user clicks "图片浏览" button
 *  nav_to_network_cb:  called when user clicks "网络通讯" button */
lv_obj_t * home_page_create(home_nav_to_video_cb_t    nav_to_video_cb,
                            home_nav_to_gallery_cb_t  nav_to_gallery_cb,
                            home_nav_to_network_cb_t  nav_to_network_cb);

#ifdef __cplusplus
}
#endif
#endif
