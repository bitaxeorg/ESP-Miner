#include <string.h>
#include <inttypes.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "system.h"
#include "common.h"
#include "asic.h"
#include "utils.h"

#define EPSILON 0.0001f

#define HASHRATE_UNIT 0x100000uLL // Hashrate register unit (2^24 hashes)

#define POLL_RATE 5000
#define HASHRATE_1M_SIZE (60000 / POLL_RATE)  // 12
#define HASHRATE_10M_SIZE 10
#define HASHRATE_1H_SIZE 6
#define DIV_10M (HASHRATE_1M_SIZE)
#define DIV_1H (HASHRATE_10M_SIZE * DIV_10M)

static unsigned long poll_count = 0;
static float hashrate_1m[HASHRATE_1M_SIZE];
static float hashrate_10m_prev;
static float hashrate_10m[HASHRATE_10M_SIZE];
static float hashrate_1h_prev;
static float hashrate_1h[HASHRATE_1H_SIZE];

static const char *TAG = "hashrate_monitor";

static float sum_hashrates(measurement_t * measurement, int asic_count)
{
    if (asic_count == 1) return measurement[0].hashrate;

    float total = 0;
    for (int asic_nr = 0; asic_nr < asic_count; asic_nr++) {
        total += measurement[asic_nr].hashrate;
    }
    return total;
}

static void clear_measurements(GlobalState * GLOBAL_STATE)
{
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;

    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;

    memset(HASHRATE_MONITOR_MODULE->total_measurement, 0, asic_count * sizeof(measurement_t));
    memset(HASHRATE_MONITOR_MODULE->domain_measurements[0], 0, asic_count * hash_domains * sizeof(measurement_t));
    memset(HASHRATE_MONITOR_MODULE->error_measurement, 0, asic_count * sizeof(measurement_t));
}

static void update_hashrate(measurement_t * measurement, uint32_t value)
{
    uint8_t flag_long = (value & 0x80000000) >> 31;
    uint32_t hashrate_value = value & 0x7FFFFFFF;    

    if (hashrate_value != 0x007FFFFF && !flag_long) {
        float hashrate = hashrate_value * (float)HASHRATE_UNIT; // Make sure it stays in float
        measurement->hashrate =  hashrate / 1e9f; // Convert to Gh/s
    }
}

static void update_hash_counter(measurement_t * measurement, uint32_t value, uint32_t time_ms)
{
    uint32_t previous_time_ms = measurement->time_ms;
    if (previous_time_ms != 0) {
        uint32_t duration_ms = time_ms - previous_time_ms;
        uint32_t counter = value - measurement->value; // Compute counter difference, handling uint32_t wraparound
        measurement->hashrate = hashCounterToGhs(duration_ms, counter);
    }

    measurement->value = value;
    measurement->time_ms = time_ms;
}

static void init_averages()
{
    float nan_val = nanf("");
    for (int i = 0; i < HASHRATE_1M_SIZE; i++) hashrate_1m[i] = nan_val;
    for (int i = 0; i < HASHRATE_10M_SIZE; i++) hashrate_10m[i] = nan_val;
    for (int i = 0; i < HASHRATE_1H_SIZE; i++) hashrate_1h[i] = nan_val;
}

static float calculate_avg_nan_safe(const float arr[], int size) {
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < size; i++) {
        if (!isnanf(arr[i])) {
            sum += arr[i];
            count++;
        }
    }
    return (count > 0) ? (sum / count) : 0.0f;
}

static void update_hashrate_averages(SystemModule * SYSTEM_MODULE)
{
    hashrate_1m[poll_count % HASHRATE_1M_SIZE] = SYSTEM_MODULE->current_hashrate;
    SYSTEM_MODULE->hashrate_1m = calculate_avg_nan_safe(hashrate_1m, HASHRATE_1M_SIZE);

    int hashrate_10m_blend = poll_count % HASHRATE_1M_SIZE;
    if (hashrate_10m_blend == 0) {
        hashrate_10m_prev = hashrate_10m[(poll_count / DIV_10M) % HASHRATE_10M_SIZE];
    }
    float hashrate_1m_value = SYSTEM_MODULE->hashrate_1m;
    if (!isnanf(hashrate_10m_prev)) {
        float f = (hashrate_10m_blend + 1.0f) / (float)HASHRATE_1M_SIZE;
        hashrate_1m_value = f * hashrate_1m_value + (1.0f - f) * hashrate_10m_prev;
    }

    hashrate_10m[(poll_count / DIV_10M) % HASHRATE_10M_SIZE] = hashrate_1m_value;
    SYSTEM_MODULE->hashrate_10m = calculate_avg_nan_safe(hashrate_10m, HASHRATE_10M_SIZE);

    int hashrate_1h_blend = poll_count % DIV_1H;
    if (hashrate_1h_blend == 0) {
        hashrate_1h_prev = hashrate_1h[(poll_count / DIV_1H) % HASHRATE_1H_SIZE];
    }
    float hashrate_10m_value = SYSTEM_MODULE->hashrate_10m;
    if (!isnanf(hashrate_1h_prev)) {
        float f = (hashrate_1h_blend + 1.0f) / (float)DIV_1H;
        hashrate_10m_value = f * hashrate_10m_value + (1.0f - f) * hashrate_1h_prev;
    }

    hashrate_1h[(poll_count / DIV_1H) % HASHRATE_1H_SIZE] = hashrate_10m_value;
    SYSTEM_MODULE->hashrate_1h = calculate_avg_nan_safe(hashrate_1h, HASHRATE_1H_SIZE);

    poll_count++;
}

void hashrate_monitor_task(void *pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    SystemModule * SYSTEM_MODULE = &GLOBAL_STATE->SYSTEM_MODULE;

    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;

    HASHRATE_MONITOR_MODULE->total_measurement = heap_caps_malloc(asic_count * sizeof(measurement_t), MALLOC_CAP_SPIRAM);
    measurement_t* data = heap_caps_malloc(asic_count * hash_domains * sizeof(measurement_t), MALLOC_CAP_SPIRAM);
    HASHRATE_MONITOR_MODULE->domain_measurements = heap_caps_malloc(asic_count * sizeof(measurement_t*), MALLOC_CAP_SPIRAM);
    for (size_t asic_nr = 0; asic_nr < asic_count; asic_nr++) {
        HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr] = data + (asic_nr * hash_domains);
    }
    HASHRATE_MONITOR_MODULE->error_measurement = heap_caps_malloc(asic_count * sizeof(measurement_t), MALLOC_CAP_SPIRAM);

    clear_measurements(GLOBAL_STATE);

    init_averages();

    HASHRATE_MONITOR_MODULE->is_initialized = true;

    TickType_t taskWakeTime = xTaskGetTickCount();
    while (1) {
        ASIC_read_registers(GLOBAL_STATE);

        vTaskDelay(100 / portTICK_PERIOD_MS);

        float current_hashrate = sum_hashrates(HASHRATE_MONITOR_MODULE->total_measurement, asic_count);
        float error_hashrate = sum_hashrates(HASHRATE_MONITOR_MODULE->error_measurement, asic_count);

        SYSTEM_MODULE->current_hashrate = current_hashrate;
        SYSTEM_MODULE->error_percentage = current_hashrate > 0 ? error_hashrate / current_hashrate * 100.f : 0;

        if(current_hashrate > 0.0f) update_hashrate_averages(SYSTEM_MODULE);

        vTaskDelayUntil(&taskWakeTime, POLL_RATE / portTICK_PERIOD_MS);
    }
}

void hashrate_monitor_register_read(void *pvParameters, register_type_t register_type, uint8_t asic_nr, uint32_t value)
{
    uint32_t time_ms = esp_timer_get_time() / 1000;

    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;

    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;

    if (asic_nr >= asic_count) {
        ESP_LOGE(TAG, "Asic nr out of bounds [%d]", asic_nr);
        return;
    }

    switch(register_type) {
        case REGISTER_HASHRATE:
            update_hashrate(&HASHRATE_MONITOR_MODULE->total_measurement[asic_nr], value);
            update_hashrate(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][0], value);
            break;
        case REGISTER_TOTAL_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->total_measurement[asic_nr], value, time_ms);
            break;
        case REGISTER_DOMAIN_0_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][0], value, time_ms);
            break;
        case REGISTER_DOMAIN_1_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][1], value, time_ms);
            break;
        case REGISTER_DOMAIN_2_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][2], value, time_ms);
            break;
        case REGISTER_DOMAIN_3_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][3], value, time_ms);
            break;
        case REGISTER_ERROR_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->error_measurement[asic_nr], value, time_ms);
            break;
        case REGISTER_PLL_PARAM:
            ESP_LOGD(TAG, "PLL param read asic %d: 0x%08" PRIX32, asic_nr, value);
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
