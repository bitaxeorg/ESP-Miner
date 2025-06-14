#ifndef STATISTICS_TASK_H_
#define STATISTICS_TASK_H_

typedef struct StatisticsData * StatisticsNodePtr;
typedef struct StatisticsData * StatisticsNextNodePtr;

struct StatisticsData
{
    int64_t timestamp;
    double hashrate;
    float chipTemperature;
    float vrTemperature;
    float power;
    float voltage;
    float current;
    int16_t coreVoltageActual;
    uint16_t fanSpeed;
    uint16_t fanRPM;
    int8_t wifiRSSI;
    uint32_t freeHeap;

    StatisticsNextNodePtr next;
};

typedef struct
{
    StatisticsNodePtr * statisticsList;
} StatisticsModule;

StatisticsNodePtr addStatisticData(StatisticsNodePtr data);
StatisticsNextNodePtr statisticData(StatisticsNodePtr nodeIn, StatisticsNodePtr dataOut);
void clearStatisticData();

void statistics_init(void * pvParameters);
void statistics_task(void * pvParameters);

#endif // STATISTICS_TASK_H_
