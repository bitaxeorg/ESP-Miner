#include <string.h>
#include <esp_log.h>
#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "asic.h"
#include "device_config.h"
#include "power_management_module.h"
#include "frequency_transition_bmXX.h"

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
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            ESP_LOGE(TAG, "Frequency transition not implemented for BM1397");
            return false;
        case BM1366:
            do_frequency_transition(target_frequency, BM1366_send_hash_frequency);
            return true;
        case BM1368:
            do_frequency_transition(target_frequency, BM1368_send_hash_frequency);
            return true;
        case BM1370:
            do_frequency_transition(target_frequency, BM1370_send_hash_frequency);
            return true;
    }
    return false;
}
