#ifndef SCOREBOARD_H
#define SCOREBOARD_H

#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"

#define MAX_SCOREBOARD 20

typedef struct {
    double difficulty;
    char job_id[32];
    char extranonce2[32];
    uint32_t ntime;
    uint32_t nonce;
    uint32_t version_bits;
    char nvs_entry[128];
} ScoreboardEntry;

typedef struct {
    ScoreboardEntry entries[MAX_SCOREBOARD];
    int count;
    SemaphoreHandle_t mutex;
} Scoreboard;

void scoreboard_init(Scoreboard *scoreboard);
void scoreboard_add(Scoreboard *scoreboard, double difficulty, const char *job_id, const char *extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version_bits);

#endif /* SCOREBOARD_H */
