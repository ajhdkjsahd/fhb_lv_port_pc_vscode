// ========== gallery_page.h ==========
#ifndef GALLERY_PAGE_H
#define GALLERY_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/** Callback: called when "返回首页" button is clicked */
typedef void (*gallery_back_cb_t)(void);

/** Create the image gallery (carousel) page screen.
 *  back_cb:      called when user clicks "返回首页" button
 *  image_paths:  array of image file paths (relative to working directory)
 *  image_count:  number of images in the array (0 = empty state) */
lv_obj_t * gallery_page_create(gallery_back_cb_t   back_cb,
                               const char * const  image_paths[],
                               int                 image_count);

#ifdef __cplusplus
}
#endif
#endif
