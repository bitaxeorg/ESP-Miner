#include <string.h>

#include <esp_log.h>

#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"

#include "asic.h"
#include "device_config.h"
#include "frequency_transition_bmXX.h"

static const char *TAG = "asic";

uint8_t ASIC_init(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Initializing %dx %s", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            return BM1397_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);
        case BM1366:
            return BM1366_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);
        case BM1368:
            return BM1368_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);
        case BM1370:
            return BM1370_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);
    }
    return ESP_OK;
}

task_result * ASIC_process_work(GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            return BM1397_process_work(GLOBAL_STATE);
        case BM1366:
            return BM1366_process_work(GLOBAL_STATE);
        case BM1368:
            return BM1368_process_work(GLOBAL_STATE);
        case BM1370:
            return BM1370_process_work(GLOBAL_STATE);
    }
    return NULL;
}

int ASIC_set_max_baud(GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
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

void ASIC_send_work(GlobalState * GLOBAL_STATE, void * next_job)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            BM1397_send_work(GLOBAL_STATE, next_job);
            break;
        case BM1366:
            BM1366_send_work(GLOBAL_STATE, next_job);
            break;
        case BM1368:
            BM1368_send_work(GLOBAL_STATE, next_job);
            break;
        case BM1370:
            BM1370_send_work(GLOBAL_STATE, next_job);
            break;
    }
}

void ASIC_set_version_mask(GlobalState * GLOBAL_STATE, uint32_t mask)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
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

bool ASIC_set_frequency(GlobalState * GLOBAL_STATE, float frequency)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            do_frequency_transition(frequency, BM1397_send_hash_frequency);
            return true;
        case BM1366:
            do_frequency_transition(frequency, BM1366_send_hash_frequency);
            return true;
        case BM1368:
            do_frequency_transition(frequency, BM1368_send_hash_frequency);
            return true;
        case BM1370:
            do_frequency_transition(frequency, BM1370_send_hash_frequency);
            return true;
    }
    return false;
}

double ASIC_get_asic_job_frequency_ms(GlobalState * GLOBAL_STATE)
{
    float freq = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    int cores = GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count;
    int small_cores = GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count;
    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int asic_default_timeout_divided = GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_asic_timeout / _next_power_of_two(asic_count);

    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            // no version-rolling so same Nonce Space is splitted between Big Cores
            return calculate_bm_timeout_ms(freq, asic_count, small_cores, cores, 4.0, 1.0, asic_default_timeout_divided);
        case BM1366:
        case BM1368:
        case BM1370:
            return asic_default_timeout_divided;
    }
    return 500;
}

void ASIC_read_registers(GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            BM1397_read_registers();
            break;
        case BM1366:
            BM1366_read_registers();
            break;
        case BM1368:
            BM1368_read_registers();
            break;
        case BM1370:
            BM1370_read_registers();
            break;
    }
}
