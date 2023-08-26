#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_
#include <esp_http_server.h>

esp_err_t start_rest_server(void *pvParameters);

#endif