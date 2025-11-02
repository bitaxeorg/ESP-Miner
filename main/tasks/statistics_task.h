#ifndef STATISTICS_TASK_H_
#define STATISTICS_TASK_H_

typedef struct StatisticsData * StatisticsDataPtr;

struct StatisticsData
{
    uint32_t timestamp;
    float hashrate;
    uint32_t errorCount;
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
};

bool getStatisticData(uint16_t index, StatisticsDataPtr dataOut);

void statistics_task(void * pvParameters);

#endif // STATISTICS_TASK_H_
