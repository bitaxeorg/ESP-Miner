#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include "esp_err.h"

esp_err_t websocket_handler(httpd_req_t * req);
void websocket_task(void * pvParameters);

#endif /* WEBSOCKET_H_ */