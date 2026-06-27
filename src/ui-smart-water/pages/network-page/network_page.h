// ========== network_page.h ==========
#ifndef NETWORK_PAGE_H
#define NETWORK_PAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl/lvgl.h"

/** Callback: return to home page */
typedef void (*network_back_cb_t)(void);

/** Callback: user clicked "连接" with IP and port */
typedef void (*network_connect_cb_t)(const char * ip, const char * port);

/** Callback: user clicked "断开" */
typedef void (*network_disconnect_cb_t)(void);

/** Callback: user clicked "发送" with the typed message */
typedef void (*network_send_cb_t)(const char * message);

/** Create the network communication page.
 *  back_cb:       called when "返回首页" clicked
 *  connect_cb:    called when "连接" clicked (ip, port from input fields)
 *  disconnect_cb: called when "断开" clicked
 *  send_cb:       called when "发送" clicked (message from input field) */
lv_obj_t * network_page_create(network_back_cb_t       back_cb,
                               network_connect_cb_t    connect_cb,
                               network_disconnect_cb_t disconnect_cb,
                               network_send_cb_t       send_cb);

/** Message type for color-coding log lines */
typedef enum {
    NETWORK_MSG_INFO  = 0,   /* gray */
    NETWORK_MSG_SEND  = 1,   /* accent (cyan) */
    NETWORK_MSG_RECV  = 2,   /* accent2 (blue) */
    NETWORK_MSG_ERROR = 3,   /* red */
} network_msg_type_t;

/** Append a timestamped message to the log area */
void network_page_append_message(lv_obj_t * screen, network_msg_type_t type,
                                 const char * msg);

/** Update connection state UI (dot color, button text, input enabled) */
void network_page_set_connected(lv_obj_t * screen, bool connected);

/** Clear all messages from the log area */
void network_page_clear_messages(lv_obj_t * screen);

#ifdef __cplusplus
}
#endif
#endif
