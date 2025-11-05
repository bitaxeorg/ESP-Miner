#include <stdint.h>
#include <pthread.h>
#include "esp_log.h"
#include "esp_timer.h"
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "statistics_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "power.h"
#include "connect.h"
#include "vcore.h"
#include "bm1370.h"

#define DEFAULT_POLL_RATE 5000

static const char * TAG = "statistics_task";

static StatisticsDataPtr statisticsBuffer;
static uint16_t statisticsDataStart;
static uint16_t statisticsDataSize;
static pthread_mutex_t statisticsDataLock = PTHREAD_MUTEX_INITIALIZER;

static const uint16_t maxDataCount = 720;
static uint16_t statsFrequency;

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
            statisticsDataStart = 0;
            statisticsDataSize = 0;
        }

        pthread_mutex_unlock(&statisticsDataLock);
    }
}

bool addStatisticData(StatisticsDataPtr data)
{
    bool result = false;

    if (NULL == data) {
        return result;
    }

    createStatisticsBuffer();

    pthread_mutex_lock(&statisticsDataLock);

    if (NULL != statisticsBuffer) {
        statisticsDataSize++;

        if (maxDataCount < statisticsDataSize) {
            statisticsDataSize = maxDataCount;
            statisticsDataStart++;
            statisticsDataStart = statisticsDataStart % maxDataCount;
        }

        const uint16_t last = (statisticsDataStart + statisticsDataSize - 1) % maxDataCount;
        statisticsBuffer[last] = *data;
        result = true;
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
        index = (statisticsDataStart + index) % maxDataCount;
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
    HashrateMonitorModule * hashrate_monitor = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    struct StatisticsData statsData = {};

    TickType_t taskWakeTime = xTaskGetTickCount();

    while (1) {
        const int32_t currentTime = esp_timer_get_time() / 1000;
        statsFrequency = nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY) * 1000;

        if (0 != statsFrequency) {
            const int32_t waitingTime = statsData.timestamp + statsFrequency - (DEFAULT_POLL_RATE / 2);

            if (currentTime > waitingTime) {
                int8_t wifiRSSI = -90;
                get_wifi_current_rssi(&wifiRSSI);

                statsData.timestamp = currentTime;
                statsData.hashrate = sys_module->current_hashrate;
                statsData.errorCount = hashrate_monitor->error_count;
                statsData.chipTemperature = power_management->chip_temp_avg;
                statsData.vrTemperature = power_management->vr_temp;
                statsData.power = power_management->power;
                statsData.voltage = power_management->voltage;
                statsData.current = Power_get_current(GLOBAL_STATE);
                statsData.coreVoltageActual = VCORE_get_voltage_mv(GLOBAL_STATE);
                statsData.fanSpeed = power_management->fan_perc;
                statsData.fanRPM = power_management->fan_rpm;
                statsData.fan2RPM = power_management->fan2_rpm;
                statsData.wifiRSSI = wifiRSSI;
                statsData.freeHeap = esp_get_free_heap_size();

                addStatisticData(&statsData);
            }
        } else {
            removeStatisticsBuffer();
        }

        vTaskDelayUntil(&taskWakeTime, DEFAULT_POLL_RATE / portTICK_PERIOD_MS); // taskWakeTime is automatically updated
    }
}
