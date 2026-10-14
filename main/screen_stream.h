#ifndef SCREEN_STREAM_H
#define SCREEN_STREAM_H

#include "esp_err.h"
#include "esp_http_server.h"

#define MAX_SCREEN_STREAM_CLIENTS 4

/**
 * Initialize the screen streaming module
 * Creates the background task for frame capture
 */
esp_err_t screen_stream_init(void);

/**
 * Add a client to the screen stream
 * @param async_req The async request handle from the HTTP server
 * @return ESP_OK on success, ESP_FAIL if max clients reached
 */
esp_err_t screen_stream_add_client(httpd_req_t *async_req);

/**
 * Remove a client from the screen stream
 * @param async_req The async request handle
 */
void screen_stream_remove_client(httpd_req_t *async_req);

/**
 * Close all clients and cleanup
 */
void screen_stream_cleanup(void);

#endif // SCREEN_STREAM_H
