#ifndef CONNECT_WIFI_H_
#define CONNECT_WIFI_H_

#include "esp_err.h"
#include "global_state.h"

/**
 * @brief Initialize WiFi STA mode
 *
 * @param GLOBAL_STATE Pointer to global state
 * @return esp_err_t ESP_OK on success
 */
esp_err_t connect_wifi_init(GlobalState *GLOBAL_STATE);

#endif /* CONNECT_WIFI_H_ */
