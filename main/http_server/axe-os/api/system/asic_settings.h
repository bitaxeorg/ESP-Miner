#ifndef ASIC_API_SETTINGS_H_
#define ASIC_API_SETTINGS_H_

#include <esp_http_server.h>
#include "global_state.h"

// Function to handle the /api/system/asic endpoint
esp_err_t GET_system_asic(httpd_req_t *req);

// Initialize the ASIC API with the global state
void asic_api_init(GlobalState *global_state);

#endif // ASIC_API_SETTINGS_H_
