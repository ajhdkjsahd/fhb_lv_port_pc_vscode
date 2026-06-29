// ========== ai_chat_page.h ==========
#ifndef AI_CHAT_PAGE_H
#define AI_CHAT_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/** Callback: return to home page */
typedef void (*ai_chat_back_cb_t)(void);

/** Callback: user clicked send with the typed message */
typedef void (*ai_chat_send_cb_t)(const char * message);

/** Callback: user clicked stop during generation */
typedef void (*ai_chat_stop_cb_t)(void);

/** Create the AI chat page.
 *  back_cb: called when back button clicked
 *  send_cb: called when user sends a message
 *  stop_cb: called when user stops generation */
lv_obj_t * ai_chat_page_create(ai_chat_back_cb_t  back_cb,
                               ai_chat_send_cb_t  send_cb,
                               ai_chat_stop_cb_t  stop_cb);

/* ── Thread-safe update functions (call via lv_async_call) ── */

/** Begin a new AI response bubble. Called once at start of streaming. */
void ai_chat_page_begin_response(lv_obj_t * screen);

/** Append thinking text to the current AI response. Creates think panel if needed. */
void ai_chat_page_append_thinking(lv_obj_t * screen, const char * text);

/** Collapse the thinking panel (call when thinking is done). */
void ai_chat_page_finish_thinking(lv_obj_t * screen);

/** Append answer text to the current AI bubble. */
void ai_chat_page_append_answer(lv_obj_t * screen, const char * text);

/** Finalize the AI response (remove streaming cursor, collapse thinking). */
void ai_chat_page_finish_response(lv_obj_t * screen);

/** Show an error message in the chat (e.g., connection failed). */
void ai_chat_page_show_error(lv_obj_t * screen, const char * msg);

/** Update online/offline indicator. */
void ai_chat_page_set_online(lv_obj_t * screen, bool online);

#ifdef __cplusplus
}
#endif
#endif
