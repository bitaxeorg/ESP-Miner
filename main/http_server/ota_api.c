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
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "global_state.h"
#include "http_server.h"
#include "nvs_config.h"
#include "ota_api.h"

static const char *TAG = "ota_api";

static esp_err_t validate_ota_request(httpd_req_t *req, GlobalState **out_state)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)httpd_get_global_user_ctx(req->handle);
    if (!GLOBAL_STATE) return ESP_FAIL;
    if (out_state) *out_state = GLOBAL_STATE;

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        HTTP_send_json_error(req, "500 Internal Server Error", "Not allowed in AP mode");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static esp_err_t erase_partition_in_chunks(const esp_partition_t *partition)
{
    if (!partition) return ESP_ERR_INVALID_ARG;
    size_t erase_size = 65536;
    for (size_t offset = 0; offset < partition->size; offset += erase_size) {
        size_t size_to_erase = MIN(erase_size, partition->size - offset);
        esp_err_t err = esp_partition_erase_range(partition, offset, size_to_erase);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

static esp_err_t receive_http_payload_to_buffer(httpd_req_t *req, uint8_t *buf, size_t content_len, GlobalState *GLOBAL_STATE)
{
    size_t cur_len = 0;
    int remaining = content_len;
    int chunks = 0;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, (char *)(buf + cur_len), MIN(remaining, 4096));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        } else if (recv_len <= 0) {
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
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
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    return ESP_OK;
}

static void trigger_reboot_and_cleanup(GlobalState *GLOBAL_STATE, const char *msg)
{
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 100;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "%s", msg ? msg : "Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
    esp_restart();
}

static esp_err_t validate_firmware_image(const uint8_t *ota_buf, size_t content_len)
{
    if (content_len < sizeof(esp_image_header_t) || ota_buf[0] != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Validation failed: Invalid ESP image header magic (0x%02x)", ota_buf[0]);
        return ESP_ERR_INVALID_ARG;
    }

    esp_app_desc_t app_desc;
    size_t app_desc_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (content_len > app_desc_offset + sizeof(esp_app_desc_t)) {
        memcpy(&app_desc, ota_buf + app_desc_offset, sizeof(esp_app_desc_t));
        if (app_desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
            ESP_LOGE(TAG, "Validation failed: Invalid app description magic (0x%08x)", (unsigned int)app_desc.magic_word);
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Validated firmware image: project '%s', version '%s'", app_desc.project_name, app_desc.version);
    }
    return ESP_OK;
}

static esp_err_t download_https_payload_to_buffer(esp_http_client_handle_t client, uint8_t *buf, size_t content_len, GlobalState *global_state, const char *status_prefix)
{
    int read_bytes = 0;
    size_t total_bytes_read = 0;

    while (total_bytes_read < content_len && (read_bytes = esp_http_client_read(client, (char *)(buf + total_bytes_read), MIN(4096, content_len - total_bytes_read))) > 0) {
        total_bytes_read += read_bytes;
        if (content_len > 0 && global_state) {
            uint8_t percentage = (uint8_t)((total_bytes_read * 100ULL) / content_len);
            global_state->SYSTEM_MODULE.firmware_update_percent = percentage;
            snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "%s (%d%%)", status_prefix ? status_prefix : "Downloading", percentage);
        }
    }

    if (content_len > 0 && total_bytes_read < content_len) {
        ESP_LOGE(TAG, "HTTPS download incomplete: fetched %d / %d bytes", (int)total_bytes_read, (int)content_len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t flash_buffer_to_ota_partition(const uint8_t *buf, size_t content_len, const esp_partition_t *ota_partition, GlobalState *global_state)
{
    if (!ota_partition || !buf || content_len == 0) return ESP_ERR_INVALID_ARG;

    esp_ota_handle_t ota_handle;
    if (esp_ota_begin(ota_partition, content_len, &ota_handle) != ESP_OK) {
        if (global_state) {
            snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "Erase Error");
        }
        return ESP_FAIL;
    }

    size_t write_offset = 0;
    while (write_offset < content_len) {
        size_t chunk_size = MIN(65536, content_len - write_offset);
        if (esp_ota_write(ota_handle, (const void *)(buf + write_offset), chunk_size) != ESP_OK) {
            esp_ota_abort(ota_handle);
            if (global_state) {
                snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            }
            return ESP_FAIL;
        }

        write_offset += chunk_size;
        if (global_state) {
            uint8_t flash_perc = (uint8_t)((write_offset * 100ULL) / content_len);
            global_state->SYSTEM_MODULE.firmware_update_percent = flash_perc;
            snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "Flashing (%d%%)", flash_perc);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        if (global_state) {
            snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "Activation Error");
        }
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t flash_buffer_to_www_partition(const uint8_t *buf, size_t content_len, const esp_partition_t *www_partition, GlobalState *global_state)
{
    if (!www_partition || !buf || content_len == 0) return ESP_ERR_INVALID_ARG;

    if (global_state) {
        global_state->SYSTEM_MODULE.firmware_update_percent = 0;
        snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "Flashing WWW (0%%)");
    }

    if (erase_partition_in_chunks(www_partition) != ESP_OK) {
        if (global_state) {
            snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "Erase Error");
        }
        return ESP_FAIL;
    }

    size_t write_offset = 0;
    size_t chunk_size = 65536;

    while (write_offset < content_len) {
        size_t size_to_write = MIN(chunk_size, content_len - write_offset);
        if (esp_partition_write(www_partition, write_offset, (const void *)(buf + write_offset), size_to_write) != ESP_OK) {
            if (global_state) {
                snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            }
            return ESP_FAIL;
        }

        write_offset += size_to_write;
        if (global_state) {
            uint8_t flash_perc = (uint8_t)((write_offset * 100ULL) / content_len);
            global_state->SYSTEM_MODULE.firmware_update_percent = flash_perc;
            snprintf(global_state->SYSTEM_MODULE.firmware_update_status, 20, "Flashing WWW (%d%%)", flash_perc);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

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

    vTaskDelay(pdMS_TO_TICKS(100));

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
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Validation Error");
        HTTP_send_json_error(req, "500 Internal Server Error", "Validation / Activation Error");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");
    ESP_LOGI(TAG, "Restarting System because of Firmware update complete");
    nvs_config_set_bool(NVS_CONFIG_USE_CUSTOM_WWW, false);
    trigger_reboot_and_cleanup(GLOBAL_STATE, "Rebooting...");
    return ESP_OK;
}

esp_err_t POST_OTA_update(httpd_req_t *req)
{
    GlobalState *GLOBAL_STATE = NULL;
    esp_err_t err = validate_ota_request(req, &GLOBAL_STATE);
    if (err != ESP_OK) return (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;

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
    if (receive_http_payload_to_buffer(req, ota_buf, content_len, GLOBAL_STATE) != ESP_OK) {
        free(ota_buf);
        return ESP_FAIL;
    }

    // Phase 2: Validate Image Magic & Header in PSRAM
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Validating...");
    vTaskDelay(pdMS_TO_TICKS(20));

    if (validate_firmware_image(ota_buf, content_len) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
        free(ota_buf);
        HTTP_send_json_error(req, "400 Bad Request", "Invalid firmware binary format");
        return ESP_OK;
    }

    // Phase 3: Flash Erase & Write (PSRAM ➔ Flash)
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Flashing (0%%)");
    vTaskDelay(pdMS_TO_TICKS(20));

    if (flash_buffer_to_ota_partition(ota_buf, content_len, ota_partition, GLOBAL_STATE) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
        free(ota_buf);
        HTTP_send_json_error(req, "500 Internal Server Error", "Write Error");
        return ESP_FAIL;
    }

    free(ota_buf);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");
    ESP_LOGI(TAG, "Restarting System because of Firmware update complete");
    nvs_config_set_bool(NVS_CONFIG_USE_CUSTOM_WWW, false);
    trigger_reboot_and_cleanup(GLOBAL_STATE, "Rebooting...");
    return ESP_OK;
}

esp_err_t POST_WWW_update(httpd_req_t *req)
{
    GlobalState *GLOBAL_STATE = NULL;
    esp_err_t err = validate_ota_request(req, &GLOBAL_STATE);
    if (err != ESP_OK) return (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;

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

    uint8_t *www_buf = NULL;
    if (esp_psram_is_initialized()) {
        www_buf = (uint8_t *)heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);
    }

    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, 20, "www.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Receiving (0%%)");

    if (www_buf != NULL) {
        // RAM-First Path: Receive into PSRAM
        if (receive_http_payload_to_buffer(req, www_buf, content_len, GLOBAL_STATE) != ESP_OK) {
            free(www_buf);
            return ESP_FAIL;
        }

        // Flash to WWW Partition
        if (flash_buffer_to_www_partition(www_buf, content_len, www_partition, GLOBAL_STATE) != ESP_OK) {
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
            free(www_buf);
            HTTP_send_json_error(req, "500 Internal Server Error", "Write Error");
            return ESP_FAIL;
        }
        free(www_buf);
    } else {
        // Fallback streaming path
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Erasing flash...");
        vTaskDelay(pdMS_TO_TICKS(100));

        if (erase_partition_in_chunks(www_partition) != ESP_OK) {
            GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;
            HTTP_send_json_error(req, "500 Internal Server Error", "Erase Error");
            return ESP_FAIL;
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
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "WWW update complete, rebooting now!\n");
    trigger_reboot_and_cleanup(GLOBAL_STATE, "Rebooting...");
    return ESP_OK;
}

typedef struct {
    char url[256];
    char tag[64];
    GlobalState *global_state;
} github_ota_args_t;

static bool is_valid_github_release_url(const char *url)
{
    if (!url) return false;
    const char *official_prefix = "https://github.com/bitaxeorg/ESP-Miner/releases/download/";
    const char *githubusercontent_prefix = "https://objects.githubusercontent.com/";

    if (strncmp(url, official_prefix, strlen(official_prefix)) == 0) {
        return true;
    }
    if (strncmp(url, githubusercontent_prefix, strlen(githubusercontent_prefix)) == 0) {
        return true;
    }
    return false;
}

static void construct_www_url(const char *fw_url, const char *tag, char *www_url, size_t max_len)
{
    if (tag && tag[0] != '\0') {
        snprintf(www_url, max_len, "https://github.com/bitaxeorg/ESP-Miner/releases/download/%s/www.bin", tag);
        return;
    }
    const char *pos = strstr(fw_url, "esp-miner.bin");
    if (pos) {
        size_t len_before = pos - fw_url;
        snprintf(www_url, max_len, "%.*swww.bin%s", (int)len_before, fw_url, pos + strlen("esp-miner.bin"));
    } else {
        snprintf(www_url, max_len, "%s", fw_url);
    }
}

static void cleanup_github_ota_task(github_ota_args_t *args, esp_http_client_handle_t client, const char *status)
{
    if (args && args->global_state) {
        if (status) {
            snprintf(args->global_state->SYSTEM_MODULE.firmware_update_status, 20, "%s", status);
        }
        vTaskDelay(pdMS_TO_TICKS(4000));
        args->global_state->SYSTEM_MODULE.is_firmware_update = false;
    }
    if (client) esp_http_client_cleanup(client);
    if (args) free(args);
    vTaskDelete(NULL);
}

static esp_err_t open_https_following_redirects(esp_http_client_handle_t client, int *out_status_code, int *out_content_len)
{
    int redirect_count = 0;
    const int max_redirects = 5;

    while (redirect_count < max_redirects) {
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTPS connection: %s", esp_err_to_name(err));
            return err;
        }

        int content_len = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);

        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
            ESP_LOGI(TAG, "HTTP %d redirect received, following redirection...", status_code);
            err = esp_http_client_set_redirection(client);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set HTTP redirection: %s", esp_err_to_name(err));
                return err;
            }
            redirect_count++;
            continue;
        }

        if (out_status_code) *out_status_code = status_code;
        if (out_content_len) *out_content_len = content_len;
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Exceeded maximum HTTP redirect count (%d)", max_redirects);
    return ESP_ERR_HTTP_MAX_REDIRECT;
}

static void github_ota_task(void *pvParameters)
{
    github_ota_args_t *args = (github_ota_args_t *)pvParameters;
    GlobalState *GLOBAL_STATE = args->global_state;

    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, 20, "esp-miner.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Downloading...");

    esp_http_client_config_t config = {
        .url = args->url,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = true,
        .max_redirection_count = 5,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        cleanup_github_ota_task(args, NULL, "Client Init Error");
        return;
    }

    int content_length = 0;
    int status_code = 0;
    esp_err_t err = open_https_following_redirects(client, &status_code, &content_length);
    if (err != ESP_OK) {
        cleanup_github_ota_task(args, client, "Connection Error");
        return;
    }

    if (status_code < 200 || status_code >= 300) {
        ESP_LOGE(TAG, "GitHub returned HTTP status %d", status_code);
        char status_buf[20];
        snprintf(status_buf, sizeof(status_buf), "HTTP Error %d", status_code);
        cleanup_github_ota_task(args, client, status_buf);
        return;
    }

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        cleanup_github_ota_task(args, client, "Partition Error");
        return;
    }

    if (content_length > 0 && content_length > ota_partition->size) {
        cleanup_github_ota_task(args, client, "Image Too Large");
        return;
    }

    // Attempt to allocate PSRAM buffer
    uint8_t *ota_buf = NULL;
    if (esp_psram_is_initialized() && content_length > 0) {
        ota_buf = (uint8_t *)heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM);
    }

    if (ota_buf != NULL) {
        // --- RAM-FIRST PATH ---
        ESP_LOGI(TAG, "Starting RAM-First GitHub OTA (%d bytes in PSRAM)", content_length);

        if (download_https_payload_to_buffer(client, ota_buf, content_length, GLOBAL_STATE, "Downloading") != ESP_OK) {
            free(ota_buf);
            cleanup_github_ota_task(args, client, "Download Failed");
            return;
        }

        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Validating...");
        vTaskDelay(pdMS_TO_TICKS(20));

        if (validate_firmware_image(ota_buf, content_length) != ESP_OK) {
            free(ota_buf);
            cleanup_github_ota_task(args, client, "Validation Error");
            return;
        }

        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Flashing (0%%)");
        if (flash_buffer_to_ota_partition(ota_buf, content_length, ota_partition, GLOBAL_STATE) != ESP_OK) {
            free(ota_buf);
            cleanup_github_ota_task(args, client, "Flash Error");
            return;
        }

        free(ota_buf);
    } else {
        // --- STREAMING FALLBACK PATH (PSRAM unavailable or malloc failed) ---
        ESP_LOGI(TAG, "Starting Streaming GitHub OTA (PSRAM unavailable)");

        esp_ota_handle_t ota_handle;
        if (esp_ota_begin(ota_partition, content_length > 0 ? content_length : OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
            cleanup_github_ota_task(args, client, "Erase Error");
            return;
        }

        char *buf = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!buf) {
            esp_ota_abort(ota_handle);
            cleanup_github_ota_task(args, client, "Memory Error");
            return;
        }

        int read_bytes = 0;
        int total_bytes_read = 0;
        bool download_failed = false;

        while ((read_bytes = esp_http_client_read(client, buf, 4096)) > 0) {
            if (esp_ota_write(ota_handle, buf, read_bytes) != ESP_OK) {
                download_failed = true;
                break;
            }
            total_bytes_read += read_bytes;
            if (content_length > 0) {
                uint8_t percentage = (uint8_t)((total_bytes_read * 100ULL) / content_length);
                GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = percentage;
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Flashing (%d%%)", percentage);
            }
        }

        free(buf);

        if (download_failed || (content_length > 0 && total_bytes_read < content_length)) {
            esp_ota_abort(ota_handle);
            cleanup_github_ota_task(args, client, "Download Failed");
            return;
        }

        if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
            cleanup_github_ota_task(args, client, "Validation Error");
            return;
        }
    }

    esp_http_client_cleanup(client);
    client = NULL;

    // Stage 2: Check for www.bin on GitHub for older split releases
    char www_url[256] = {0};
    construct_www_url(args->url, args->tag, www_url, sizeof(www_url));

    ESP_LOGI(TAG, "Checking for www.bin at %s", www_url);
    esp_http_client_config_t www_config = {
        .url = www_url,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = true,
        .max_redirection_count = 5,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t www_client = esp_http_client_init(&www_config);
    if (www_client) {
        int www_len = 0;
        int www_status = 0;
        if (open_https_following_redirects(www_client, &www_status, &www_len) == ESP_OK) {
            if (www_status == 200 && www_len > 0) {
                const esp_partition_t *www_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
                if (www_partition && www_len <= www_partition->size) {
                    ESP_LOGI(TAG, "Found www.bin on GitHub release (%d bytes).", www_len);

                    uint8_t *psram_www = NULL;
                    if (esp_psram_is_initialized()) {
                        psram_www = (uint8_t *)heap_caps_malloc(www_len, MALLOC_CAP_SPIRAM);
                    }

                    if (psram_www != NULL) {
                        ESP_LOGI(TAG, "Downloading www.bin into PSRAM first...");
                        if (download_https_payload_to_buffer(www_client, psram_www, www_len, GLOBAL_STATE, "WWW") == ESP_OK) {
                            if (flash_buffer_to_www_partition(psram_www, www_len, www_partition, GLOBAL_STATE) == ESP_OK) {
                                ESP_LOGI(TAG, "WWW partition updated from PSRAM.");
                                nvs_config_set_bool(NVS_CONFIG_USE_CUSTOM_WWW, false);
                            }
                        }
                        free(psram_www);
                    } else {
                        // Direct streaming fallback if PSRAM unavailable
                        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Erasing WWW...");
                        GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = 0;

                        if (erase_partition_in_chunks(www_partition) == ESP_OK) {
                            char *www_buf = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                            if (www_buf) {
                                int r_bytes = 0;
                                int total_r = 0;
                                bool www_write_err = false;

                                while ((r_bytes = esp_http_client_read(www_client, www_buf, 4096)) > 0) {
                                    if (esp_partition_write(www_partition, total_r, www_buf, r_bytes) != ESP_OK) {
                                        www_write_err = true;
                                        break;
                                    }
                                    total_r += r_bytes;
                                    uint8_t percentage = (uint8_t)((total_r * 100ULL) / www_len);
                                    GLOBAL_STATE->SYSTEM_MODULE.firmware_update_percent = percentage;
                                    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "WWW (%d%%)", percentage);
                                }
                                free(www_buf);

                                if (!www_write_err && total_r >= www_len) {
                                    ESP_LOGI(TAG, "WWW partition updated successfully.");
                                    nvs_config_set_bool(NVS_CONFIG_USE_CUSTOM_WWW, false);
                                }
                            }
                        }
                    }
                }
            } else {
                ESP_LOGI(TAG, "No www.bin found (HTTP %d). Unified firmware, using embedded WWW.", www_status);
                nvs_config_set_bool(NVS_CONFIG_USE_CUSTOM_WWW, false);
            }
        } else {
            ESP_LOGI(TAG, "Could not connect to www.bin. Using embedded WWW.");
            nvs_config_set_bool(NVS_CONFIG_USE_CUSTOM_WWW, false);
        }
        esp_http_client_cleanup(www_client);
    }

    trigger_reboot_and_cleanup(GLOBAL_STATE, "Rebooting...");
    free(args);
}

esp_err_t POST_OTA_github_update(httpd_req_t *req)
{
    GlobalState *GLOBAL_STATE = NULL;
    esp_err_t err = validate_ota_request(req, &GLOBAL_STATE);
    if (err != ESP_OK) return (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int total_len = req->content_len;
    if (total_len <= 0 || total_len > MAX_REST_PAYLOAD_SIZE) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content length invalid or too large");
    }

    char *buf = malloc(total_len + 1);
    if (!buf) {
        return httpd_resp_send_500(req);
    }

    int ret = httpd_req_recv(req, buf, total_len);
    if (ret <= 0) {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request");
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    char download_url[256] = {0};
    cJSON *url_item = cJSON_GetObjectItem(root, "url");
    cJSON *tag_item = cJSON_GetObjectItem(root, "tag");

    if (cJSON_IsString(url_item) && url_item->valuestring) {
        snprintf(download_url, sizeof(download_url), "%s", url_item->valuestring);
    } else if (cJSON_IsString(tag_item) && tag_item->valuestring) {
        snprintf(download_url, sizeof(download_url), "https://github.com/bitaxeorg/ESP-Miner/releases/download/%s/esp-miner.bin", tag_item->valuestring);
    }

    if (download_url[0] == '\0' || !is_valid_github_release_url(download_url)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid or disallowed GitHub release URL");
    }

    github_ota_args_t *args = calloc(1, sizeof(github_ota_args_t));
    if (!args) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }
    snprintf(args->url, sizeof(args->url), "%s", download_url);
    if (cJSON_IsString(tag_item) && tag_item->valuestring) {
        snprintf(args->tag, sizeof(args->tag), "%s", tag_item->valuestring);
    }
    args->global_state = GLOBAL_STATE;
    cJSON_Delete(root);

    if (xTaskCreate(github_ota_task, "github_ota", 8192, args, 5, NULL) != pdPASS) {
        free(args);
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"Downloading\"}");
    return ESP_OK;
}
