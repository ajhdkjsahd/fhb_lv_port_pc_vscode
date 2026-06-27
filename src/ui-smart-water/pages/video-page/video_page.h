// ========== video_page.h ==========
#ifndef VIDEO_PAGE_H
#define VIDEO_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"
#include <stdbool.h>

/** Video control action types */
typedef enum {
    VIDEO_ACTION_PLAY_PAUSE,  /**< Toggle play / pause */
    VIDEO_ACTION_REWIND,      /**< Rewind 10 seconds */
    VIDEO_ACTION_FAST_FORWARD,/**< Fast forward 10 seconds */
    VIDEO_ACTION_VOLUME_UP,   /**< Increase volume */
    VIDEO_ACTION_VOLUME_DOWN, /**< Decrease volume */
} video_action_t;

/** Callback: called when a video control button is pressed */
typedef void (*video_control_cb_t)(video_action_t action);

/** Callback: called when user seeks via progress bar (seconds) */
typedef void (*video_seek_cb_t)(int32_t position);

/** Callback: called when "返回首页" button is clicked */
typedef void (*video_back_cb_t)(void);

/** Callback: called when user swipes to select a different video (index) */
typedef void (*video_select_cb_t)(int index);

/** Create the video player page screen.
 *  control_cb:   called for play/pause, rewind, ffwd, volume buttons
 *  seek_cb:      called when progress bar is dragged (seconds)
 *  back_cb:      called when back-to-home button is clicked
 *  select_cb:    called when user swipes to a new video (index)
 *  video_paths:  array of video file paths
 *  cover_paths:  array of cover image paths (can be NULL)
 *  video_count:  number of videos (0 = no videos) */
lv_obj_t * video_page_create(video_control_cb_t  control_cb,
                             video_seek_cb_t     seek_cb,
                             video_back_cb_t     back_cb,
                             video_select_cb_t   select_cb,
                             const char * const  video_paths[],
                             const char * const  cover_paths[],
                             int                 video_count);

/** Update the progress bar and time labels from external player. */
void video_page_update_progress(lv_obj_t * screen,
                                int32_t position,
                                const char * current_time,
                                const char * total_time);

/** Update the play/pause button icon. */
void video_page_set_play_state(lv_obj_t * screen, bool is_playing);

/** Toggle video overlay mode (transparent hole for mplayer). */
void video_page_set_video_active(lv_obj_t * screen, bool active);

/** Set the video duration (in seconds) to configure slider range. */
void video_page_set_duration(lv_obj_t * screen, int32_t duration_sec);

#ifdef __cplusplus
}
#endif
#endif
