#ifndef STATISTICS_TASK_H_
#define STATISTICS_TASK_H_

typedef struct StatisticsData * StatisticsDataPtr;

struct StatisticsData
{
    uint32_t timestamp;
    float hashrate;
    float hashrate_1m;
    float hashrate_10m;
    float hashrate_1h;
    float errorPercentage;
    float chipTemperature;
    float vrTemperature;
    float power;
    float voltage;
    float current;
    int16_t coreVoltageActual;
    float fanSpeed;
    uint16_t fanRPM;
    uint16_t fan2RPM;
    int8_t wifiRSSI;
    uint32_t freeHeap;
    float responseTime;
};

bool getStatisticData(uint16_t index, StatisticsDataPtr dataOut);

void statistics_task(void * pvParameters);

#endif // STATISTICS_TASK_H_
