#include <stdio.h>
#include <string.h>
#include <sys/param.h>
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
#include "ota_api.h"

static const char *TAG = "ota_api";

/**
 * @brief Performs streaming chunked OTA fallback when PSRAM is unavailable.
 */
static esp_err_t POST_OTA_update_streaming(httpd_req_t *req, const esp_partition_t *ota_partition, GlobalState *GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Starting streaming OTA update (PSRAM unavailable)");

    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, 20, "esp-miner.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Erasing flash...");

    vTaskDelay(100 / portTICK_PERIOD_MS);

    char buf[1000];
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;

    if (esp_ota_begin(ota_partition, req->content_len, &ota_handle) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
        HTTP_send_json_error(req, "500 Internal Server Error", "Failed to begin OTA erase");
        return ESP_FAIL;
    }

    int chunks = 0;
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        } else if (recv_len <= 0) {
            esp_ota_abort(ota_handle);
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Protocol Error");
            HTTP_send_json_error(req, "500 Internal Server Error", "Protocol Error");
            return ESP_FAIL;
        }

        if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
            esp_ota_abort(ota_handle);
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            HTTP_send_json_error(req, "500 Internal Server Error", "Write Error");
            return ESP_FAIL;
        }

        remaining -= recv_len;
        size_t written = req->content_len - remaining;
        uint8_t percentage = (uint8_t)((written * 100ULL) / req->content_len);
        GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = percentage;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Flashing (%d%%)", percentage);

        chunks++;
        if (chunks % 4 == 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Validation Error");
        HTTP_send_json_error(req, "500 Internal Server Error", "Validation / Activation Error");
        return ESP_OK;
    }

    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 100;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Rebooting...");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");
    ESP_LOGI(TAG, "Restarting System because of Firmware update complete");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
    esp_restart();

    return ESP_OK;
}

esp_err_t POST_OTA_update(httpd_req_t *req)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)httpd_get_global_user_ctx(req->handle);
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        HTTP_send_json_error(req, "500 Internal Server Error", "Not allowed in AP mode");
        return ESP_OK;
    }

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        HTTP_send_json_error(req, "500 Internal Server Error", "OTA partition not found");
        return ESP_OK;
    }

    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > ota_partition->size) {
        ESP_LOGE(TAG, "Requested payload size %d exceeds target partition size %d", (int)content_len, (int)ota_partition->size);
        HTTP_send_json_error(req, "400 Bad Request", "File provided is too large for device partition");
        return ESP_OK;
    }

    // Allocate PSRAM buffer if available
    uint8_t *ota_buf = NULL;
    if (esp_psram_is_initialized()) {
        ota_buf = (uint8_t *)heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);
    }

    if (ota_buf == NULL) {
        return POST_OTA_update_streaming(req, ota_partition, GLOBAL_STATE);
    }

    ESP_LOGI(TAG, "Starting RAM-First OTA update (allocated %d bytes in PSRAM)", (int)content_len);

    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, 20, "esp-miner.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Receiving (0%%)");

    // Phase 1: High-Speed Receive into PSRAM
    size_t cur_len = 0;
    int remaining = content_len;
    int chunks = 0;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, (char *)(ota_buf + cur_len), MIN(remaining, 4096));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        } else if (recv_len <= 0) {
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
            free(ota_buf);
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Protocol Error");
            HTTP_send_json_error(req, "500 Internal Server Error", "Protocol Error");
            return ESP_FAIL;
        }

        cur_len += recv_len;
        remaining -= recv_len;

        uint8_t percentage = (uint8_t)((cur_len * 100ULL) / content_len);
        GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = percentage;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Receiving (%d%%)", percentage);

        chunks++;
        if (chunks % 4 == 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    // Phase 2: Validate Image Magic & Header in PSRAM
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Validating...");
    vTaskDelay(20 / portTICK_PERIOD_MS);

    if (cur_len < sizeof(esp_image_header_t) || ota_buf[0] != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Validation failed: Invalid ESP image header magic (0x%02x)", ota_buf[0]);
        GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
        free(ota_buf);
        HTTP_send_json_error(req, "400 Bad Request", "Invalid firmware binary format");
        return ESP_OK;
    }

    esp_app_desc_t app_desc;
    size_t app_desc_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (cur_len > app_desc_offset + sizeof(esp_app_desc_t)) {
        memcpy(&app_desc, ota_buf + app_desc_offset, sizeof(esp_app_desc_t));
        if (app_desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
            ESP_LOGE(TAG, "Validation failed: Invalid app description magic (0x%08x)", (unsigned int)app_desc.magic_word);
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
            free(ota_buf);
            HTTP_send_json_error(req, "400 Bad Request", "Invalid firmware app description");
            return ESP_OK;
        }
        ESP_LOGI(TAG, "Validated firmware image: project '%s', version '%s'", app_desc.project_name, app_desc.version);
    }

    // Phase 3: Flash Erase & Write (PSRAM ➔ Flash)
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Flashing (0%%)");
    vTaskDelay(20 / portTICK_PERIOD_MS);

    esp_ota_handle_t ota_handle;
    if (esp_ota_begin(ota_partition, content_len, &ota_handle) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
        free(ota_buf);
        HTTP_send_json_error(req, "500 Internal Server Error", "Failed to begin OTA erase");
        return ESP_FAIL;
    }

    size_t write_offset = 0;
    while (write_offset < cur_len) {
        size_t chunk_size = MIN(65536, cur_len - write_offset);
        if (esp_ota_write(ota_handle, (const void *)(ota_buf + write_offset), chunk_size) != ESP_OK) {
            esp_ota_abort(ota_handle);
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
            free(ota_buf);
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            HTTP_send_json_error(req, "500 Internal Server Error", "Write Error");
            return ESP_FAIL;
        }

        write_offset += chunk_size;
        uint8_t flash_perc = (uint8_t)((write_offset * 100ULL) / cur_len);
        GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = flash_perc;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Flashing (%d%%)", flash_perc);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
        free(ota_buf);
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Activation Error");
        HTTP_send_json_error(req, "500 Internal Server Error", "Validation / Activation Error");
        return ESP_OK;
    }

    free(ota_buf);

    // Phase 4: Response & Reboot
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 100;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Rebooting...");

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
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        HTTP_send_json_error(req, "500 Internal Server Error", "Not allowed in AP mode");
        return ESP_OK;
    }

    const esp_partition_t *www_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (www_partition == NULL) {
        HTTP_send_json_error(req, "500 Internal Server Error", "WWW partition not found");
        return ESP_OK;
    }

    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > www_partition->size) {
        ESP_LOGE(TAG, "Requested WWW size %d exceeds partition size %d", (int)content_len, (int)www_partition->size);
        HTTP_send_json_error(req, "400 Bad Request", "File provided is too large for device partition");
        return ESP_OK;
    }

    // Allocate PSRAM buffer if available
    uint8_t *www_buf = NULL;
    if (esp_psram_is_initialized()) {
        www_buf = (uint8_t *)heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);
    }

    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, 20, "www.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Receiving (0%%)");

    if (www_buf != NULL) {
        // RAM-First Path
        size_t cur_len = 0;
        int remaining = content_len;
        int chunks = 0;

        while (remaining > 0) {
            int recv_len = httpd_req_recv(req, (char *)(www_buf + cur_len), MIN(remaining, 4096));

            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            } else if (recv_len <= 0) {
                GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
                free(www_buf);
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Protocol Error");
                HTTP_send_json_error(req, "500 Internal Server Error", "Protocol Error");
                return ESP_FAIL;
            }

            cur_len += recv_len;
            remaining -= recv_len;

            uint8_t percentage = (uint8_t)((cur_len * 100ULL) / content_len);
            GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = percentage;
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Receiving (%d%%)", percentage);

            chunks++;
            if (chunks % 4 == 0) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }

        // Erase & Write WWW partition
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Flashing...");
        size_t erase_size = 65536;
        for (size_t offset = 0; offset < www_partition->size; offset += erase_size) {
            size_t size_to_erase = MIN(erase_size, www_partition->size - offset);
            ESP_ERROR_CHECK(esp_partition_erase_range(www_partition, offset, size_to_erase));
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        if (esp_partition_write(www_partition, 0, (const void *)www_buf, cur_len) != ESP_OK) {
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
            free(www_buf);
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            HTTP_send_json_error(req, "500 Internal Server Error", "Write Error");
            return ESP_FAIL;
        }

        free(www_buf);
    } else {
        // Fallback streaming path
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Erasing flash...");
        vTaskDelay(100 / portTICK_PERIOD_MS);

        size_t erase_size = 65536;
        for (size_t offset = 0; offset < www_partition->size; offset += erase_size) {
            size_t size_to_erase = MIN(erase_size, www_partition->size - offset);
            ESP_ERROR_CHECK(esp_partition_erase_range(www_partition, offset, size_to_erase));
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        char buf[1000];
        int remaining = content_len;
        int chunks = 0;

        while (remaining > 0) {
            int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            } else if (recv_len <= 0) {
                GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Protocol Error");
                HTTP_send_json_error(req, "500 Internal Server Error", "Protocol Error");
                return ESP_FAIL;
            }

            if (esp_partition_write(www_partition, www_partition->size - remaining, (const void *)buf, recv_len) != ESP_OK) {
                GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
                HTTP_send_json_error(req, "500 Internal Server Error", "Write Error");
                return ESP_FAIL;
            }

            remaining -= recv_len;
            size_t written = content_len - remaining;
            uint8_t percentage = (uint8_t)((written * 100ULL) / content_len);
            GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = percentage;
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Flashing (%d%%)", percentage);

            chunks++;
            if (chunks % 4 == 0) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "WWW update complete, rebooting now!\n");
    nvs_config_set_bool(NVS_CONFIG_USE_CUSTOM_WWW, true);

    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 100;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Rebooting...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
    esp_restart();

    return ESP_OK;
}
