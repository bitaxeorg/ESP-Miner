/**
 * @file mock_nvs.h
 * @brief Mock NVS (Non-Volatile Storage) for host-based testing
 *
 * Provides an in-memory key-value store that mimics ESP-IDF NVS behavior.
 * Test code can pre-populate values and verify writes.
 */

#ifndef MOCK_NVS_H
#define MOCK_NVS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NVS handle type */
typedef uint32_t nvs_handle_t;

/* NVS open modes */
typedef enum {
    NVS_READONLY,
    NVS_READWRITE
} nvs_open_mode_t;

/* Error codes (matching ESP-IDF) */
#define ESP_ERR_NVS_BASE                0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED     (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND           (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH       (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY           (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE    (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME        (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE      (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_REMOVE_FAILED       (ESP_ERR_NVS_BASE + 0x08)
#define ESP_ERR_NVS_KEY_TOO_LONG        (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_PAGE_FULL           (ESP_ERR_NVS_BASE + 0x0a)
#define ESP_ERR_NVS_INVALID_STATE       (ESP_ERR_NVS_BASE + 0x0b)
#define ESP_ERR_NVS_INVALID_LENGTH      (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES       (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_VALUE_TOO_LONG      (ESP_ERR_NVS_BASE + 0x0e)

/* ============================================
 * Mock Control Functions (for test setup)
 * ============================================ */

/**
 * @brief Initialize the mock NVS system
 * Call this at the start of each test to get a clean slate
 */
void mock_nvs_init(void);

/**
 * @brief Clear all stored values
 */
void mock_nvs_clear(void);

/**
 * @brief Pre-populate a uint16 value for testing
 */
void mock_nvs_set_u16(const char* namespace, const char* key, uint16_t value);

/**
 * @brief Pre-populate a string value for testing
 */
void mock_nvs_set_string(const char* namespace, const char* key, const char* value);

/**
 * @brief Check if a key was written during the test
 */
bool mock_nvs_was_written(const char* namespace, const char* key);

/**
 * @brief Get the number of writes to a specific key
 */
int mock_nvs_get_write_count(const char* namespace, const char* key);

/**
 * @brief Get total commit count (for verifying commit behavior)
 */
int mock_nvs_get_commit_count(void);

/* ============================================
 * NVS API Functions (matching ESP-IDF)
 * ============================================ */

int nvs_flash_init(void);
int nvs_flash_erase(void);

int nvs_open(const char* namespace, nvs_open_mode_t mode, nvs_handle_t* handle);
void nvs_close(nvs_handle_t handle);

int nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out_value);
int nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value);

int nvs_get_i8(nvs_handle_t handle, const char* key, int8_t* out_value);
int nvs_set_i8(nvs_handle_t handle, const char* key, int8_t value);

int nvs_get_u16(nvs_handle_t handle, const char* key, uint16_t* out_value);
int nvs_set_u16(nvs_handle_t handle, const char* key, uint16_t value);

int nvs_get_i16(nvs_handle_t handle, const char* key, int16_t* out_value);
int nvs_set_i16(nvs_handle_t handle, const char* key, int16_t value);

int nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out_value);
int nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value);

int nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out_value);
int nvs_set_i32(nvs_handle_t handle, const char* key, int32_t value);

int nvs_get_u64(nvs_handle_t handle, const char* key, uint64_t* out_value);
int nvs_set_u64(nvs_handle_t handle, const char* key, uint64_t value);

int nvs_get_str(nvs_handle_t handle, const char* key, char* out_value, size_t* length);
int nvs_set_str(nvs_handle_t handle, const char* key, const char* value);

int nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length);
int nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length);

int nvs_erase_key(nvs_handle_t handle, const char* key);
int nvs_erase_all(nvs_handle_t handle);

int nvs_commit(nvs_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_NVS_H */
