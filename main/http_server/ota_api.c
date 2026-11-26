#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_app_format.h"
#include "global_state.h"
#include "http_server.h"
#include "nvs_config.h"
#include "stratum_task.h"
#include "system.h"
#include "ota_api.h"

static const char *TAG = "ota_api";

#define OTA_RECV_MAX_TIMEOUTS 3
#define OTA_BLOCK_SIZE 65536

_Static_assert(OTA_BLOCK_SIZE >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t),
               "OTA_BLOCK_SIZE must be large enough to hold the ESP image header and app description");

typedef struct {
    uint8_t **blocks;
    size_t count;
    size_t total_len;
} ota_blocks_t;

static void ota_blocks_free(ota_blocks_t *b)
{
    if (!b || !b->blocks) return;
    for (size_t i = 0; i < b->count; i++) {
        free(b->blocks[i]);
    }
    free(b->blocks);
    b->blocks = NULL;
    b->count = 0;
    b->total_len = 0;
}

static bool ota_blocks_alloc(ota_blocks_t *b, size_t total_len)
{
    memset(b, 0, sizeof(*b));
    if (total_len == 0 || !esp_psram_is_initialized()) return false;

    size_t count = (total_len + OTA_BLOCK_SIZE - 1) / OTA_BLOCK_SIZE;
    b->blocks = (uint8_t **)heap_caps_calloc(count, sizeof(uint8_t *), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b->blocks) return false;
    b->count = count;
    b->total_len = total_len;

    for (size_t i = 0; i < count; i++) {
        size_t blk_size = MIN((size_t)OTA_BLOCK_SIZE, total_len - i * OTA_BLOCK_SIZE);
        b->blocks[i] = (uint8_t *)heap_caps_malloc(blk_size, MALLOC_CAP_SPIRAM);
        if (!b->blocks[i]) {
            ota_blocks_free(b);
            return false;
        }
    }
    return true;
}

static esp_err_t ota_send_error_and_close(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    HTTP_send_json_error(req, status, message);
    return ESP_FAIL;
}

static bool ota_pause_mining(GlobalState *GLOBAL_STATE)
{
    bool was_paused = GLOBAL_STATE->SYSTEM_MODULE.mining_paused;
    if (!was_paused) {
        ESP_LOGI(TAG, "Pausing mining and disconnecting stratum for OTA update");
        GLOBAL_STATE->SYSTEM_MODULE.mining_paused = true;
        stratum_abort_connection();
    }
    return was_paused;
}

static void ota_restore_after_failure(GlobalState *GLOBAL_STATE, bool was_mining_paused, const char *status)
{
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "%s", status ? status : "");
    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
    if (!was_mining_paused) {
        ESP_LOGI(TAG, "Resuming mining after aborted/failed OTA update");
        GLOBAL_STATE->SYSTEM_MODULE.mining_paused = false;
    }
}

static esp_err_t ota_recv_into_psram(httpd_req_t *req, ota_blocks_t *b,
                                     GlobalState *GLOBAL_STATE, bool was_mining_paused)
{
    size_t content_len = b->total_len;
    size_t cur_len = 0;
    size_t remaining = content_len;
    int timeouts = 0;

    while (remaining > 0) {
        size_t block_idx = cur_len / OTA_BLOCK_SIZE;
        size_t block_off = cur_len % OTA_BLOCK_SIZE;
        size_t max_read = MIN(remaining, (size_t)OTA_BLOCK_SIZE - block_off);
        int recv_len = httpd_req_recv(req, (char *)(b->blocks[block_idx] + block_off), max_read);

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts <= OTA_RECV_MAX_TIMEOUTS) {
                continue;
            }
            ota_blocks_free(b);
            ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Receive Timeout");
            return ota_send_error_and_close(req, "408 Request Timeout", "Receive Timeout");
        } else if (recv_len <= 0) {
            ESP_LOGW(TAG, "OTA upload aborted after %u/%u bytes (recv_len=%d)",
                     (unsigned int)cur_len, (unsigned int)content_len, recv_len);
            ota_blocks_free(b);
            ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Upload Aborted");
            return ESP_FAIL;
        }
        timeouts = 0;

        cur_len += (size_t)recv_len;
        remaining -= (size_t)recv_len;

        uint8_t percentage = (uint8_t)((cur_len * 100ULL) / content_len);
        GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = percentage;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Receiving (%d%%)", percentage);

        if (block_off + (size_t)recv_len == OTA_BLOCK_SIZE) {
            vTaskDelay(1);
        }
    }

    return ESP_OK;
}

esp_err_t POST_OTA_update(httpd_req_t *req)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)httpd_get_global_user_ctx(req->handle);
    if (!GLOBAL_STATE) return ESP_FAIL;

    if (is_network_allowed(req) != ESP_OK) {
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        return ota_send_error_and_close(req, "500 Internal Server Error", "Not allowed in AP mode");
    }

    if (GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update) {
        return ota_send_error_and_close(req, "409 Conflict", "Update already in progress");
    }

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM is not initialized; cannot perform OTA update");
        return ota_send_error_and_close(req, "500 Internal Server Error", "PSRAM required for OTA update");
    }

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        return ota_send_error_and_close(req, "500 Internal Server Error", "OTA partition not found");
    }

    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > ota_partition->size) {
        ESP_LOGE(TAG, "Requested payload size %d exceeds target partition size %d", (int)content_len, (int)ota_partition->size);
        return ota_send_error_and_close(req, "400 Bad Request", "File provided is too large for device partition");
    }

    bool was_mining_paused = ota_pause_mining(GLOBAL_STATE);

    ota_blocks_t ota_blocks;
    if (!ota_blocks_alloc(&ota_blocks, content_len)) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes across 64 KB PSRAM blocks for OTA update", (unsigned int)content_len);
        if (!was_mining_paused) {
            GLOBAL_STATE->SYSTEM_MODULE.mining_paused = false;
        }
        return ota_send_error_and_close(req, "500 Internal Server Error", "Insufficient PSRAM for OTA update");
    }

    ESP_LOGI(TAG, "Starting RAM-First OTA update (%u bytes in %u PSRAM blocks)",
             (unsigned int)content_len, (unsigned int)ota_blocks.count);

    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename), "esp-miner.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Receiving (0%%)");

    // Phase 1: High-Speed Receive into PSRAM
    if (ota_recv_into_psram(req, &ota_blocks, GLOBAL_STATE, was_mining_paused) != ESP_OK) {
        return ESP_FAIL;
    }

    // Phase 2: Validate Image Magic & Header in PSRAM (always fits within blocks[0])
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Validating...");

    const uint8_t *hdr_block = ota_blocks.blocks[0];
    if (content_len < sizeof(esp_image_header_t) || hdr_block[0] != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Validation failed: Invalid ESP image header magic (0x%02x)", hdr_block[0]);
        ota_blocks_free(&ota_blocks);
        ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Invalid Image");
        HTTP_send_json_error(req, "400 Bad Request", "Invalid firmware binary format");
        return ESP_OK;
    }

    esp_app_desc_t app_desc;
    size_t app_desc_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (content_len < app_desc_offset + sizeof(esp_app_desc_t)) {
        ESP_LOGE(TAG, "Validation failed: Firmware image too small (%u bytes)", (unsigned int)content_len);
        ota_blocks_free(&ota_blocks);
        ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Invalid Image");
        HTTP_send_json_error(req, "400 Bad Request", "Invalid firmware binary format");
        return ESP_OK;
    }
    memcpy(&app_desc, hdr_block + app_desc_offset, sizeof(esp_app_desc_t));
    if (app_desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "Validation failed: Invalid app description magic (0x%08x)", (unsigned int)app_desc.magic_word);
        ota_blocks_free(&ota_blocks);
        ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Invalid Image");
        HTTP_send_json_error(req, "400 Bad Request", "Invalid firmware app description");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Validated firmware image: project '%s', version '%s'", app_desc.project_name, app_desc.version);

    // Phase 3: Flash Erase & Write (PSRAM -> Flash)
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Flashing (0%%)");

    esp_ota_handle_t ota_handle;
    if (esp_ota_begin(ota_partition, content_len, &ota_handle) != ESP_OK) {
        ota_blocks_free(&ota_blocks);
        ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Erase Error");
        HTTP_send_json_error(req, "500 Internal Server Error", "Failed to begin OTA erase");
        return ESP_OK;
    }

    size_t write_offset = 0;
    for (size_t i = 0; i < ota_blocks.count; i++) {
        size_t chunk_size = MIN((size_t)OTA_BLOCK_SIZE, content_len - write_offset);
        if (esp_ota_write(ota_handle, (const void *)ota_blocks.blocks[i], chunk_size) != ESP_OK) {
            esp_ota_abort(ota_handle);
            ota_blocks_free(&ota_blocks);
            ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Write Error");
            HTTP_send_json_error(req, "500 Internal Server Error", "Write Error");
            return ESP_OK;
        }
        free(ota_blocks.blocks[i]);
        ota_blocks.blocks[i] = NULL;

        write_offset += chunk_size;
        uint8_t flash_perc = (uint8_t)((write_offset * 100ULL) / content_len);
        GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = flash_perc;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Flashing (%d%%)", flash_perc);
        vTaskDelay(1);
    }

    ota_blocks_free(&ota_blocks);

    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Activation Error");
        HTTP_send_json_error(req, "500 Internal Server Error", "Validation / Activation Error");
        return ESP_OK;
    }

    // Phase 4: Response & Reboot
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 100;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Rebooting...");

    // Reset custom WWW so the newly flashed firmware defaults to its matching embedded Web UI
    SYSTEM_reset_custom_www();

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");
    ESP_LOGI(TAG, "Restarting System because of Firmware update complete");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
    esp_restart();

    return ESP_OK;
}

esp_err_t POST_WWW_update(httpd_req_t *req)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)httpd_get_global_user_ctx(req->handle);
    if (!GLOBAL_STATE) return ESP_FAIL;

    if (is_network_allowed(req) != ESP_OK) {
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        return ota_send_error_and_close(req, "500 Internal Server Error", "Not allowed in AP mode");
    }

    if (GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update) {
        return ota_send_error_and_close(req, "409 Conflict", "Update already in progress");
    }

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM is not initialized; cannot perform WWW update");
        return ota_send_error_and_close(req, "500 Internal Server Error", "PSRAM required for WWW update");
    }

    const esp_partition_t *www_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (www_partition == NULL) {
        return ota_send_error_and_close(req, "500 Internal Server Error", "WWW partition not found");
    }

    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > www_partition->size) {
        ESP_LOGE(TAG, "Requested WWW size %d exceeds partition size %d", (int)content_len, (int)www_partition->size);
        return ota_send_error_and_close(req, "400 Bad Request", "File provided is too large for device partition");
    }

    bool was_mining_paused = ota_pause_mining(GLOBAL_STATE);

    ota_blocks_t www_blocks;
    if (!ota_blocks_alloc(&www_blocks, content_len)) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes across 64 KB PSRAM blocks for WWW update", (unsigned int)content_len);
        if (!was_mining_paused) {
            GLOBAL_STATE->SYSTEM_MODULE.mining_paused = false;
        }
        return ota_send_error_and_close(req, "500 Internal Server Error", "Insufficient PSRAM for WWW update");
    }

    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename), "www.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Receiving (0%%)");

    if (ota_recv_into_psram(req, &www_blocks, GLOBAL_STATE, was_mining_paused) != ESP_OK) {
        return ESP_FAIL;
    }

    // Erase & Write WWW partition
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Erasing flash...");
    size_t erase_size = OTA_BLOCK_SIZE;
    for (size_t offset = 0; offset < www_partition->size; offset += erase_size) {
        size_t size_to_erase = MIN(erase_size, www_partition->size - offset);
        if (esp_partition_erase_range(www_partition, offset, size_to_erase) != ESP_OK) {
            ota_blocks_free(&www_blocks);
            SYSTEM_reset_custom_www();
            ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Erase Error");
            HTTP_send_json_error(req, "500 Internal Server Error", "Failed to erase WWW partition");
            return ESP_OK;
        }
        vTaskDelay(1);
    }

    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Flashing (0%%)");
    size_t write_offset = 0;
    for (size_t i = 0; i < www_blocks.count; i++) {
        size_t chunk_size = MIN((size_t)OTA_BLOCK_SIZE, content_len - write_offset);
        if (esp_partition_write(www_partition, write_offset, (const void *)www_blocks.blocks[i], chunk_size) != ESP_OK) {
            ota_blocks_free(&www_blocks);
            SYSTEM_reset_custom_www();
            ota_restore_after_failure(GLOBAL_STATE, was_mining_paused, "Write Error");
            HTTP_send_json_error(req, "500 Internal Server Error", "Write Error");
            return ESP_OK;
        }
        free(www_blocks.blocks[i]);
        www_blocks.blocks[i] = NULL;

        write_offset += chunk_size;
        uint8_t flash_perc = (uint8_t)((write_offset * 100ULL) / content_len);
        GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = flash_perc;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
                 sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Flashing (%d%%)", flash_perc);
        vTaskDelay(1);
    }

    ota_blocks_free(&www_blocks);

    nvs_config_set_bool(NVS_CONFIG_USE_CUSTOM_WWW, true);

    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 100;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status,
             sizeof(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status), "Rebooting...");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "WWW update complete, rebooting now!\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
    esp_restart();

    return ESP_OK;
}
