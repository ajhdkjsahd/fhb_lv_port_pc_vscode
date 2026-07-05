// ========== sensor_page.h ==========
#ifndef SENSOR_PAGE_H
#define SENSOR_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/** Callback: called when the "返回首页" button is clicked */
typedef void (*sensor_back_cb_t)(void);

/** Create the sensor-monitoring page screen.
 *  back_cb : called when user clicks the back button (navigate to home). */
lv_obj_t * sensor_page_create(sensor_back_cb_t back_cb);

#ifdef __cplusplus
}
#endif
#endif
