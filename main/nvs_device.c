#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "nvs_config.h"
#include "nvs_device.h"

#include "connect.h"
#include "global_state.h"
#include "asic.h"

static const char * TAG = "nvs_device";
<<<<<<< HEAD
=======
static const double NONCE_SPACE = 4294967296.0; //  2^32
static const double VERSION_SPACE = 65536.0; //  2^16

>>>>>>> 4274e4e (update equations)

esp_err_t NVSDevice_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t NVSDevice_get_wifi_creds(GlobalState * GLOBAL_STATE, char ** wifi_ssid, char ** wifi_pass, char ** hostname) {
    // pull the wifi credentials and hostname out of NVS
    *wifi_ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, WIFI_SSID);
    *wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS, WIFI_PASS);
    *hostname  = nvs_config_get_string(NVS_CONFIG_HOSTNAME, HOSTNAME);

    // copy the wifi ssid to the global state
    strncpy(GLOBAL_STATE->SYSTEM_MODULE.ssid, *wifi_ssid, sizeof(GLOBAL_STATE->SYSTEM_MODULE.ssid));
    GLOBAL_STATE->SYSTEM_MODULE.ssid[sizeof(GLOBAL_STATE->SYSTEM_MODULE.ssid)-1] = 0;

    return ESP_OK;
}

esp_err_t NVSDevice_parse_config(GlobalState * GLOBAL_STATE) {

    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);
    ESP_LOGI(TAG, "NVS_CONFIG_ASIC_FREQ %f", (float)GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value);

    GLOBAL_STATE->asic_model_str = nvs_config_get_string(NVS_CONFIG_ASIC_MODEL, "");
    GLOBAL_STATE->device_model_str = nvs_config_get_string(NVS_CONFIG_DEVICE_MODEL, "invalid");
    char * board_version = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION, "000");
    GLOBAL_STATE->board_version = atoi(board_version);
    free(board_version);
    ESP_LOGI(TAG, "Found Device Model: %s", GLOBAL_STATE->device_model_str);
    ESP_LOGI(TAG, "Found Board Version: %d", GLOBAL_STATE->board_version);

<<<<<<< HEAD
    ESP_RETURN_ON_ERROR(ASIC_set_device_model(GLOBAL_STATE), TAG, "Failed to get device model");
=======
    GLOBAL_STATE->asic_model_str = nvs_config_get_string(NVS_CONFIG_ASIC_MODEL, "");
    if (strcmp(GLOBAL_STATE->asic_model_str, "BM1366") == 0) {
        ESP_LOGI(TAG, "ASIC: %dx BM1366 (%" PRIu64 " cores)", GLOBAL_STATE->asic_count, BM1366_CORE_COUNT);
        GLOBAL_STATE->asic_model = ASIC_BM1366;
        AsicFunctions ASIC_functions = {.init_fn = BM1366_init,
                                        .receive_result_fn = BM1366_proccess_work,
                                        .set_max_baud_fn = BM1366_set_max_baud,
                                        .set_difficulty_mask_fn = BM1366_set_job_difficulty_mask,
                                        .send_work_fn = BM1366_send_work,
                                        .set_version_mask = BM1366_set_version_mask};
        GLOBAL_STATE->asic_job_frequency_ms =  (
            (double) (BM1366_FULLSCAN_PERCENT * BM1366_HCN_PERCENT * BM1366_NONCE_PERCENT * BM1366_VERSION_PERCENT * NONCE_SPACE * VERSION_SPACE) / 
            (double) (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * BM1366_SMALL_CORE_COUNT * 1000)) / (double) GLOBAL_STATE->asic_count;
        GLOBAL_STATE->ASIC_difficulty = BM1366_ASIC_DIFFICULTY;

        GLOBAL_STATE->ASIC_functions = ASIC_functions;
        } else if (strcmp(GLOBAL_STATE->asic_model_str, "BM1370") == 0) {
        ESP_LOGI(TAG, "ASIC: %dx BM1370 (%" PRIu64 " cores)", GLOBAL_STATE->asic_count, BM1370_CORE_COUNT);
        GLOBAL_STATE->asic_model = ASIC_BM1370;
        AsicFunctions ASIC_functions = {.init_fn = BM1370_init,
                                        .receive_result_fn = BM1370_proccess_work,
                                        .set_max_baud_fn = BM1370_set_max_baud,
                                        .set_difficulty_mask_fn = BM1370_set_job_difficulty_mask,
                                        .send_work_fn = BM1370_send_work,
                                        .set_version_mask = BM1370_set_version_mask};
        GLOBAL_STATE->asic_job_frequency_ms =  (
            (double) (BM1370_FULLSCAN_PERCENT * BM1370_HCN_PERCENT * BM1370_NONCE_PERCENT * BM1370_VERSION_PERCENT * NONCE_SPACE * VERSION_SPACE) / 
            (double) (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * BM1370_SMALL_CORE_COUNT * 1000)) / (double) GLOBAL_STATE->asic_count;
        GLOBAL_STATE->ASIC_difficulty = BM1370_ASIC_DIFFICULTY;

        GLOBAL_STATE->ASIC_functions = ASIC_functions;
    } else if (strcmp(GLOBAL_STATE->asic_model_str, "BM1368") == 0) {
        ESP_LOGI(TAG, "ASIC: %dx BM1368 (%" PRIu64 " cores)", GLOBAL_STATE->asic_count, BM1368_CORE_COUNT);
        GLOBAL_STATE->asic_model = ASIC_BM1368;
        AsicFunctions ASIC_functions = {.init_fn = BM1368_init,
                                        .receive_result_fn = BM1368_proccess_work,
                                        .set_max_baud_fn = BM1368_set_max_baud,
                                        .set_difficulty_mask_fn = BM1368_set_job_difficulty_mask,
                                        .send_work_fn = BM1368_send_work,
                                        .set_version_mask = BM1368_set_version_mask};

        GLOBAL_STATE->asic_job_frequency_ms =  (
            (double) (BM1368_FULLSCAN_PERCENT * BM1368_HCN_PERCENT * BM1368_NONCE_PERCENT * BM1368_VERSION_PERCENT * NONCE_SPACE * VERSION_SPACE) / 
            (double) (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * BM1368_SMALL_CORE_COUNT * 1000)) / (double) GLOBAL_STATE->asic_count;
        GLOBAL_STATE->ASIC_difficulty = BM1368_ASIC_DIFFICULTY;

        GLOBAL_STATE->ASIC_functions = ASIC_functions;
    } else if (strcmp(GLOBAL_STATE->asic_model_str, "BM1397") == 0) {
        ESP_LOGI(TAG, "ASIC: %dx BM1397 (%" PRIu64 " cores)", GLOBAL_STATE->asic_count, BM1397_SMALL_CORE_COUNT);
        GLOBAL_STATE->asic_model = ASIC_BM1397;
        AsicFunctions ASIC_functions = {.init_fn = BM1397_init,
                                        .receive_result_fn = BM1397_proccess_work,
                                        .set_max_baud_fn = BM1397_set_max_baud,
                                        .set_difficulty_mask_fn = BM1397_set_job_difficulty_mask,
                                        .send_work_fn = BM1397_send_work,
                                        .set_version_mask = BM1397_set_version_mask};
        // the bm1397 is a different equation as no version rolling and operates on 4 different spaces in paralell  
        GLOBAL_STATE->asic_job_frequency_ms = (
            (double) (BM1397_FULLSCAN_PERCENT * BM1397_MIDSTATE_ENGINES * BM1397_NONCE_PERCENT * NONCE_SPACE) / 
            (double) (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * BM1397_SMALL_CORE_COUNT * 1000)) / (double) GLOBAL_STATE->asic_count; 
        GLOBAL_STATE->ASIC_difficulty = BM1397_ASIC_DIFFICULTY;

        GLOBAL_STATE->ASIC_functions = ASIC_functions;
    } else {
        ESP_LOGE(TAG, "Invalid ASIC model");
        AsicFunctions ASIC_functions = {.init_fn = NULL,
                                        .receive_result_fn = NULL,
                                        .set_max_baud_fn = NULL,
                                        .set_difficulty_mask_fn = NULL,
                                        .send_work_fn = NULL};
        GLOBAL_STATE->ASIC_functions = ASIC_functions;
        // maybe should return here to not execute anything with a faulty device parameter !
        return ESP_FAIL;
    }
>>>>>>> 4274e4e (update equations)

    return ESP_OK;
}