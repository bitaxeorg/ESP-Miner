#ifndef BZM_ESP_H
#define BZM_ESP_H
#include <stdbool.h>
/* Bind before starting BZM tasks; other ASIC families keep their existing I/O. */
bool bzm_platform_esp_init(void);
#endif
