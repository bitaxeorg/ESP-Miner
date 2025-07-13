#include <string.h>
#include <esp_log.h>
#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "asic.h"

static const char *TAG = "asic";

task_result*(*process_work)();
int (*set_max_baud)();
void (*send_work)(bm_job * next_job);
void (*set_version_mask)(uint32_t mask);
bool (*set_frequency)(float target_frequency);
uint8_t asic_count;
uint16_t difficulty;

uint8_t ASIC_init(float frequency, int id, uint8_t _asic_count,uint16_t _difficulty)
{
    //ESP_LOGI(TAG, "Initializing %s", DEVICE_CONFIG.family.asic.name);
    asic_count = _asic_count;
    difficulty = _difficulty;
    switch (id) {

        case 0:
            process_work = BM1397_process_work;
            set_max_baud =BM1397_set_max_baud;
            send_work = BM1397_send_work;
            set_version_mask = BM1397_set_version_mask;
            set_frequency = NULL;
            return BM1397_init(frequency, asic_count, difficulty);
        case 1:
            process_work = BM1366_process_work;
            set_max_baud = BM1366_set_max_baud;
            send_work = BM1366_send_work;
            set_version_mask = BM1366_set_version_mask;
            set_frequency = BM1366_set_frequency;
            return BM1366_init(frequency, asic_count, difficulty);
        case 2:
            process_work =BM1368_process_work;
            set_max_baud = BM1368_set_max_baud;
            send_work = BM1368_send_work;
            set_version_mask = BM1368_set_version_mask;
            set_frequency = BM1368_set_frequency;
            return BM1368_init(frequency, asic_count, difficulty);
        case 3:
            process_work = BM1370_process_work;
            set_max_baud = BM1370_set_max_baud;
            send_work = BM1370_send_work;
            set_version_mask = BM1370_set_version_mask;
            set_frequency = BM1370_set_frequency;
            return BM1370_init(frequency, asic_count, difficulty);
        default:
    }
    return ESP_OK;
}

task_result * ASIC_process_work()
{
    return process_work();
}

int ASIC_set_max_baud()
{
    return set_max_baud();
}

void ASIC_send_work(void * next_job)
{
    send_work(next_job);
}

void ASIC_set_version_mask(uint32_t mask)
{
    set_version_mask(mask);
}

bool ASIC_set_frequency(float target_frequency)
{
    //ESP_LOGI(TAG, "Setting ASIC frequency to %.2f MHz", target_frequency);
    bool success = set_frequency(target_frequency);
    if (success) {
        ESP_LOGI(TAG, "Successfully transitioned to new ASIC frequency: %.2f MHz", target_frequency);
    } else {
        ESP_LOGE(TAG, "Failed to transition to new ASIC frequency: %.2f MHz", target_frequency);
    }
    
    return success;
}

