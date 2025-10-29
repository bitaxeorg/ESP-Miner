#include <string.h>
#include <esp_heap_caps.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "system.h"
#include "common.h"
#include "asic.h"

#define POLL_RATE 5000
#define EMA_ALPHA 12

#define HASH_CNT_LSB 0x100000000uLL // Hash counters are incremented on difficulty 1 (2^32 hashes)
#define HASHRATE_UNIT 0x100000uLL // Hashrate register unit (2^24 hashes)

static const char *TAG = "hashrate_monitor";

static float frequency_value;

static float sum_hashrates(measurement_t * measurement, int asic_count)
{
    if (asic_count == 1) return measurement[0].hashrate;

    float total = 0;
    for (int i = 0; i < asic_count; i++) {
        if (measurement[i].hashrate == 0.0) return 0.0;
        total += measurement[i].hashrate;
    }
    return total;
}

static uint32_t sum_values(measurement_t * measurement, int asic_count)
{
    if (asic_count == 1) return measurement[0].value;

    uint32_t total = 0;
    for (int i = 0; i < asic_count; i++) total += measurement[i].value;
    return total;
}

static void clear_measurements(HashrateMonitorModule * HASHRATE_MONITOR_MODULE, int asic_count, int hash_domains)
{
    memset(HASHRATE_MONITOR_MODULE->total_measurement, 0, asic_count * sizeof(measurement_t));
    if (hash_domains > 0) {
        memset(HASHRATE_MONITOR_MODULE->domain_measurements[0], 0, asic_count * hash_domains * sizeof(measurement_t));
    }
    memset(HASHRATE_MONITOR_MODULE->error_measurement, 0, asic_count * sizeof(measurement_t));
}

static float hash_counter_to_ghs(uint32_t duration_ms, uint32_t counter)
{
    if (duration_ms == 0) return 0.0f;
    float seconds = duration_ms / 1000.0;
    float hashrate = counter / seconds * (float)HASH_CNT_LSB; // Make sure it stays in float
    return hashrate / 1e9f; // Convert to Gh/s
}

static void update_hashrate(uint32_t value, measurement_t * measurement, int asic_nr)
{
    uint8_t flag_long = (value & 0x80000000) >> 31;
    uint32_t hashrate_value = value & 0x7FFFFFFF;    

    if (hashrate_value != 0x007FFFFF && !flag_long) {
        float hashrate = hashrate_value * (float)HASHRATE_UNIT; // Make sure it stays in float
        measurement[asic_nr].hashrate =  hashrate / 1e9f; // Convert to Gh/s
    }
}

static void update_hash_counter(uint32_t time_ms, uint32_t value, measurement_t * measurement, int asic_nr)
{
    uint32_t previous_time_ms = measurement[asic_nr].time_ms;
    if (previous_time_ms != 0) {
        uint32_t duration_ms = time_ms - previous_time_ms;
        uint32_t counter = value - measurement[asic_nr].value; // Compute counter difference, handling uint32_t wraparound
        measurement[asic_nr].hashrate = hash_counter_to_ghs(duration_ms, counter);
    }

    measurement[asic_nr].value = value;
    measurement[asic_nr].time_ms = time_ms;
}

void hashrate_monitor_task(void *pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;

    HASHRATE_MONITOR_MODULE->total_measurement = heap_caps_malloc(asic_count * sizeof(measurement_t), MALLOC_CAP_SPIRAM);
    if (hash_domains > 0) {
        measurement_t* data = malloc(asic_count * hash_domains * sizeof(measurement_t));
        HASHRATE_MONITOR_MODULE->domain_measurements = heap_caps_malloc(hash_domains * sizeof(measurement_t*), MALLOC_CAP_SPIRAM);
        for (size_t i = 0; i < hash_domains; i++) {
            HASHRATE_MONITOR_MODULE->domain_measurements[i] = data + (i * asic_count);
        }
    }
    HASHRATE_MONITOR_MODULE->error_measurement = heap_caps_malloc(asic_count * sizeof(measurement_t), MALLOC_CAP_SPIRAM);

    clear_measurements(HASHRATE_MONITOR_MODULE, asic_count, hash_domains);

    HASHRATE_MONITOR_MODULE->is_initialized = true;

    TickType_t taskWakeTime = xTaskGetTickCount();
    while (1) {
        ASIC_read_registers(GLOBAL_STATE);

        vTaskDelay(100 / portTICK_PERIOD_MS);

        float hashrate = sum_hashrates(HASHRATE_MONITOR_MODULE->total_measurement, asic_count);

        if (hashrate == 0.0) {
            HASHRATE_MONITOR_MODULE->hashrate = 0.0;
        } else {
            if (HASHRATE_MONITOR_MODULE->hashrate == 0.0f) {
                HASHRATE_MONITOR_MODULE->hashrate = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate;
            }
            HASHRATE_MONITOR_MODULE->hashrate = ((HASHRATE_MONITOR_MODULE->hashrate * (EMA_ALPHA - 1)) + hashrate) / EMA_ALPHA;
        }

        HASHRATE_MONITOR_MODULE->error_count = sum_values(HASHRATE_MONITOR_MODULE->error_measurement, asic_count);

        vTaskDelayUntil(&taskWakeTime, POLL_RATE / portTICK_PERIOD_MS);
    }
}

void hashrate_monitor_register_read(void *pvParameters, register_type_t register_type, uint8_t asic_nr, uint32_t value)
{
    uint32_t time_ms = esp_timer_get_time() / 1000;

    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    PowerManagementModule * POWER_MANAGEMENT_MODULE = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;

    if (asic_nr >= asic_count) {
        ESP_LOGE(TAG, "Asic nr out of bounds");
        return;
    }

    // Reset statistics on start and when frequency changes
    if (POWER_MANAGEMENT_MODULE->frequency_value != frequency_value) {
        clear_measurements(HASHRATE_MONITOR_MODULE, asic_count, hash_domains);
        frequency_value = POWER_MANAGEMENT_MODULE->frequency_value;
    }

    switch(register_type) {
        case REGISTER_HASHRATE:
            update_hashrate(value, HASHRATE_MONITOR_MODULE->total_measurement, asic_nr);
            break;
        case REGISTER_TOTAL_COUNT:
            update_hash_counter(time_ms, value, HASHRATE_MONITOR_MODULE->total_measurement, asic_nr);
            break;
        case REGISTER_DOMAIN_0_COUNT:
            update_hash_counter(time_ms, value, HASHRATE_MONITOR_MODULE->domain_measurements[0], asic_nr);
            break;
        case REGISTER_DOMAIN_1_COUNT:
            update_hash_counter(time_ms, value, HASHRATE_MONITOR_MODULE->domain_measurements[1], asic_nr);
            break;
        case REGISTER_DOMAIN_2_COUNT:
            update_hash_counter(time_ms, value, HASHRATE_MONITOR_MODULE->domain_measurements[2], asic_nr);
            break;
        case REGISTER_DOMAIN_3_COUNT:
            update_hash_counter(time_ms, value, HASHRATE_MONITOR_MODULE->domain_measurements[3], asic_nr);
            break;
        case REGISTER_ERROR_COUNT:
            update_hash_counter(time_ms, value, HASHRATE_MONITOR_MODULE->error_measurement, asic_nr);
            break;
        case REGISTER_INVALID:
            ESP_LOGE(TAG, "Invalid register type");
            break;
    }
}

/*
    // From NerdAxe codebase, temparature conversion?
    if (asic_result.data & 0x80000000) {
        float ftemp = (float) (asic_result.data & 0x0000ffff) * 0.171342f - 299.5144f;
        ESP_LOGI(TAG, "asic %d temp: %.3f", (int) asic_result.asic_nr, ftemp);
        board->setChipTemp(asic_result.asic_nr, ftemp);
    }
    break;
*/
