#include <stdint.h>
#include <pthread.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "statistics_task.h"

#include "nvs_config.h"
#include "power.h"
#include "connect.h"
#include "vcore.h"
#include "power_management_module.h"
#include "system_module.h"

#define DEFAULT_POLL_RATE 5000

static const char * TAG = "statistics_task";

static StatisticsNodePtr statisticsDataStart = NULL;
static StatisticsNodePtr statisticsDataEnd = NULL;
static pthread_mutex_t statisticsDataLock = PTHREAD_MUTEX_INITIALIZER;

static const uint16_t maxDataCount = 720;
static uint16_t currentDataCount;
static uint16_t statsFrequency;

StatisticsNodePtr addStatisticData(StatisticsNodePtr data)
{
    if ((NULL == data) || (0 == statsFrequency)) {
        return NULL;
    }

    StatisticsNodePtr newData = NULL;

    // create new data block or reuse first data block
    if (currentDataCount < maxDataCount) {
        newData = (StatisticsNodePtr)malloc(sizeof(struct StatisticsData));
        currentDataCount++;
    } else {
        newData = statisticsDataStart;
    }

    // set data
    if (NULL != newData) {
        pthread_mutex_lock(&statisticsDataLock);

        if (NULL == statisticsDataStart) {
            statisticsDataStart = newData; // set first new data block
        } else {
            if ((statisticsDataStart == newData) && (NULL != statisticsDataStart->next)) {
                statisticsDataStart = statisticsDataStart->next; // move DataStart to next (first data block reused)
            }
        }

        *newData = *data;
        newData->next = NULL;

        if ((NULL != statisticsDataEnd) && (newData != statisticsDataEnd)) {
            statisticsDataEnd->next = newData; // link data block
        }
        statisticsDataEnd = newData; // set DataEnd to new data

        pthread_mutex_unlock(&statisticsDataLock);
    }

    return newData;
}

StatisticsNextNodePtr statisticData(StatisticsNodePtr nodeIn, StatisticsNodePtr dataOut)
{
    pthread_mutex_lock(&statisticsDataLock);

    if ((NULL == nodeIn) || (NULL == dataOut)) {
        pthread_mutex_unlock(&statisticsDataLock);
        return NULL;
    }

    StatisticsNextNodePtr nextNode = nodeIn->next;
    *dataOut = *nodeIn;

    pthread_mutex_unlock(&statisticsDataLock);

    return nextNode;
}

void clearStatisticData()
{
    if (NULL != statisticsDataStart) {
        pthread_mutex_lock(&statisticsDataLock);

        StatisticsNextNodePtr nextNode = statisticsDataStart;

        while (NULL != nextNode) {
            StatisticsNodePtr node = nextNode;
            nextNode = node->next;
            free(node);
        }

        statisticsDataStart = NULL;
        statisticsDataEnd = NULL;
        currentDataCount = 0;

        pthread_mutex_unlock(&statisticsDataLock);
    }
}

void statistics_init()
{
    STATISTICS_MODULE.statisticsList = &statisticsDataStart;
}

void statistics_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    struct StatisticsData statsData = {};
    TickType_t taskWakeTime = xTaskGetTickCount();

    while (1) {
        const int64_t currentTime = esp_timer_get_time() / 1000;
        statsFrequency = nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY, 0) * 1000;

        if (0 != statsFrequency) {
            const int64_t waitingTime = statsData.timestamp + statsFrequency - (DEFAULT_POLL_RATE / 2);

            if (currentTime > waitingTime) {
                int8_t wifiRSSI = -90;
                get_wifi_current_rssi(&wifiRSSI);

                statsData.timestamp = currentTime;
                statsData.hashrate = SYSTEM_MODULE.current_hashrate;
                statsData.chipTemperature = POWER_MANAGEMENT_MODULE.chip_temp_avg;
                statsData.vrTemperature = POWER_MANAGEMENT_MODULE.vr_temp;
                statsData.power = POWER_MANAGEMENT_MODULE.power;
                statsData.voltage = POWER_MANAGEMENT_MODULE.voltage;
                statsData.current = Power_get_current();
                statsData.coreVoltageActual = VCORE_get_voltage_mv();
                statsData.fanSpeed = POWER_MANAGEMENT_MODULE.fan_perc;
                statsData.fanRPM = POWER_MANAGEMENT_MODULE.fan_rpm;
                statsData.wifiRSSI = wifiRSSI;
                statsData.freeHeap = esp_get_free_heap_size();
                statsData.frequency = POWER_MANAGEMENT_MODULE.frequency_value;

                addStatisticData(&statsData);
            }
        } else {
            clearStatisticData();
        }

        vTaskDelayUntil(&taskWakeTime, DEFAULT_POLL_RATE / portTICK_PERIOD_MS); // taskWakeTime is automatically updated
    }
}
