#include <string.h>
#include <esp_log.h>
#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "asic.h"
#include "device_config.h"
#include "power_management_task.h"

static const double NONCE_SPACE = 4294967296.0; //  2^32

static const char *TAG = "asic";

uint8_t ASIC_init()
{
    ESP_LOGI(TAG, "Initializing %s", DEVICE_CONFIG.family.asic.name);
    switch (DEVICE_CONFIG.family.asic.id) {

        case BM1397:
            return BM1397_init(POWER_MANAGEMENT_MODULE.frequency_value, DEVICE_CONFIG.family.asic_count, DEVICE_CONFIG.family.asic.difficulty);
        case BM1366:
            return BM1366_init(POWER_MANAGEMENT_MODULE.frequency_value, DEVICE_CONFIG.family.asic_count, DEVICE_CONFIG.family.asic.difficulty);
        case BM1368:
            return BM1368_init(POWER_MANAGEMENT_MODULE.frequency_value, DEVICE_CONFIG.family.asic_count, DEVICE_CONFIG.family.asic.difficulty);
        case BM1370:
            return BM1370_init(POWER_MANAGEMENT_MODULE.frequency_value, DEVICE_CONFIG.family.asic_count, DEVICE_CONFIG.family.asic.difficulty);
        default:
    }
    return ESP_OK;
}

task_result * ASIC_process_work()
{
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            return BM1397_process_work();
        case BM1366:
            return BM1366_process_work();
        case BM1368:
            return BM1368_process_work();
        case BM1370:
            return BM1370_process_work();
    }
    return NULL;
}

int ASIC_set_max_baud()
{
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            return BM1397_set_max_baud();
        case BM1366:
            return BM1366_set_max_baud();
        case BM1368:
            return BM1368_set_max_baud();
        case BM1370:
            return BM1370_set_max_baud();
        }
    return 0;
}

void ASIC_send_work(void * next_job)
{
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            BM1397_send_work(next_job);
            break;
        case BM1366:
            BM1366_send_work(next_job);
            break;
        case BM1368:
            BM1368_send_work(next_job);
            break;
        case BM1370:
            BM1370_send_work(next_job);
            break;
    }
}

void ASIC_set_version_mask(uint32_t mask)
{
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            BM1397_set_version_mask(mask);
            break;
        case BM1366:
            BM1366_set_version_mask(mask);
            break;
        case BM1368:
            BM1368_set_version_mask(mask);
            break;
        case BM1370:
            BM1370_set_version_mask(mask);
            break;
    }
}

bool ASIC_set_frequency(float target_frequency)
{
    //ESP_LOGI(TAG, "Setting ASIC frequency to %.2f MHz", target_frequency);
    bool success = false;
    
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1366:
            success = BM1366_set_frequency(target_frequency);
            break;
        case BM1368:
            success = BM1368_set_frequency(target_frequency);
            break;
        case BM1370:
            success = BM1370_set_frequency(target_frequency);
            break;
        case BM1397:
            // BM1397 doesn't have a set_frequency function yet
            ESP_LOGE(TAG, "Frequency transition not implemented for BM1397");
            success = false;
            break;
    }
    
    if (success) {
        ESP_LOGI(TAG, "Successfully transitioned to new ASIC frequency: %.2f MHz", target_frequency);
    } else {
        ESP_LOGE(TAG, "Failed to transition to new ASIC frequency: %.2f MHz", target_frequency);
    }
    
    return success;
}

double ASIC_get_asic_job_frequency_ms()
{
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            // no version-rolling so same Nonce Space is splitted between Small Cores
            return (NONCE_SPACE / (double) (POWER_MANAGEMENT_MODULE.frequency_value * DEVICE_CONFIG.family.asic.small_core_count * 1000)) / (double) DEVICE_CONFIG.family.asic_count;
        case BM1366:
            return 2000;
        case BM1368:
        case BM1370:
            return 500;
    }
    return 500;
}
