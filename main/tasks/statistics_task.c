#include <stdint.h>
#include <pthread.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "statistics_task.h"
#include "global_state.h"
#include "nvs_config.h"

#define DEFAULT_POLL_RATE 5000

static const char * TAG = "statistics_task";

static StatisticsNodePtr statisticsDataStart = NULL;
static StatisticsNodePtr statisticsDataEnd = NULL;
static pthread_mutex_t statisticsDataLock = PTHREAD_MUTEX_INITIALIZER;

static uint16_t maxDataCount = 0;
static uint16_t currentDataCount = 0;
static uint16_t duration = 0;

StatisticsNodePtr addStatisticData(double hashrate, float temperature, float power, int64_t timestamp)
{
    if (0 == maxDataCount) {
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

        newData->hashrate = hashrate;
        newData->temperature = temperature;
        newData->power = power;
        newData->timestamp = timestamp;
        newData->next = NULL;

        if ((NULL != statisticsDataEnd) && (newData != statisticsDataEnd)) {
            statisticsDataEnd->next = newData; // link data block
        }
        statisticsDataEnd = newData; // set DataEnd to new data

        pthread_mutex_unlock(&statisticsDataLock);
    }

    return newData;
}

StatisticsNextNodePtr statisticData(StatisticsNodePtr node, double * hashrate, float * temperature, float * power, int64_t * timestamp)
{
    if ((NULL == node) || (0 == maxDataCount)) {
        return NULL;
    }

    StatisticsNextNodePtr nextNode = NULL;

    pthread_mutex_lock(&statisticsDataLock);

    if (NULL != hashrate) {
        *hashrate = node->hashrate;
    }
    if (NULL != temperature) {
        *temperature = node->temperature;
    }
    if (NULL != power) {
        *power = node->power;
    }
    if (NULL != timestamp) {
        *timestamp = node->timestamp;
    }
    nextNode = node->next;

    pthread_mutex_unlock(&statisticsDataLock);

    return nextNode;
}

void statistics_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    GLOBAL_STATE->STATISTICS_MODULE.statisticsList = &statisticsDataStart;
}

void statistics_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    maxDataCount = nvs_config_get_u16(NVS_CONFIG_STATISTICS_LIMIT, 0);
    if (720 < maxDataCount) {
        maxDataCount = 720;
        nvs_config_set_u16(NVS_CONFIG_STATISTICS_LIMIT, maxDataCount);
    }
    duration = nvs_config_get_u16(NVS_CONFIG_STATISTICS_DURATION, 1);
    if (720 < duration) {
        duration = 720;
        nvs_config_set_u16(NVS_CONFIG_STATISTICS_DURATION, duration);
    }
    if (1 > duration) {
        duration = 1;
        nvs_config_set_u16(NVS_CONFIG_STATISTICS_DURATION, duration);
    }

    const uint32_t pollRate = DEFAULT_POLL_RATE * duration;

    ESP_LOGI(TAG, "Ready!");

    while (1) {
        addStatisticData(sys_module->current_hashrate,
                         power_management->chip_temp_avg,
                         power_management->power,
                         esp_timer_get_time() / 1000);

        // looper:
        vTaskDelay(pollRate / portTICK_PERIOD_MS);
    }
}
