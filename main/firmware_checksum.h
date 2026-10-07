#ifndef FIRMWARE_CHECKSUM_H_
#define FIRMWARE_CHECKSUM_H_

#include <stdint.h>
#include "esp_err.h"

/**
 * Returns the SHA-256 of the running app image as a lowercase hex string.
 *
 * The hash covers exactly the flashed image (header, segments, checksum padding
 * and appended digest), so it matches `sha256sum esp-miner.bin`. It is computed
 * on the first call and cached, since the running image cannot change until reboot.
 *
 * @param sha256_hex Receives a pointer to the cached 64-character hex string.
 * @param image_len  Receives the image size in bytes (may be NULL).
 */
esp_err_t firmware_checksum_get_running(const char **sha256_hex, uint32_t *image_len);

#endif /* FIRMWARE_CHECKSUM_H_ */
