/**
 * @file esp_err.h
 * @brief ESP-IDF error codes stub for host-based testing
 *
 * This provides the esp_err_t type and error codes without ESP-IDF dependencies.
 */

#ifndef ESP_ERR_H_STUB
#define ESP_ERR_H_STUB

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t esp_err_t;

/* Error codes */
#define ESP_OK          0       /*!< Success */
#define ESP_FAIL        -1      /*!< Generic failure */

#define ESP_ERR_NO_MEM          0x101   /*!< Out of memory */
#define ESP_ERR_INVALID_ARG     0x102   /*!< Invalid argument */
#define ESP_ERR_INVALID_STATE   0x103   /*!< Invalid state */
#define ESP_ERR_INVALID_SIZE    0x104   /*!< Invalid size */
#define ESP_ERR_NOT_FOUND       0x105   /*!< Requested resource not found */
#define ESP_ERR_NOT_SUPPORTED   0x106   /*!< Operation not supported */
#define ESP_ERR_TIMEOUT         0x107   /*!< Operation timed out */
#define ESP_ERR_INVALID_RESPONSE 0x108  /*!< Received response was invalid */
#define ESP_ERR_INVALID_CRC     0x109   /*!< CRC or checksum was invalid */
#define ESP_ERR_INVALID_VERSION 0x10A   /*!< Version was invalid */
#define ESP_ERR_INVALID_MAC     0x10B   /*!< MAC address was invalid */
#define ESP_ERR_NOT_FINISHED    0x10C   /*!< Operation not finished yet */

#define ESP_ERR_WIFI_BASE       0x3000  /*!< Starting number of WiFi error codes */
#define ESP_ERR_MESH_BASE       0x4000  /*!< Starting number of MESH error codes */
#define ESP_ERR_FLASH_BASE      0x6000  /*!< Starting number of flash error codes */
#define ESP_ERR_HW_CRYPTO_BASE  0xc000  /*!< Starting number of HW crypto error codes */

/**
 * @brief Returns string representation of esp_err_t error code
 * @param code Error code
 * @return String representation
 */
const char *esp_err_to_name(esp_err_t code);

#ifdef __cplusplus
}
#endif

#endif /* ESP_ERR_H_STUB */
