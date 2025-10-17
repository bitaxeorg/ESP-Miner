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

static StatisticsNodePtr statisticsBuffer = NULL;
static StatisticsNodePtr statisticsDataStart = NULL;
static StatisticsNodePtr statisticsDataEnd = NULL;
static pthread_mutex_t statisticsDataLock = PTHREAD_MUTEX_INITIALIZER;

static const uint16_t maxDataCount = 720;
static uint16_t statsFrequency;

StatisticsNodePtr addStatisticData(StatisticsNodePtr data)
{
    if ((NULL == data) || (0 == statsFrequency)) {
        return NULL;
    }

    createStatisticsBuffer();

    pthread_mutex_lock(&statisticsDataLock);

    if (NULL != statisticsBuffer) {
        statisticsDataEnd = statisticsDataEnd->next;

        if (statisticsDataStart == statisticsDataEnd) {
            statisticsDataStart = statisticsDataStart->next;
        } else if(NULL == statisticsDataStart) {
            statisticsDataStart = statisticsDataEnd;
        }

        StatisticsNextNodePtr next = statisticsDataEnd->next;
        *statisticsDataEnd = *data;
        statisticsDataEnd->next = next;
    }

    pthread_mutex_unlock(&statisticsDataLock);

    return statisticsDataEnd;
}

StatisticsNextNodePtr statisticData(StatisticsNodePtr nodeIn, StatisticsNodePtr dataOut)
{
    StatisticsNextNodePtr nextNode = NULL;

    pthread_mutex_lock(&statisticsDataLock);

    if ((NULL == nodeIn) || (NULL == dataOut)) {
        pthread_mutex_unlock(&statisticsDataLock);
        return NULL;
    }

    if (nodeIn != statisticsDataEnd) {
        nextNode = nodeIn->next;
    }

    *dataOut = *nodeIn;
    dataOut->next = nextNode;

    pthread_mutex_unlock(&statisticsDataLock);

    return nextNode;
}

void createStatisticsBuffer()
{
    if (NULL == statisticsBuffer) {
        pthread_mutex_lock(&statisticsDataLock);

        if (NULL == statisticsBuffer) {
            StatisticsNodePtr buffer = (StatisticsNodePtr)heap_caps_malloc(sizeof(struct StatisticsData) * maxDataCount, MALLOC_CAP_SPIRAM);
            if (NULL != buffer) {
                for (uint16_t i = 0; i < (maxDataCount - 1); i++) {
                    buffer[i].next = &buffer[i + 1];
                }
                buffer[maxDataCount - 1].next = &buffer[0];

                statisticsBuffer = buffer;
                statisticsDataStart = NULL;
                statisticsDataEnd = &buffer[maxDataCount - 1];
            } else {
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
            statisticsDataStart = NULL;
            statisticsDataEnd = NULL;
        }

        pthread_mutex_unlock(&statisticsDataLock);
    }
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
    HashrateMonitorModule * hashrate_monitor = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
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
                statsData.hashrate = sys_module->current_hashrate;
                statsData.hashrateRegister = hashrate_monitor->hashrate;
                statsData.errorCountRegister = hashrate_monitor->error_count;
                statsData.chipTemperature = power_management->chip_temp_avg;
                statsData.vrTemperature = power_management->vr_temp;
                statsData.power = power_management->power;
                statsData.voltage = power_management->voltage;
                statsData.current = Power_get_current(GLOBAL_STATE);
                statsData.coreVoltageActual = VCORE_get_voltage_mv(GLOBAL_STATE);
                statsData.fanSpeed = power_management->fan_perc;
                statsData.fanRPM = power_management->fan_rpm;
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
