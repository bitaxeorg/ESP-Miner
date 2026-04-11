#include <string.h>
#include <inttypes.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "system.h"
#include "asic_common.h"
#include "asic.h"
#include "utils.h"
#include "esp_system.h"
#include "asic_init.h"
#include "driver/uart.h"

#define EPSILON 0.0001f

#define HASHRATE_UNIT 0x100000uLL // Hashrate register unit (2^24 hashes)

#define POLL_RATE 5000
#define HASHRATE_1M_SIZE (60000 / POLL_RATE)  // 12
#define HASHRATE_10M_SIZE 10
#define HASHRATE_1H_SIZE 6
#define DIV_10M (HASHRATE_1M_SIZE)
#define DIV_1H (HASHRATE_10M_SIZE * DIV_10M)

// Consecutive anomalous polls before triggering ASIC recovery
#define ANOMALY_CONSECUTIVE_THRESHOLD 3
// Delay after ASIC recovery before resuming normal operation
#define RECOVERY_STABILIZATION_DELAY_MS 2000
// Max recovery attempts before forcing a full reboot
#define MAX_RECOVERY_ATTEMPTS 3
// Lower/upper hashrate thresholds relative to expected hashrate
#define LOWER_HASHRATE_THRESHOLD 0.75f
#define UPPER_HASHRATE_THRESHOLD 1.50f

static unsigned long poll_count = 0;
static float hashrate_1m[HASHRATE_1M_SIZE];
static float hashrate_10m_prev;
static float hashrate_10m[HASHRATE_10M_SIZE];
static float hashrate_1h_prev;
static float hashrate_1h[HASHRATE_1H_SIZE];

static const char *TAG = "hashrate_monitor";

static uint8_t consecutive_anomaly_count = 0;
static int recovery_count = 0;
static bool warmup_complete = false;

static float sum_hashrates(measurement_t * measurement, int asic_count)
{
    if (asic_count == 1) return measurement[0].hashrate;

    float total = 0;
    for (int asic_nr = 0; asic_nr < asic_count; asic_nr++) {
        total += measurement[asic_nr].hashrate;
    }
    return total;
}

void hashrate_monitor_reset_measurements(void *pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;    
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;

    if (!HASHRATE_MONITOR_MODULE->is_initialized) {
        return;
    }

    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;

    memset(HASHRATE_MONITOR_MODULE->total_measurement, 0, asic_count * sizeof(measurement_t));
    memset(HASHRATE_MONITOR_MODULE->domain_measurements[0], 0, asic_count * hash_domains * sizeof(measurement_t));
    memset(HASHRATE_MONITOR_MODULE->error_measurement, 0, asic_count * sizeof(measurement_t));
}

void update_hashrate(measurement_t * measurement, uint32_t value)
{
    uint8_t flag_long = (value & 0x80000000) >> 31;
    uint32_t hashrate_value = value & 0x7FFFFFFF;    

    if (hashrate_value != 0x007FFFFF && !flag_long) {
        float hashrate = hashrate_value * (float)HASHRATE_UNIT; // Make sure it stays in float
        measurement->hashrate =  hashrate / 1e9f; // Convert to Gh/s
    }
}

void update_hash_counter(measurement_t * measurement, uint32_t value, uint64_t time_us)
{
    uint64_t previous_time_us = measurement->time_us;
    if (previous_time_us != 0) {
        uint64_t duration_us = time_us - previous_time_us;
        uint32_t counter = value - measurement->value; // Compute counter difference, handling uint32_t wraparound
        measurement->hashrate = hashCounterToGhs(duration_us, counter);
    }

    measurement->value = value;
    measurement->time_us = time_us;
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

void check_hashrate_anomaly(void *pvParameters, float current_hashrate)
{
    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;
    float expected_hashrate = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate;

    if (expected_hashrate <= 0.0f || GLOBAL_STATE->SYSTEM_MODULE.mining_paused) {
        return;
    }

    // Skip anomaly detection until hashrate has reached at least 50% of expected (warmup)
    if (!warmup_complete) {
        if (current_hashrate >= expected_hashrate * 0.5f) {
            warmup_complete = true;
            ESP_LOGI(TAG, "Warmup complete, enabling anomaly detection");
        }
        return;
    }

    bool is_low  = current_hashrate < expected_hashrate * LOWER_HASHRATE_THRESHOLD;
    bool is_high = current_hashrate > expected_hashrate * UPPER_HASHRATE_THRESHOLD;

    if (is_low || is_high) {
        consecutive_anomaly_count++;
        ESP_LOGW(TAG, "Abnormal hashrate: %.3f Gh/s (expected: %.3f Gh/s), count: %d",
                 current_hashrate, expected_hashrate, consecutive_anomaly_count);
    } else {
        consecutive_anomaly_count = 0;
        return;
    }

    if (consecutive_anomaly_count < ANOMALY_CONSECUTIVE_THRESHOLD) {
        return;
    }

    recovery_count++;
    ESP_LOGW(TAG, "ASIC recovery #%d due to sustained abnormal hashrate", recovery_count);

    // Stop tasks from using UART, then wait for current operations to finish
    GLOBAL_STATE->ASIC_initalized = false;
    vTaskDelay(500 / portTICK_PERIOD_MS);

    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    uint8_t chip_count = asic_initialize(GLOBAL_STATE, ASIC_INIT_RECOVERY, RECOVERY_STABILIZATION_DELAY_MS);

    if (chip_count > 0) {
        ESP_LOGI(TAG, "ASIC recovery successful");
        hashrate_monitor_reset_measurements(GLOBAL_STATE);
        warmup_complete = false;
        recovery_count = 0;
    } else {
        ESP_LOGE(TAG, "ASIC recovery failed (chip count 0), rebooting");
        esp_restart();
    }

    consecutive_anomaly_count = 0;

    if (recovery_count >= MAX_RECOVERY_ATTEMPTS) {
        ESP_LOGE(TAG, "Rebooting after %d recovery attempts", recovery_count);
        esp_restart();
    }
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

    hashrate_monitor_reset_measurements(GLOBAL_STATE);

    init_averages();

    HASHRATE_MONITOR_MODULE->is_initialized = true;

    bool was_asic_initialized = false;
    TickType_t taskWakeTime = xTaskGetTickCount();
    while (1) {
        bool is_asic_initialized = GLOBAL_STATE->ASIC_initalized;

        if (was_asic_initialized && !is_asic_initialized) {
            // ASIC just stopped (pause or overheat): clear measurements so that
            // time_us resets to 0. This prevents update_hash_counter from computing
            // a huge uint32_t wraparound diff (counter resets to 0 on ASIC reset)
            // which would cause a hashrate spike when resuming.
            hashrate_monitor_reset_measurements(GLOBAL_STATE);
        }
        was_asic_initialized = is_asic_initialized;

        if (is_asic_initialized) {
            ASIC_read_registers(GLOBAL_STATE);
            vTaskDelay(100 / portTICK_PERIOD_MS);

            float current_hashrate = sum_hashrates(HASHRATE_MONITOR_MODULE->total_measurement, asic_count);
            float error_hashrate = sum_hashrates(HASHRATE_MONITOR_MODULE->error_measurement, asic_count);

            SYSTEM_MODULE->current_hashrate = current_hashrate;
            SYSTEM_MODULE->error_percentage = current_hashrate > 0 ? error_hashrate / current_hashrate * 100.f : 0;

            if (current_hashrate > 0.0f) update_hashrate_averages(SYSTEM_MODULE);

            check_hashrate_anomaly(pvParameters, current_hashrate);
        } else {
            SYSTEM_MODULE->current_hashrate = 0;
        }

        vTaskDelayUntil(&taskWakeTime, POLL_RATE / portTICK_PERIOD_MS);
    }
}

void hashrate_monitor_register_read(void *pvParameters, register_type_t register_type, uint8_t asic_nr, uint32_t value)
{
    uint64_t time_us = esp_timer_get_time();

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
            update_hash_counter(&HASHRATE_MONITOR_MODULE->total_measurement[asic_nr], value, time_us);
            break;
        case REGISTER_DOMAIN_0_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][0], value, time_us);
            break;
        case REGISTER_DOMAIN_1_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][1], value, time_us);
            break;
        case REGISTER_DOMAIN_2_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][2], value, time_us);
            break;
        case REGISTER_DOMAIN_3_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][3], value, time_us);
            break;
        case REGISTER_ERROR_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->error_measurement[asic_nr], value, time_us);
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
