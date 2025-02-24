#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"

#include "nvs_config.h"
#include "i2c_bitaxe.h"
#include "global_state.h"
#include "lvglDisplay.h"
#include "main.h"
#include "system.h"
#include "vcore.h"
#include "mempoolAPI.h"

#define lvglDisplayI2CAddr 0x50
#define DISPLAY_UPDATE_INTERVAL_MS 2500
#define MAX_BUFFER_SIZE 512  //Placeholder Buffer Size



/*  sent to the display
Network:
    - SSID Global_STATE->SYSTEM_MODULE.ssid
    - IP Address esp_ip4addr_ntoa(&ip_info.ip, ip_address_str, IP4ADDR_STRLEN_MAX);
    - Wifi Status GLOBAL_STATE->SYSTEM_MODULE.wifi_status
    - Pool URL Global_STATE->SYSTEM_MODULE.pool_url
    - Fallback Pool URL Global_STATE->SYSTEM_MODULE.fallback_pool_url
    - Pool Port Global_STATE->SYSTEM_MODULE.pool_port
    - Fallback Pool Port Global_STATE->SYSTEM_MODULE.fallback_pool_port

Mining:
    - Hashrate Global_STATE->SYSTEM_MODULE.current_hashrate
    - Efficiency Global_STATE->POWER_MANAGEMENT_MODULE.power / (module->current_hashrate / 1000.0)
    - Best Diff Global_STATE->SYSTEM_MODULE.best_diff_string
    - Best Session Diff Global_STATE->SYSTEM_MODULE.best_session_diff_string
    - Shares Accepted Global_STATE->SYSTEM_MODULE.shares_accepted
    - Shares Rejected Global_STATE->SYSTEM_MODULE.shares_rejected

Monitoring:
    - Asic Temp 1 Global_STATE->POWER_MANAGEMENT_MODULE.chip_temp[0]
    - Asic Temp 2 Global_STATE->POWER_MANAGEMENT_MODULE.chip_temp[1]
    - Asic Temp 3 Global_STATE->POWER_MANAGEMENT_MODULE.chip_temp[2]
    - Asic Temp 4 Global_STATE->POWER_MANAGEMENT_MODULE.chip_temp[3]
    - Asic Temp 5 Global_STATE->POWER_MANAGEMENT_MODULE.chip_temp[4]
    - Asic Temp 6 Global_STATE->POWER_MANAGEMENT_MODULE.chip_temp[5]
    - Asic Temp AVG Global_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg
    - Asic Frequency Global_STATE->POWER_MANAGEMENT_MODULE.frequency_value
    - Fan RPM Global_STATE->POWER_MANAGEMENT_MODULE.fan_rpm
    - Fan Percent Global_STATE->POWER_MANAGEMENT_MODULE.fan_perc
    - VR Temp Global_STATE->POWER_MANAGEMENT_MODULE.vr_temp
    - Power Global_STATE->POWER_MANAGEMENT_MODULE.power
    - Voltage Global_STATE->POWER_MANAGEMENT_MODULE.voltage
    - Current Global_STATE->POWER_MANAGEMENT_MODULE.current
    - ASIC Count Global_STATE->asic_count
    - Voltage Domain VCORE_get_voltage_mv(GLOBAL_STATE)
    - Uptime current time - GLOBAL_STATE->SYSTEM_MODULE.start_time
    

Device Status:

    
    - Found Block Global_STATE->SYSTEM_MODULE.FOUND_BLOCK
    - Startup Done Global_STATE->SYSTEM_MODULE.startup_done
    - Overheat Mode Global_STATE->SYSTEM_MODULE.overheat_mode
    - Last Clock Sync Global_STATE->SYSTEM_MODULE.lastClockSync
    - Device Model String Global_STATE->device_model_str
    - Board Version Global_STATE->board_version
    - ASIC Model String Global_STATE->asic_model_str
    
API Data:
    - BTC Price MEMPOOL_STATE.priceUSD
    - BTC Price Timestamp MEMPOOL_STATE.priceTimestamp
    - Block Height MEMPOOL_STATE.btcBlockHeight
    - Network Hashrate MEMPOOL_STATE.networkHashrate
    - Network Difficulty MEMPOOL_STATE.networkDifficulty
    - Network Fee MEMPOOL_STATE.networkFee
*/

static i2c_master_dev_handle_t lvglDisplay_dev_handle;
static TickType_t lastUpdateTime = 0;

static esp_err_t parseSettingsData(uint8_t* buffer, size_t length);


// Static Network Variables
static char lastSsid[32] = {0};
static char lastIpAddress[16] = {0};
static char lastWifiStatus[20] = {0};
static char lastPoolUrl[128] = {0};
static uint16_t lastPoolPort = 0;
static uint16_t lastFallbackPoolPort = 0;
static bool hasChanges = false;
static char lastFallbackUrl[128] = {0};

// Static Device Status Variables
static uint8_t lastFlags = 0;
static DeviceModel lastDeviceModel = DEVICE_UNKNOWN;
static AsicModel lastAsicModel = ASIC_UNKNOWN;
static int lastBoardVersion = 0;
static uint32_t lastClockSync = 0;
static char lastBoardInfo[128] = {0};

// Static buffers for all display data
static uint8_t displayBuffer[MAX_BUFFER_SIZE];
static uint8_t settingsBuffer[MAX_BUFFER_SIZE];
static float tempBuffer[8];  // For temperature data
static float powerBuffer[4]; // For power stats
static uint16_t infoBuffer[2]; // For ASIC info
static char boardInfo[128]; // For board info

static uint32_t lastPrice = 0;
static double lastNetworkHashrate = 0.0;
static double lastNetworkDifficulty = 0.0;
static uint32_t lastBlockHeight = 0;

static esp_err_t sendRegisterData(uint8_t reg, const void* data, size_t dataLen) 
{
    // Check total size including register byte and length byte
    if (dataLen + 2 > MAX_BUFFER_SIZE) {
        ESP_LOGE("LVGL", "Buffer overflow prevented: reg 0x%02X, size %d", reg, dataLen);
        return ESP_ERR_NO_MEM;
    }        
    
    // Clear only the portion of buffer we'll use
    memset(displayBuffer, 0, dataLen + 2);
    
    // Prepare data
    displayBuffer[0] = reg;
    displayBuffer[1] = (uint8_t)dataLen; // Explicitly cast size to uint8_t
    if (data != NULL && dataLen > 0) {
        memcpy(&displayBuffer[2], data, dataLen);
    }
    
    // Add debug logging
    ESP_LOGD("LVGL", "Sending reg 0x%02X, len %d", reg, dataLen);
    
    // Send data with exact length
    return i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, displayBuffer, dataLen + 2);
}

esp_err_t lvglDisplay_init(void) 
{
    lastUpdateTime = xTaskGetTickCount();
    return i2c_bitaxe_add_device(lvglDisplayI2CAddr, &lvglDisplay_dev_handle, "BAP");
}

static esp_err_t lvglReadRegisterData(uint8_t reg, void* data, size_t dataLen) 
{
    // Add delay before reading
    vTaskDelay(pdMS_TO_TICKS(10));  // Add 10ms delay
    
    // Add more detailed logging
    ESP_LOGI("LVGL", "Attempting to read register 0x%02X, length %d", reg, dataLen);
    
    if (dataLen + 2 > MAX_BUFFER_SIZE) {
        ESP_LOGE("LVGL", "Buffer overflow prevented: reg 0x%02X, size %d", reg, dataLen);
        return ESP_ERR_NO_MEM;
    }

    // Clear buffer before reading
    memset(settingsBuffer, 0, dataLen + 2);

    // Read into temporary buffer first
    esp_err_t ret = i2c_bitaxe_register_read(lvglDisplay_dev_handle, reg, settingsBuffer, dataLen + 2);
    if (ret != ESP_OK) {
        ESP_LOGE("LVGL", "Failed to read register 0x%02X: %d", reg, ret);
        return ret;
    }

    // Add debug logging for raw response
    ESP_LOGI("LVGL", "Raw response for reg 0x%02X: [%02X %02X %02X %02X]", 
        reg, settingsBuffer[0], settingsBuffer[1], 
        settingsBuffer[2], settingsBuffer[3]);

    // Verify register byte matches
    if (settingsBuffer[0] != reg) {
        ESP_LOGE("LVGL", "Register mismatch: expected 0x%02X, got 0x%02X", reg, settingsBuffer[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Verify length byte
    uint8_t actualLen = settingsBuffer[1];
    if (actualLen > dataLen) {
        ESP_LOGE("LVGL", "Received data length (%d) exceeds buffer size (%d)", actualLen, dataLen);
        return ESP_ERR_INVALID_SIZE;
    }

    // Copy actual data portion to output buffer, skipping register and length bytes
    if (data != NULL && actualLen > 0) {
        memcpy(data, &settingsBuffer[2], actualLen);
        // Ensure null termination for string data
        ((uint8_t*)data)[actualLen] = '\0';
        
        ESP_LOGD("LVGL", "Read reg 0x%02X: len=%d, first bytes: %02X %02X %02X %02X", 
            reg, actualLen,
            actualLen > 0 ? ((uint8_t*)data)[0] : 0,
            actualLen > 1 ? ((uint8_t*)data)[1] : 0,
            actualLen > 2 ? ((uint8_t*)data)[2] : 0,
            actualLen > 3 ? ((uint8_t*)data)[3] : 0);
    }

    return ESP_OK;
}

esp_err_t lvglUpdateDisplayNetwork(GlobalState *GLOBAL_STATE) 
{
    static TickType_t lastNetworkUpdateTime = 0;
    TickType_t currentNetworkTime = xTaskGetTickCount();
    if ((currentNetworkTime - lastNetworkUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS * 4)) // Doesn't need to be updated as often as mining data
    {
        return ESP_OK;
    }
    lastNetworkUpdateTime = currentNetworkTime;
    
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    esp_err_t ret;
    esp_netif_ip_info_t ip_info;
    char ip_address_str[IP4ADDR_STRLEN_MAX];
    
    // LVGL_REG_SSID (0x21)
    size_t dataLen = strlen(module->ssid);
    if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
        
    ret = sendRegisterData(LVGL_REG_SSID, module->ssid, dataLen);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_IP_ADDR (0x22)
    esp_netif_get_ip_info(netif, &ip_info);
    esp_ip4addr_ntoa(&ip_info.ip, ip_address_str, IP4ADDR_STRLEN_MAX);
    
    dataLen = strlen(ip_address_str);
    if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    ret = sendRegisterData(LVGL_REG_IP_ADDR, ip_address_str, dataLen);
    if (ret != ESP_OK) return ret;
        
    // LVGL_REG_WIFI_STATUS (0x23)
    dataLen = strlen(module->wifi_status);
    if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;

    ret = sendRegisterData(LVGL_REG_WIFI_STATUS, module->wifi_status, dataLen);
    if (ret != ESP_OK) return ret;


    // LVGL_REG_POOL_URL (0x24)
    const char *currentPoolUrl = module->pool_url;
    dataLen = strlen(currentPoolUrl);
    if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;

    ret = sendRegisterData(LVGL_REG_POOL_URL, currentPoolUrl, dataLen);
    if (ret != ESP_OK) return ret;


    // LVGL_REG_FALLBACK_URL (0x25)
    dataLen = strlen(module->fallback_pool_url);
    if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;

    ret = sendRegisterData(LVGL_REG_FALLBACK_URL, module->fallback_pool_url, dataLen);
    if (ret != ESP_OK) return ret;


    // LVGL_REG_POOL_PORTS (0x26)
    uint16_t ports[2] =
    {
        module->pool_port,
        module->fallback_pool_port
    };
    if (sizeof(uint16_t) * 2 + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    ret = sendRegisterData(LVGL_REG_POOL_PORTS, ports, sizeof(uint16_t) * 2);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t lvglUpdateDisplayMining(GlobalState *GLOBAL_STATE) 
{
    static TickType_t lastMiningUpdateTime = 0;
    TickType_t currentMiningTime = xTaskGetTickCount();
    if ((currentMiningTime - lastMiningUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS)) {
        return ESP_OK;
    }
    lastMiningUpdateTime = currentMiningTime;

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule *power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    esp_err_t ret;

    // LVGL_REG_HASHRATE (0x30)
    float hashrate = module->current_hashrate;
    ESP_LOGI("LVGL", "Sending hashrate: %.2f", hashrate);
    ESP_LOGI("LVGL", "Bytes: %02X %02X %02X %02X", 
        ((uint8_t*)&hashrate)[0],
        ((uint8_t*)&hashrate)[1],
        ((uint8_t*)&hashrate)[2],
        ((uint8_t*)&hashrate)[3]);

    ret = sendRegisterData(LVGL_REG_HASHRATE, &hashrate, sizeof(float));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_HIST_HASHRATE (0x31)
    // Add historical hashrate if available
    // TODO: Implement historical hashrate tracking

    // LVGL_REG_EFFICIENCY (0x32)
    if (sizeof(float) + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    float efficiency = 0.0f;
    if (module->current_hashrate > 0) {
        efficiency = power->power / (module->current_hashrate / 1000.0);
    }
    ret = sendRegisterData(LVGL_REG_EFFICIENCY, &efficiency, sizeof(float));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_BEST_DIFF (0x33)
    size_t bestDiffLen = strlen(module->best_diff_string);
    if (bestDiffLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    ret = sendRegisterData(LVGL_REG_BEST_DIFF, module->best_diff_string, bestDiffLen);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_SESSION_DIFF (0x34)
    size_t sessionDiffLen = strlen(module->best_session_diff_string);
    if (sessionDiffLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    ret = sendRegisterData(LVGL_REG_SESSION_DIFF, module->best_session_diff_string, sessionDiffLen);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_SHARES (0x35)
    uint32_t shares[2] = {
        module->shares_accepted,
        module->shares_rejected
    };
    ret = sendRegisterData(LVGL_REG_SHARES, shares, sizeof(uint32_t) * 2);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t lvglUpdateDisplayMonitoring(GlobalState *GLOBAL_STATE) 
{
    static TickType_t lastMonitorUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    
    if ((currentTime - lastMonitorUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS)) {
        return ESP_OK;
    }
    lastMonitorUpdateTime = currentTime;

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule *power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    esp_err_t ret;

    // LVGL_REG_TEMPS (0x40)
    // Send all chip temperatures and average
    size_t tempDataSize = (GLOBAL_STATE->asic_count * sizeof(float)) + sizeof(float);
    if (tempDataSize + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    float tempData[GLOBAL_STATE->asic_count + 1];
    for (int i = 0; i < GLOBAL_STATE->asic_count; i++) {
        tempData[i] = power->chip_temp[i];
    }
    tempData[GLOBAL_STATE->asic_count] = power->chip_temp_avg;
    
    ret = sendRegisterData(LVGL_REG_TEMPS, tempData, tempDataSize);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_ASIC_FREQ (0x41)
    if (sizeof(float) + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    ret = sendRegisterData(LVGL_REG_ASIC_FREQ, &power->frequency_value, sizeof(float)); 
    if (ret != ESP_OK) return ret;

    // LVGL_REG_FAN (0x42)
    if (sizeof(float) * 2 + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    float fanData[2] = 
    {
        (float)power->fan_rpm,   // Fan RPM
        (float)power->fan_perc   // Fan Percentage
    };
    ret = sendRegisterData(LVGL_REG_FAN, fanData, sizeof(float) * 2);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_POWER_STATS (0x43)
    float powerStats[4] = {
        power->voltage,    // Voltage
        power->current,    // Current
        power->power,      // Power
        VCORE_get_voltage_mv(GLOBAL_STATE)      // Voltage Domain
    };
    ret = sendRegisterData(LVGL_REG_POWER_STATS, powerStats, sizeof(float) * 4);
    ESP_LOGI("LVGL", "Sending power stats: %.2f %.2f %.2f %.2f", powerStats[0], powerStats[1], powerStats[2], powerStats[3]);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_ASIC_INFO (0x44)
    if (sizeof(uint16_t) * 2 + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    uint16_t asicInfo[2] = {
        GLOBAL_STATE->asic_count,      // ASIC Count
        //power->vr_temp  // VR Temperature
    };
    ret = sendRegisterData(LVGL_REG_ASIC_INFO, asicInfo, sizeof(uint16_t) * 2);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_UPTIME (0x45)
    if (sizeof(uint32_t) + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    uint32_t uptimeSeconds = ((esp_timer_get_time() - GLOBAL_STATE->SYSTEM_MODULE.start_time) / 1000000);
    ret = sendRegisterData(LVGL_REG_UPTIME, &uptimeSeconds, sizeof(uint32_t));
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t lvglUpdateDisplayDeviceStatus(GlobalState *GLOBAL_STATE) 
{
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    esp_err_t ret;
    bool hasChanges = false;

    // LVGL_REG_FLAGS (0x50)
    uint8_t flags = (module->FOUND_BLOCK ? 0x01 : 0) |
                    (module->startup_done ? 0x02 : 0) |
                    (module->overheat_mode ? 0x04 : 0);
                    
    if (lastFlags != flags) {
        ret = sendRegisterData(LVGL_REG_FLAGS, &flags, 1);
        if (ret != ESP_OK) return ret;
        
        lastFlags = flags;
        hasChanges = true;
    }

    // LVGL_REG_DEVICE_INFO (0x52)


    // LVGL_REG_BOARD_INFO (0x53)
    if (strcmp(lastBoardInfo, GLOBAL_STATE->device_model_str) != 0) 
    {
        size_t totalLen = snprintf(boardInfo, sizeof(boardInfo), "%s|%s", 
            GLOBAL_STATE->device_model_str, 
            GLOBAL_STATE->asic_model_str);

        ret = sendRegisterData(LVGL_REG_BOARD_INFO, boardInfo, totalLen);
        if (ret != ESP_OK) return ret;

        strncpy(lastBoardInfo, GLOBAL_STATE->device_model_str, sizeof(lastBoardInfo) - 1);
        lastBoardInfo[sizeof(lastBoardInfo) - 1] = '\0';
    }

    // LVGL_REG_CLOCK_SYNC (0x54)
    if (lastClockSync != module->lastClockSync) {
        if (sizeof(uint32_t) + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
        ret = sendRegisterData(LVGL_REG_CLOCK_SYNC, &module->lastClockSync, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;

        lastClockSync = module->lastClockSync;
        hasChanges = true;
    }

    return ESP_OK;
}

#if USE_MEMPOOL_API == 1
esp_err_t lvglUpdateDisplayAPI(void) 
{
    static TickType_t lastPriceUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    
    if ((currentTime - lastPriceUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS * 4)) {
        return ESP_OK;
    }
    lastPriceUpdateTime = currentTime;

    esp_err_t ret;
    MempoolApiState* mempoolState = getMempoolState();

    // Only send if we have valid price data
    if (mempoolState->priceValid) {
        // Send price
        ret = sendRegisterData(LVGL_REG_API_BTC_PRICE, &mempoolState->priceUSD, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent BTC price: %lu", mempoolState->priceUSD);

    }

    if (mempoolState->networkHashrateValid) {
        ret = sendRegisterData(LVGL_REG_API_NETWORK_HASHRATE, &mempoolState->networkHashrate, sizeof(double));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent network hashrate: %.2f", mempoolState->networkHashrate);
    }

    if (mempoolState->networkDifficultyValid) {
        ret = sendRegisterData(LVGL_REG_API_NETWORK_DIFFICULTY, &mempoolState->networkDifficulty, sizeof(double));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent network difficulty: %.2f", mempoolState->networkDifficulty);
    }

    if (mempoolState->blockHeightValid) {
        ret = sendRegisterData(LVGL_REG_API_BLOCK_HEIGHT, &mempoolState->blockHeight, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent block height: %lu", mempoolState->blockHeight);
    }

    if (mempoolState->difficultyProgressPercentValid) {
        ret = sendRegisterData(LVGL_REG_API_DIFFICULTY_PROGRESS, &mempoolState->difficultyProgressPercent, sizeof(double));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent difficulty progress: %.2f", mempoolState->difficultyProgressPercent);
    }

    if (mempoolState->difficultyChangePercentValid) {
        ret = sendRegisterData(LVGL_REG_API_DIFFICULTY_CHANGE, &mempoolState->difficultyChangePercent, sizeof(double));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent difficulty change: %.2f", mempoolState->difficultyChangePercent);
    }

    if (mempoolState->remainingBlocksToDifficultyAdjustmentValid) {
        ret = sendRegisterData(LVGL_REG_API_REMAINING_BLOCKS, &mempoolState->remainingBlocksToDifficultyAdjustment, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent remaining blocks: %lu", mempoolState->remainingBlocksToDifficultyAdjustment);
    }

    if (mempoolState->remainingTimeToDifficultyAdjustmentValid) {
        ret = sendRegisterData(LVGL_REG_API_REMAINING_TIME, &mempoolState->remainingTimeToDifficultyAdjustment, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent remaining time: %lu", mempoolState->remainingTimeToDifficultyAdjustment);
    }

    if (mempoolState->fastestFeeValid) {
        ret = sendRegisterData(LVGL_REG_API_FASTEST_FEE, &mempoolState->fastestFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent fastest fee: %lu", mempoolState->fastestFee);
    }

    if (mempoolState->halfHourFeeValid) {
        ret = sendRegisterData(LVGL_REG_API_HALF_HOUR_FEE, &mempoolState->halfHourFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent half hour fee: %lu", mempoolState->halfHourFee);
    }

    if (mempoolState->hourFeeValid) {
        ret = sendRegisterData(LVGL_REG_API_HOUR_FEE, &mempoolState->hourFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent hour fee: %lu", mempoolState->hourFee);
    }

    if (mempoolState->economyFeeValid) {
        ret = sendRegisterData(LVGL_REG_API_ECONOMY_FEE, &mempoolState->economyFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent economy fee: %lu", mempoolState->economyFee);
    }

    if (mempoolState->minimumFeeValid) {
        ret = sendRegisterData(LVGL_REG_API_MINIMUM_FEE, &mempoolState->minimumFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent minimum fee: %lu", mempoolState->minimumFee);
    }

    return ESP_OK;
}
#endif

esp_err_t lvglGetSettings(void)
 {
    static TickType_t lastSettingsUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    
    if ((currentTime - lastSettingsUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS * 4)) {
        return ESP_OK;
    }
    lastSettingsUpdateTime = currentTime;
    // read settings from i2c
    i2c_master_receive(lvglDisplay_dev_handle, settingsBuffer, sizeof(settingsBuffer), 50);

    // parse received data

    // no data received
    if (settingsBuffer[0] == 0) 
    {
        ESP_LOGI("LVGL", "No settings received");
        return ESP_OK;
    }

    // settings received
    else
    {
        ESP_LOGI("LVGL", "Settings received");
        // Print first 16 bytes in hex format
        ESP_LOGI("LVGL", "Settings (hex): %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            settingsBuffer[0], settingsBuffer[1], settingsBuffer[2], settingsBuffer[3],
            settingsBuffer[4], settingsBuffer[5], settingsBuffer[6], settingsBuffer[7],
            settingsBuffer[8], settingsBuffer[9], settingsBuffer[10], settingsBuffer[11],
            settingsBuffer[12], settingsBuffer[13], settingsBuffer[14], settingsBuffer[15]);

        return parseSettingsData(settingsBuffer, sizeof(settingsBuffer));
    }

    
}

static esp_err_t parseSettingsData(uint8_t* buffer, size_t length) {
    size_t pos = 0;
    esp_err_t ret = ESP_OK;

    while (pos + 4 < length) {  // Need at least 4 bytes for header + register + length
        // Check for preamble
        if (buffer[pos] != 0xFF || buffer[pos + 1] != 0xAA) {
            ESP_LOGE("LVGL", "Invalid preamble at position %d", pos);
            return ESP_ERR_INVALID_STATE;
        }

        uint8_t reg = buffer[pos + 2];
        uint8_t dataLen = buffer[pos + 3];

        // Check if we have enough data
        if (pos + 4 + dataLen > length) {
            ESP_LOGE("LVGL", "Buffer overflow prevented at register 0x%02X", reg);
            return ESP_ERR_INVALID_SIZE;
        }

        // Point to start of data
        uint8_t* data = &buffer[pos + 4];

        // Process based on register
        switch (reg) {
            case LVGL_REG_SETTINGS_HOSTNAME:
                nvs_config_set_string(NVS_CONFIG_HOSTNAME, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_WIFI_SSID:
                nvs_config_set_string(NVS_CONFIG_WIFI_SSID, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_WIFI_PASSWORD:
                nvs_config_set_string(NVS_CONFIG_WIFI_PASS, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_STRATUM_URL_MAIN:
                nvs_config_set_string(NVS_CONFIG_STRATUM_URL, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_STRATUM_PORT_MAIN:
                if (dataLen == 2) {
                    uint16_t port = (data[0] << 8) | data[1];
                    nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, port);
                }
                break;

            case LVGL_REG_SETTINGS_STRATUM_USER_MAIN:
                nvs_config_set_string(NVS_CONFIG_STRATUM_USER, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_STRATUM_PASSWORD_MAIN:
                nvs_config_set_string(NVS_CONFIG_STRATUM_PASS, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_STRATUM_URL_FALLBACK:
                nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_URL, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_STRATUM_PORT_FALLBACK:
                if (dataLen == 2) {
                    uint16_t port = (data[0] << 8) | data[1];
                    nvs_config_set_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, port);
                }
                break;

            case LVGL_REG_SETTINGS_STRATUM_USER_FALLBACK:
                nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_USER, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_STRATUM_PASSWORD_FALLBACK:
                nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_PASS, (const char*)data);
                break;

            case LVGL_REG_SETTINGS_ASIC_VOLTAGE:
                if (dataLen == 2) {
                    uint16_t voltage = (data[0] << 8) | data[1];
                    if (voltage >= 1000 && voltage <= 1250) {
                        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, voltage);
                    }
                }
                break;

            case LVGL_REG_SETTINGS_ASIC_FREQ:
                if (dataLen == 2) {
                    uint16_t freq = (data[0] << 8) | data[1];
                    if (freq >= 200 && freq <= 625) {
                        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, freq);
                    }
                }
                break;

            case LVGL_REG_SETTINGS_FAN_SPEED:
                if (dataLen == 2) {
                    uint16_t speed = (data[0] << 8) | data[1];
                    if (speed <= 100) {
                        nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, speed);
                    }
                }
                break;

            case LVGL_REG_SETTINGS_AUTO_FAN_SPEED:
                if (dataLen == 1) {
                    nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, data[0] ? 1 : 0);
                }
                break;

            default:
                ESP_LOGW("LVGL", "Unknown settings register: 0x%02X", reg);
                break;
        }

        // Move to next setting
        pos += 4 + dataLen;
    }

    return ret;
}
