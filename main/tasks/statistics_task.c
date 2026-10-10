#include <stdint.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "statistics_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "connect.h"

#define DEFAULT_POLL_RATE 1000

static const char * TAG = "statistics_task";

static StatisticsDataPtr statisticsBuffer;
static uint16_t statisticsDataSize;
static pthread_mutex_t statisticsDataLock = PTHREAD_MUTEX_INITIALIZER;

static const uint16_t maxDataCount = MAX_STATISTICS_COUNT;

void createStatisticsBuffer()
{
    if (NULL == statisticsBuffer) {
        pthread_mutex_lock(&statisticsDataLock);

        if (NULL == statisticsBuffer) {
            statisticsBuffer = (StatisticsDataPtr)heap_caps_malloc(sizeof(struct StatisticsData) * maxDataCount, MALLOC_CAP_SPIRAM);
            if (NULL == statisticsBuffer) {
                ESP_LOGW(TAG, "Not enough memory for the statistics data buffer!");
            }
        }

        pthread_mutex_unlock(&statisticsDataLock);
    }
}

void removeStatisticsBuffer()
{
    if (NULL != statisticsBuffer) {
        pthread_mutex_lock(&statisticsDataLock);

        if (NULL != statisticsBuffer) {
            heap_caps_free(statisticsBuffer);

            statisticsBuffer = NULL;
            statisticsDataSize = 0;
        }

        pthread_mutex_unlock(&statisticsDataLock);
    }
}

static inline void merge_min_positive(float *target, float val)
{
    if (val > 0.0f && (*target <= 0.0f || val < *target)) {
        *target = val;
    }
}

static inline void merge_max_positive(float *target, float val)
{
    if (val > 0.0f && (*target <= 0.0f || val > *target)) {
        *target = val;
    }
}

bool addStatisticData(StatisticsDataPtr data, uint16_t statsFrequency)
{
    bool result = false;

    if (NULL == data) {
        return result;
    }

    createStatisticsBuffer();

    pthread_mutex_lock(&statisticsDataLock);

    if (NULL != statisticsBuffer) {
        if (statisticsDataSize < maxDataCount) {
            statisticsBuffer[statisticsDataSize] = *data;
            statisticsDataSize++;
            result = true;
        } else {
            // Buffer is full. Determine indexToRemove using Triangle Area thinning logic.
            uint16_t indexToRemove = 0;
            const uint64_t currentSpan = data->timestamp - statisticsBuffer[0].timestamp;
            const uint64_t targetDuration = (uint64_t)maxDataCount * (uint64_t)statsFrequency * 1000;

            if (currentSpan >= targetDuration) {
                indexToRemove = 0;
            } else {
                uint16_t low = 1;
                uint16_t high = maxDataCount - 1;

                while (high - low > 1) {
                    uint64_t lowTime = statisticsBuffer[low].timestamp;
                    uint64_t highTime = statisticsBuffer[high].timestamp;
                    uint64_t midTime = (lowTime + highTime) / 2;

                    uint16_t split = low;
                    for (uint16_t i = low; i <= high; i++) {
                        uint64_t t = statisticsBuffer[i].timestamp;
                        if (t >= midTime) {
                            split = i;
                            break;
                        }
                    }

                    // Ensure progress
                    if (split == low) split++;
                    if (split > high) split = high;

                    uint16_t leftCount = split - low;
                    uint16_t rightCount = high - split + 1;

                    if (leftCount > rightCount) {
                        high = split - 1;
                    } else {
                        low = split;
                    }
                }
                indexToRemove = low;
            }

            struct StatisticsData *toRemove = &statisticsBuffer[indexToRemove];
            struct StatisticsData *successor = &statisticsBuffer[indexToRemove + 1];

            // Merge into successor before removing indexToRemove
            if (indexToRemove > 0) {
                merge_min_positive(&successor->responseTimeMin, toRemove->responseTimeMin);
                merge_max_positive(&successor->responseTimeMax, toRemove->responseTimeMax);
            }

            // Shift and append (Standard linear array shift)
            if (indexToRemove < maxDataCount - 1) {
                memmove(toRemove, successor, (maxDataCount - indexToRemove - 1) * sizeof(struct StatisticsData));
            }
            statisticsBuffer[maxDataCount - 1] = *data;
            result = true;
        }
    }

    pthread_mutex_unlock(&statisticsDataLock);

    return result;
}

bool getStatisticData(uint16_t index, StatisticsDataPtr dataOut)
{
    bool result = false;

    if ((NULL == statisticsBuffer) || (NULL == dataOut) || (maxDataCount <= index)) {
        return result;
    }

    pthread_mutex_lock(&statisticsDataLock);

    if ((NULL != statisticsBuffer) && (index < statisticsDataSize)) {
        *dataOut = statisticsBuffer[index];
        result = true;
    }

    pthread_mutex_unlock(&statisticsDataLock);

    return result;
}

void statistics_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    struct StatisticsData statsData = {};
    uint64_t last_shares_count = sys_module->shares_accepted + sys_module->shares_rejected;

    TickType_t taskWakeTime = xTaskGetTickCount();

    while (1) {
        const uint64_t currentTime = esp_timer_get_time() / 1000;
        const uint16_t configStatsFrequency = nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY);

        if (0 != configStatsFrequency) {
            // Record every second (DEFAULT_POLL_RATE is 1000ms)
            if (currentTime >= statsData.timestamp + 1000) {
                int8_t wifiRSSI = -90;
                get_wifi_current_rssi(&wifiRSSI);

                statsData.timestamp = currentTime;
                statsData.hashrate = sys_module->current_hashrate;
                statsData.hashrate_1m = sys_module->hashrate_1m;
                statsData.hashrate_10m = sys_module->hashrate_10m;
                statsData.hashrate_1h = sys_module->hashrate_1h;
                statsData.errorPercentage = sys_module->error_percentage;
                statsData.chipTemperature = power_management->chip_temp_avg;
                statsData.chipTemperature2 = power_management->chip_temp2_avg;
                statsData.vrTemperature = power_management->vr_temp;
                statsData.power = power_management->power;
                statsData.voltage = power_management->voltage;
                statsData.current = power_management->current;
                statsData.coreVoltageActual = power_management->core_voltage;
                statsData.fanSpeed = power_management->fan_perc;
                statsData.fanRPM = power_management->fan_rpm;
                statsData.fan2RPM = power_management->fan2_rpm;
                statsData.wifiRSSI = wifiRSSI;
                statsData.freeHeap = esp_get_free_heap_size();

                uint64_t current_shares = sys_module->shares_accepted + sys_module->shares_rejected;
                if (current_shares != last_shares_count && sys_module->response_time > 0.0f) {
                    statsData.responseTimeMin = sys_module->response_time_min > 0.0f ? sys_module->response_time_min : sys_module->response_time;
                    statsData.responseTimeMax = sys_module->response_time_max > 0.0f ? sys_module->response_time_max : sys_module->response_time;
                    sys_module->last_response_time_min = statsData.responseTimeMin;
                    sys_module->last_response_time_max = statsData.responseTimeMax;
                } else {
                    statsData.responseTimeMin = 0.0f;
                    statsData.responseTimeMax = 0.0f;
                    sys_module->last_response_time_min = 0.0f;
                    sys_module->last_response_time_max = 0.0f;
                }
                last_shares_count = current_shares;
                sys_module->response_time_min = 0.0f;
                sys_module->response_time_max = 0.0f;

                addStatisticData(&statsData, configStatsFrequency);
            }
        } else {
            removeStatisticsBuffer();
        }

        vTaskDelayUntil(&taskWakeTime, DEFAULT_POLL_RATE / portTICK_PERIOD_MS); // taskWakeTime is automatically updated
    }
}
