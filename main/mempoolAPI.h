#ifndef MEMPOOLAPI_H
#define MEMPOOLAPI_H

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "cJSON.h"
#include "esp_system.h"

typedef struct {
    uint32_t priceTimestamp;
    uint32_t priceUSD;
    bool priceValid;
    double networkHashrate;
    bool networkHashrateValid;
    double networkDifficulty;
    bool networkDifficultyValid;
    uint32_t blockHeight;
    bool blockHeightValid;
    double difficultyProgressPercent;
    bool difficultyProgressPercentValid;
    double difficultyChangePercent;
    bool difficultyChangePercentValid;
    uint32_t remainingBlocksToDifficultyAdjustment;
    bool remainingBlocksToDifficultyAdjustmentValid;
    uint32_t remainingTimeToDifficultyAdjustment;
    bool remainingTimeToDifficultyAdjustmentValid;
    uint32_t fastestFee;
    bool fastestFeeValid;
    uint32_t halfHourFee;
    bool halfHourFeeValid;
    uint32_t hourFee;
    bool hourFeeValid;
    uint32_t economyFee;
    bool economyFeeValid;
    uint32_t minimumFee;
    bool minimumFeeValid;
} MempoolApiState;

extern MempoolApiState MEMPOOL_STATE;  // Declare the global variable

esp_err_t mempool_api_price(void);
esp_err_t mempool_api_network_hashrate(void);
esp_err_t mempool_api_network_difficulty_adjustement(void);
esp_err_t mempool_api_block_tip_height(void);
esp_err_t mempool_api_network_recommended_fee(void);

MempoolApiState* getMempoolState(void);  // Add getter function

#endif