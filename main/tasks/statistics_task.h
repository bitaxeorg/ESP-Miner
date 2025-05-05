#ifndef STATISTICS_TASK_H_
#define STATISTICS_TASK_H_

typedef struct StatisticsData * StatisticsNodePtr;
typedef struct StatisticsData * StatisticsNextNodePtr;

struct StatisticsData
{
    double hashrate;
    float temperature;
    float power;
    int64_t timestamp;

    StatisticsNextNodePtr next;
};

typedef struct
{
    StatisticsNodePtr * statisticsList;
} StatisticsModule;

StatisticsNodePtr addStatisticData(double hashrate, float temperature, float power, int64_t timestamp);
StatisticsNextNodePtr statisticData(StatisticsNodePtr node, double * hashrate, float * temperature, float * power, int64_t * timestamp);

void statistics_init(void * pvParameters);
void statistics_task(void * pvParameters);

#endif // STATISTICS_TASK_H_
