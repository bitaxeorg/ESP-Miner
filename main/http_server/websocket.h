#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

void websocket_set_log_task_handle(TaskHandle_t task_handle);

#define MESSAGE_QUEUE_SIZE (128)
#define MAX_WEBSOCKET_CLIENTS (10)
#define LOG_BUFFER_SIZE  (512 * 1024)  /* 512 KB */

typedef enum {
    WS_TYPE_LOGS,
    WS_TYPE_API,
    WS_TYPE_MAX
} WebSocketClientType;

esp_err_t websocket_add_client(int fd, WebSocketClientType type);
void websocket_remove_client(int fd);
void websocket_broadcast(WebSocketClientType type, httpd_ws_frame_t *pkt);
void websocket_send_to_client(int fd, httpd_ws_frame_t *pkt);
int websocket_get_active_client_count(WebSocketClientType type);
void websocket_init(httpd_handle_t server);
esp_err_t websocket_handler(httpd_req_t *req);

void websocket_close_fn(httpd_handle_t hd, int sockfd);

/**
 * Notifies the WebSocket task that new log data is available in the ring buffer.
 * Non-blocking, safe to call from log hook.
 */
void websocket_log_notify(void);

#endif /* WEBSOCKET_H_ */
