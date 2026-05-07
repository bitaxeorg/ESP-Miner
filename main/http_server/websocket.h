#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define MESSAGE_QUEUE_SIZE (128)
#define MAX_WEBSOCKET_CLIENTS (10)
#define LOG_BUFFER_SIZE  (512 * 1024)  /* 512 KB */

esp_err_t websocket_handler(httpd_req_t * req);
void websocket_task(void * pvParameters);
void websocket_close_fn(httpd_handle_t hd, int sockfd);

/**
 * Notifies the WebSocket task that new log data is available in the ring buffer.
 * Non-blocking, safe to call from log hook.
 */
void websocket_log_notify(void);

#endif /* WEBSOCKET_H_ */
