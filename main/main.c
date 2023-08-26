
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

//#include "protocol_examples_common.h"
#include "main.h"


#include "stratum_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "global_state.h"
#include "serial.h"
#include "asic_result_task.h"
#include "nvs_config.h"
#include "http_server.h"
#include "esp_netif.h"
#include "user_input_task.h"



#define ASIC_MODEL CONFIG_ASIC_MODEL


static GlobalState GLOBAL_STATE = {
    .extranonce_str = NULL,
    .extranonce_2_len = 0,
    .abandon_work = 0,
    .version_mask = 0,
    .POWER_MANAGEMENT_MODULE = {
        .frequency_multiplier = 1,
        .frequency_value = ASIC_FREQUENCY
    }
};

static const char *TAG = "miner";

void app_main(void)
{

    if(strcmp(ASIC_MODEL, "BM1366") == 0){
        ESP_LOGI(TAG, "ASIC: BM1366");
        AsicFunctions ASIC_functions =  {
            .init_fn = BM1366_init,
            .receive_result_fn = BM1366_proccess_work,
            .set_max_baud_fn = BM1366_set_max_baud,
            .set_difficulty_mask_fn = BM1366_set_job_difficulty_mask,
            .send_work_fn = BM1366_send_work
        };
        GLOBAL_STATE.asic_job_frequency_ms = BM1366_FULLSCAN_MS;

        GLOBAL_STATE.ASIC_functions = ASIC_functions;
    }else if(strcmp(ASIC_MODEL, "BM1397") == 0){
        ESP_LOGI(TAG, "ASIC: BM1397");
        AsicFunctions ASIC_functions =  {
            .init_fn = BM1397_init,
            .receive_result_fn = BM1397_proccess_work,
            .set_max_baud_fn = BM1397_set_max_baud,
            .set_difficulty_mask_fn = BM1397_set_job_difficulty_mask,
            .send_work_fn = BM1397_send_work
        };
        GLOBAL_STATE.asic_job_frequency_ms = BM1397_FULLSCAN_MS;

        GLOBAL_STATE.ASIC_functions = ASIC_functions;
    }else{
        ESP_LOGI(TAG, "Invalid ASIC model");
        exit(EXIT_FAILURE);
    }

    ESP_LOGI(TAG, "Welcome to the bitaxe!");
    //wait between 0 and 5 seconds for multiple units
    vTaskDelay(rand() % 5001 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(nvs_flash_init());

    xTaskCreate(SYSTEM_task, "SYSTEM_task", 4096, (void*)&GLOBAL_STATE, 3, NULL);

    //pull the wifi credentials out of NVS
    char * wifi_ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, WIFI_SSID);
    char * wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS, WIFI_PASS);

    //copy the wifi ssid to the global state
    strncpy(GLOBAL_STATE.SYSTEM_MODULE.ssid, wifi_ssid, 20);

    //init and connect to wifi
    wifi_init(wifi_ssid, wifi_pass);
    start_rest_server((void*)&GLOBAL_STATE);
    EventBits_t result_bits = wifi_connect();

    if (result_bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", wifi_ssid);
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "Connected!", 20);
    } else if (result_bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", wifi_ssid);
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "Failed to connect", 20);
        // User might be trying to configure with AP, just chill here
        ESP_LOGI(TAG,"Finished, waiting for user input.");
        while(1){
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "unexpected error", 20);
        // User might be trying to configure with AP, just chill here
        ESP_LOGI(TAG,"Finished, waiting for user input.");
        while(1){
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    wifi_softap_off();

    free(wifi_ssid);
    free(wifi_pass);

    queue_init(&GLOBAL_STATE.stratum_queue);
    queue_init(&GLOBAL_STATE.ASIC_jobs_queue);

    SERIAL_init();

    (*GLOBAL_STATE.ASIC_functions.init_fn)(GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value);

    //set the startup_done flag
    GLOBAL_STATE.SYSTEM_MODULE.startup_done = true;

    xTaskCreate(stratum_task, "stratum admin", 8192, (void*)&GLOBAL_STATE, 5, NULL);
    xTaskCreate(create_jobs_task, "stratum miner", 8192, (void*)&GLOBAL_STATE, 10, NULL);
    xTaskCreate(POWER_MANAGEMENT_task, "power mangement", 8192, (void*)&GLOBAL_STATE, 10, NULL);
    xTaskCreate(ASIC_task, "asic", 8192, (void*)&GLOBAL_STATE, 10, NULL);
    xTaskCreate(ASIC_result_task, "asic result", 8192, (void*)&GLOBAL_STATE, 15, NULL);

    xTaskCreate(USER_INPUT_task, "user input", 8192, (void*)&GLOBAL_STATE, 5, NULL);


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
