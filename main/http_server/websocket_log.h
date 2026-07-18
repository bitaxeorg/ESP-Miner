#ifndef WEBSOCKET_LOG_H
#define WEBSOCKET_LOG_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void websocket_log_task(void *pvParameters);

#endif // WEBSOCKET_LOG_H
