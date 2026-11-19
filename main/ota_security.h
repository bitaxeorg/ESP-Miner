#ifndef OTA_SECURITY_H_
#define OTA_SECURITY_H_

#include <stdbool.h>
#include <stdint.h>

void ota_security_unlock_from_button(void);
void ota_security_lock(void);
uint32_t ota_security_remaining_seconds(void);
bool ota_security_is_update_allowed(void);

#endif
