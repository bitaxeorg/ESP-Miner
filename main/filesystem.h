#ifndef FILESYSTEM_H_
#define FILESYSTEM_H_

#include "esp_err.h"

/**
 * @brief Initialize the SPIFFS filesystem
 *
 * This function mounts the SPIFFS partition and can be used
 * independently of the REST server for self-test mode.
 *
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t filesystem_init(void * pvParameters);

#endif /* FILESYSTEM_H_ */
