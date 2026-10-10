#ifndef STATISTICS_TASK_H_
#define STATISTICS_TASK_H_

#include <stdbool.h>
#include <stdint.h>

#define MAX_STATISTICS_COUNT 720
#define STATS_TOKEN_LEN 12

typedef enum
{
    SRC_HASHRATE,
    SRC_HASHRATE_1m,
    SRC_HASHRATE_10m,
    SRC_HASHRATE_1h,
    SRC_ERROR_PERCENTAGE,
    SRC_ASIC_TEMP,
    SRC_ASIC_TEMP2,
    SRC_VR_TEMP,
    SRC_ASIC_VOLTAGE,
    SRC_VOLTAGE,
    SRC_POWER,
    SRC_CURRENT,
    SRC_FAN_SPEED,
    SRC_FAN_RPM,
    SRC_FAN2_RPM,
    SRC_WIFI_RSSI,
    SRC_FREE_HEAP,
    SRC_RESPONSE_TIME,
    SRC_NONE // last
} DataSource;

extern const char * const STATS_LABELS[SRC_NONE];

typedef struct StatisticsData * StatisticsDataPtr;

struct StatisticsData
{
    uint64_t timestamp;
    char tokens[SRC_NONE][STATS_TOKEN_LEN];
};

typedef void (*StatisticsReaderCallback)(const struct StatisticsData *rows, uint16_t count, void *user_ctx);

void withStatisticsData(StatisticsReaderCallback cb, void *user_ctx);

void statistics_task(void * pvParameters);

#endif // STATISTICS_TASK_H_
