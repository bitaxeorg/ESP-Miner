
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

//#include "protocol_examples_common.h"
#include "miner.h"


#include "stratum_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "global_state.h"
#include "serial.h"
#include "asic_result_task.h"
#include "nvs_config.h"

static GlobalState GLOBAL_STATE = {
    .extranonce_str = NULL,
    .extranonce_2_len = 0,
    .abandon_work = 0,
    .version_mask = 0,
    .POWER_MANAGEMENT_MODULE = {
        .frequency_multiplier = 1,
        .frequency_value = BM1397_FREQUENCY
    }
};

static const char *TAG = "miner";

void app_main(void)
{
    ESP_LOGI(TAG, "Welcome to the bitaxe!");
    //wait between 0 and 5 seconds for multiple units
    vTaskDelay(rand() % 5001 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(nvs_flash_init());

    xTaskCreate(SYSTEM_task, "SYSTEM_task", 4096, (void*)&GLOBAL_STATE, 3, NULL);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    //pull the wifi credentials out of NVS
    char * wifi_ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, WIFI_SSID);
    char * wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS, WIFI_PASS);

    //copy the wifi ssid to the global state
    strncpy(GLOBAL_STATE.SYSTEM_MODULE.ssid, wifi_ssid, 20);

    //init and connect to wifi
    EventBits_t result_bits = wifi_init_sta(wifi_ssid, wifi_pass);

    if (result_bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", wifi_ssid);
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "Connected!", 20);
    } else if (result_bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", wifi_ssid);
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "Failed to connect", 20);
        esp_restart(); //this is pretty much fatal, so just restart
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "unexpected error", 20);
        esp_restart(); //this is pretty much fatal, so just restart
    }

    free(wifi_ssid);
    free(wifi_pass);

    queue_init(&GLOBAL_STATE.stratum_queue);
    queue_init(&GLOBAL_STATE.ASIC_jobs_queue);

    SERIAL_init();

    BM1397_init(GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value);

    //set the startup_done flag
    GLOBAL_STATE.SYSTEM_MODULE.startup_done = true;

    xTaskCreate(stratum_task, "stratum admin", 8192, (void*)&GLOBAL_STATE, 5, NULL);
    xTaskCreate(create_jobs_task, "stratum miner", 8192, (void*)&GLOBAL_STATE, 10, NULL);
    xTaskCreate(POWER_MANAGEMENT_task, "power mangement", 8192, (void*)&GLOBAL_STATE, 10, NULL);
    xTaskCreate(ASIC_task, "asic", 8192, (void*)&GLOBAL_STATE, 10, NULL);
    xTaskCreate(ASIC_result_task, "asic result", 8192, (void*)&GLOBAL_STATE, 15, NULL);

}

void MINER_set_wifi_status(wifi_status_t status, uint16_t retry_count) {
    if (status == WIFI_RETRYING) {
        snprintf(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, 20, "Retrying: %d/%d", retry_count, WIFI_MAXIMUM_RETRY);
        return;
    } else if (status == WIFI_CONNECT_FAILED) {
        snprintf(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, 20, "Connect Failed!");
        return;
    }
}

