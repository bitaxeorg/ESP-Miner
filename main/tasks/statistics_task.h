#ifndef STATISTICS_TASK_H_
#define STATISTICS_TASK_H_

#include <stdint.h>

typedef struct StatisticsData * StatisticsDataPtr;

struct StatisticsData
{
    uint64_t timestamp;
    float responseTime;
    float hashrate;
    float hashrate_1m;
    float hashrate_10m;
    float hashrate_1h;
    float errorPercentage;
    float chipTemperature;
    float chipTemperature2;
    float vrTemperature;
    float power;
    float voltage;
    float current;
    float fanSpeed;
    uint32_t freeHeap;
    int16_t coreVoltageActual;
    uint16_t fanRPM;
    uint16_t fan2RPM;
    int8_t wifiRSSI;
};

bool getStatisticData(uint16_t index, StatisticsDataPtr dataOut);

void statistics_task(void * pvParameters);

#endif // STATISTICS_TASK_H_
