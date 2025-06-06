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
#include "serial.h"
#include "utils.h"
#include "driver/uart.h"
#include "esp_system.h"


#include "nvs_config.h"
#include "i2c_bitaxe.h"
#include "global_state.h"
#include "lvglDisplay.h"
#include "lvglDisplayBAP.h"
#include "main.h"
#include "system.h"
#include "vcore.h"
#include "mempoolAPI.h"
#include "theme_api.h"
#include "power_management_task.h"
#include "global_state.h"
#include "theme_api.h"
#include "esp_ota_ops.h"

extern GlobalState *GLOBAL_STATE;

#define lvglDisplayI2CAddr 0x50
#define DISPLAY_UPDATE_INTERVAL_MS 2000
#define MAX_BUFFER_SIZE_BAP 1024  //Placeholder Buffer Size
#define SEND_DELAY_AFTER_RECEIVE_MS 1000  // 1 second delay


extern bool apply_preset(DeviceModel device_model, const char* preset_name);


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

static TickType_t lastUpdateTime = 0;

static uint8_t displayBufferBAP[MAX_BUFFER_SIZE_BAP];
static uint8_t settingsBufferBAP[MAX_BUFFER_SIZE_BAP];

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
static float tempBuffer[8];  // For temperature data
static float powerBuffer[4]; // For power stats
static uint16_t infoBuffer[2]; // For ASIC info
static char boardInfo[128]; // For board info

static uint32_t lastPrice = 0;
static double lastNetworkHashrate = 0.0;
static double lastNetworkDifficulty = 0.0;
static uint32_t lastBlockHeight = 0;

// static crc buffer
static uint8_t crcBufferRX[2];
static uint8_t crcBufferTX[2];

// Add at the top with other static variables
static bool is_receiving_data = false;
static TickType_t last_receive_time = 0;


/// @brief waits for a serial response to Match CRC
/// @param expectedCRC the expected crc 
/// @param timeout_ms number of ms to wait before timing out
/// @return true if CRC matches, false otherwise
static bool SERIAL_rx_BAP_CRC(uint16_t expectedCRC, uint16_t timeout_ms) {
    memset(crcBufferRX, 0, 2);
    int16_t CRCBytesRead = uart_read_bytes(UART_NUM_2, crcBufferRX, 2, timeout_ms / portTICK_PERIOD_MS);

    if (CRCBytesRead != 2) {
        ESP_LOGE("LVGL", "Failed to read CRC from device");
        return false;
    }
    
    uint16_t receivedCRC = (crcBufferRX[0] << 8) | crcBufferRX[1];
    
    if (receivedCRC == expectedCRC) {
        return true;
    }
    
    else if (crcBufferRX[0] != expectedCRC) {
        ESP_LOGE("LVGL", "CRC mismatch: expected 0x%04X, received 0x%04X", expectedCRC, crcBufferRX[0]);
        return false;
    }
    
    else
    {
        ESP_LOGE("LVGL", "Unknown error");
        return false;
    }
}
/// @brief Calculates the CRC16 of a given data
/// @param data The data to calculate the CRC16 of
/// @param length The length of the data
/// @return The CRC16 of the data
static uint16_t calculate_crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;  // Initial value
    
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}

static esp_err_t sendRegisterDataBAP(uint8_t reg, const void* data, size_t dataLen) 
{
    // Don't send if we're receiving data
    if (is_receiving_data) {
        ESP_LOGI("LVGL", "Skipping send during data reception");
        return ESP_OK;
    }

    // Check if we're within the delay period after receiving data
    TickType_t current_time = xTaskGetTickCount();
    if ((current_time - last_receive_time) < pdMS_TO_TICKS(SEND_DELAY_AFTER_RECEIVE_MS)) {
        ESP_LOGI("LVGL", "Skipping send during delay period after receive");
        return ESP_OK;
    }

    // Check total size including register byte and length byte
    if (dataLen + 4 > MAX_BUFFER_SIZE_BAP) {
        ESP_LOGE("LVGL", "Buffer overflow prevented: reg 0x%02X, size %d", reg, dataLen);
        return ESP_ERR_NO_MEM;
    }        
    
    // Clear only the portion of buffer we'll use
    //memset(displayBufferBAP, 0, dataLen + 2);
    
    // Prepare data

    memset(displayBufferBAP, 0, MAX_BUFFER_SIZE_BAP);
    displayBufferBAP[0] = 0xFF;
    displayBufferBAP[1] = 0xAA;
    displayBufferBAP[2] = reg;
    displayBufferBAP[3] = (uint8_t)dataLen; // Explicitly cast size to uint8_t
    if (data != NULL && dataLen > 0) {
        memcpy(&displayBufferBAP[4], data, dataLen);
    }
    
    // Calculate CRC16 for the entire message (preamble + reg + len + data)
    uint16_t crc = calculate_crc16(&displayBufferBAP[2], dataLen + 2);  // +4 for preamble (2) + reg (1) + len (1)
    
    // Append CRC to the message
    displayBufferBAP[dataLen + 4] = (crc >> 8) & 0xFF;    // High byte
    displayBufferBAP[dataLen + 5] = crc & 0xFF;           // Low byte
    
    // Send data with CRC
    ESP_LOGI("LVGL", "Sending reg 0x%02X, len %d, CRC: 0x%04X", reg, dataLen, crc);
    SERIAL_send_BAP(displayBufferBAP, dataLen + 6, false);
    // Wait for CRC feedback


    return ESP_OK;
}

esp_err_t lvglDisplay_initBAP(void) 
{
    lastUpdateTime = xTaskGetTickCount();

    // Test Serial
    SERIAL_init_BAP();
    /*
    //TODO: Add initialization handshake to detect if screen is connected
    //SERIAL_send_BAP("Hello", 5, false);

    if (SERIAL_rx_BAP(displayBufferBAP, MAX_BUFFER_SIZE_BAP, 1000) != 5)
    {
        ESP_LOGE("LVGL", "Failed to receive data from Serial");
        return ESP_FAIL;
        // TODO: Handle Trigger flag that will not send data if screen is not detected
    }

    ESP_LOGI("LVGL", "Received: %s", displayBufferBAP);
    */

    return ESP_OK;
}

static esp_err_t lvglReadRegisterDataBAP(uint8_t reg, void* data, size_t dataLen) 
{
 // Reading data when user changes 
 return ESP_OK;
}

esp_err_t lvglUpdateDisplayNetworkBAP(GlobalState *GLOBAL_STATE) 
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
    if (dataLen + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
        
    ret = sendRegisterDataBAP(LVGL_REG_SSID, module->ssid, dataLen);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_IP_ADDR (0x22)
    esp_netif_get_ip_info(netif, &ip_info);
    esp_ip4addr_ntoa(&ip_info.ip, ip_address_str, IP4ADDR_STRLEN_MAX);
    
    dataLen = strlen(ip_address_str);
    if (dataLen + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    ret = sendRegisterDataBAP(LVGL_REG_IP_ADDR, ip_address_str, dataLen);
    if (ret != ESP_OK) return ret;
        
    // LVGL_REG_WIFI_STATUS (0x23)
    dataLen = strlen(module->wifi_status);
    if (dataLen + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;

    ret = sendRegisterDataBAP(LVGL_REG_WIFI_STATUS, module->wifi_status, dataLen);
    if (ret != ESP_OK) return ret;


    // LVGL_REG_POOL_URL (0x24)
    const char *currentPoolUrl = module->pool_url;
    dataLen = strlen(currentPoolUrl);
    if (dataLen + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;

    ret = sendRegisterDataBAP(LVGL_REG_POOL_URL, currentPoolUrl, dataLen);
    if (ret != ESP_OK) return ret;


    // LVGL_REG_FALLBACK_URL (0x25)
    dataLen = strlen(module->fallback_pool_url);
    if (dataLen + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;

    ret = sendRegisterDataBAP(LVGL_REG_FALLBACK_URL, module->fallback_pool_url, dataLen);
    if (ret != ESP_OK) return ret;


    // LVGL_REG_POOL_PORTS (0x26)
    uint16_t ports[2] =
    {
        module->pool_port,
        module->fallback_pool_port
    };
    if (sizeof(uint16_t) * 2 + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    ret = sendRegisterDataBAP(LVGL_REG_POOL_PORTS, ports, sizeof(uint16_t) * 2);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

// TODO: Redo Function to send data in one call
esp_err_t lvglUpdateDisplayMiningBAP(GlobalState *GLOBAL_STATE) 
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

    ret = sendRegisterDataBAP(LVGL_REG_HASHRATE, &hashrate, sizeof(float));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_HIST_HASHRATE (0x31)
    // Add historical hashrate if available
    // TODO: Implement historical hashrate tracking

    // LVGL_REG_EFFICIENCY (0x32)
    if (sizeof(float) + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    float efficiency = 0.0f;
    if (module->current_hashrate > 0) {
        efficiency = power->power / (module->current_hashrate / 1000.0);
    }
    ret = sendRegisterDataBAP(LVGL_REG_EFFICIENCY, &efficiency, sizeof(float));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_BEST_DIFF (0x33)
    size_t bestDiffLen = strlen(module->best_diff_string);
    if (bestDiffLen + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    ret = sendRegisterDataBAP(LVGL_REG_BEST_DIFF, module->best_diff_string, bestDiffLen);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_SESSION_DIFF (0x34)
    size_t sessionDiffLen = strlen(module->best_session_diff_string);
    if (sessionDiffLen + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    ret = sendRegisterDataBAP(LVGL_REG_SESSION_DIFF, module->best_session_diff_string, sessionDiffLen);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_SHARES (0x35)
    uint32_t shares[2] = {
        module->shares_accepted,
        module->shares_rejected
    };
    ret = sendRegisterDataBAP(LVGL_REG_SHARES, shares, sizeof(uint32_t) * 2);
    if (ret != ESP_OK) return ret;

    

    return ESP_OK;
}

esp_err_t lvglUpdateDisplayMonitoringBAP(GlobalState *GLOBAL_STATE) 
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
    if (tempDataSize + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    float tempData[GLOBAL_STATE->asic_count + 1];
    for (int i = 0; i < GLOBAL_STATE->asic_count; i++) {
        tempData[i] = power->chip_temp[i];
    }
    tempData[GLOBAL_STATE->asic_count] = power->chip_temp_avg;
    
    ret = sendRegisterDataBAP(LVGL_REG_TEMPS, tempData, tempDataSize);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_ASIC_FREQ (0x41)
    if (sizeof(float) + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    ret = sendRegisterDataBAP(LVGL_REG_ASIC_FREQ, &power->frequency_value, sizeof(float)); 
    if (ret != ESP_OK) return ret;

    // LVGL_REG_FAN (0x42)
    if (sizeof(float) * 2 + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    float fanData[2] = 
    {
        (float)power->fan_rpm,   // Fan RPM
        (float)power->fan_perc   // Fan Percentage
    };
    ret = sendRegisterDataBAP(LVGL_REG_FAN, fanData, sizeof(float) * 2);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_POWER_STATS (0x43)
    float powerStats[4] = {
        power->voltage,    // Voltage
        power->current,    // Current
        power->power,      // Power
        VCORE_get_voltage_mv(GLOBAL_STATE)      // Voltage Domain
    };
    ret = sendRegisterDataBAP(LVGL_REG_POWER_STATS, powerStats, sizeof(float) * 4);
    ESP_LOGI("LVGL", "Sending power stats: %.2f %.2f %.2f %.2f", powerStats[0], powerStats[1], powerStats[2], powerStats[3]);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_ASIC_INFO (0x44)
    if (sizeof(uint16_t) * 2 + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    uint16_t asicInfo[2] = {
        GLOBAL_STATE->asic_count,      // ASIC Count
        //power->vr_temp  // VR Temperature
    };
    ret = sendRegisterDataBAP(LVGL_REG_ASIC_INFO, asicInfo, sizeof(uint16_t) * 2);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_UPTIME (0x45)
    if (sizeof(uint32_t) + 2 > MAX_BUFFER_SIZE_BAP) return ESP_ERR_NO_MEM;
    uint32_t uptimeSeconds = ((esp_timer_get_time() - GLOBAL_STATE->SYSTEM_MODULE.start_time) / 1000000);
    ret = sendRegisterDataBAP(LVGL_REG_UPTIME, &uptimeSeconds, sizeof(uint32_t));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_VREG_TEMP   0x46
    float vreg_temp = power->vr_temp;
    ret = sendRegisterDataBAP(LVGL_REG_VREG_TEMP, &vreg_temp, sizeof(float));
    if (ret != ESP_OK) return ret;


    return ESP_OK;
}

esp_err_t lvglUpdateDisplayDeviceStatusBAP(GlobalState *GLOBAL_STATE) 
{

    static TickType_t lastMonitorUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    

    if ((currentTime - lastMonitorUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS*4)) {

        return ESP_OK;
    }
    lastMonitorUpdateTime = currentTime;

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    esp_err_t ret;
    bool hasChanges = false;

    // LVGL_REG_DEVICE_SERIAL 0x70
    static char serialNumber[MAX_SERIAL_LENGTH] = {0};
    strncpy(serialNumber, nvs_config_get_string(NVS_CONFIG_SERIAL_NUMBER, ""), MAX_SERIAL_LENGTH);
    ret = sendRegisterDataBAP(LVGL_REG_DEVICE_SERIAL, serialNumber, strlen(serialNumber));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_BOARD_MODEL 0x71
    static char boardModel[MAX_MODEL_LENGTH] = {0};
    strncpy(boardModel, GLOBAL_STATE->asic_model_str, MAX_MODEL_LENGTH);
    ret = sendRegisterDataBAP(LVGL_REG_BOARD_MODEL, boardModel, strlen(boardModel));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_BOARD_FIRMWARE_VERSION 0x72
    static char firmwareVersion[MAX_FIRMWARE_VERSION_LENGTH] = {0};
    strncpy(firmwareVersion, esp_app_get_description()->version, MAX_FIRMWARE_VERSION_LENGTH);
    ret = sendRegisterDataBAP(LVGL_REG_BOARD_FIRMWARE_VERSION, firmwareVersion, strlen(firmwareVersion));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_THEME_CURRENT 0x73
    static char themeCurrent[MAX_THEME_LENGTH] = {0};
    themePreset_t currentTheme = getCurrentThemePreset();
    strncpy(themeCurrent, themePresetToString(currentTheme), MAX_THEME_LENGTH);
    ret = sendRegisterDataBAP(LVGL_REG_THEME_CURRENT, themeCurrent, strlen(themeCurrent));
    if (ret != ESP_OK) return ret;

    // LVGL_REG_THEMES_AVAILABLE 0x74

    //TODO: Add all themes to the available themes
    


    // New Flags 0x0E to

    if (GLOBAL_STATE->SYSTEM_MODULE.FOUND_BLOCK) {
        sendRegisterDataBAP(LVGL_FLAG_FOUND_BLOCK, &GLOBAL_STATE->SYSTEM_MODULE.FOUND_BLOCK, sizeof(uint8_t));
    }
    if (GLOBAL_STATE->SYSTEM_MODULE.overheat_mode) {
        sendRegisterDataBAP(LVGL_FLAG_OVERHEAT_MODE, &GLOBAL_STATE->SYSTEM_MODULE.overheat_mode, sizeof(uint8_t));
    }
    return ESP_OK;
}

#if USE_MEMPOOL_API == 1
esp_err_t lvglUpdateDisplayAPIBAP(void) 
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
        ret = sendRegisterDataBAP(LVGL_REG_API_BTC_PRICE, &mempoolState->priceUSD, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent BTC price: %lu", mempoolState->priceUSD);

    }

    if (mempoolState->networkHashrateValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_NETWORK_HASHRATE, &mempoolState->networkHashrate, sizeof(double));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent network hashrate: %.2f", mempoolState->networkHashrate);
    }

    if (mempoolState->networkDifficultyValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_NETWORK_DIFFICULTY, &mempoolState->networkDifficulty, sizeof(double));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent network difficulty: %.2f", mempoolState->networkDifficulty);
    }

    if (mempoolState->blockHeightValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_BLOCK_HEIGHT, &mempoolState->blockHeight, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent block height: %lu", mempoolState->blockHeight);
    }

    if (mempoolState->difficultyProgressPercentValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_DIFFICULTY_PROGRESS, &mempoolState->difficultyProgressPercent, sizeof(double));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent difficulty progress: %.2f", mempoolState->difficultyProgressPercent);
    }

    if (mempoolState->difficultyChangePercentValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_DIFFICULTY_CHANGE, &mempoolState->difficultyChangePercent, sizeof(double));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent difficulty change: %.2f", mempoolState->difficultyChangePercent);
    }

    if (mempoolState->remainingBlocksToDifficultyAdjustmentValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_REMAINING_BLOCKS, &mempoolState->remainingBlocksToDifficultyAdjustment, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent remaining blocks: %lu", mempoolState->remainingBlocksToDifficultyAdjustment);
    }

    if (mempoolState->remainingTimeToDifficultyAdjustmentValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_REMAINING_TIME, &mempoolState->remainingTimeToDifficultyAdjustment, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent remaining time: %lu", mempoolState->remainingTimeToDifficultyAdjustment);
    }

    if (mempoolState->fastestFeeValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_FASTEST_FEE, &mempoolState->fastestFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent fastest fee: %lu", mempoolState->fastestFee);
    }

    if (mempoolState->halfHourFeeValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_HALF_HOUR_FEE, &mempoolState->halfHourFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent half hour fee: %lu", mempoolState->halfHourFee);
    }

    if (mempoolState->hourFeeValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_HOUR_FEE, &mempoolState->hourFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent hour fee: %lu", mempoolState->hourFee);
    }

    if (mempoolState->economyFeeValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_ECONOMY_FEE, &mempoolState->economyFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent economy fee: %lu", mempoolState->economyFee);
    }

    if (mempoolState->minimumFeeValid) {
        ret = sendRegisterDataBAP(LVGL_REG_API_MINIMUM_FEE, &mempoolState->minimumFee, sizeof(uint32_t));
        if (ret != ESP_OK) return ret;
        ESP_LOGI("LVGL", "Sent minimum fee: %lu", mempoolState->minimumFee);
    }

    return ESP_OK;
}
#endif

/// @brief waits for a serial response from the device
/// @param buf buffer to read data into
/// @param size size of the buffer
/// @param timeout_ms number of ms to wait before timing out
/// @return number of bytes read, or -1 on error
int16_t SERIAL_rx_BAP(GlobalState *GLOBAL_STATE, uint8_t *buf, uint16_t size, uint16_t timeout_ms) {
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    memset(buf, 0, size);
    
    // Set receiving flag
    is_receiving_data = true;
    
    int16_t bytes_read = uart_read_bytes(UART_NUM_2, buf, size, timeout_ms / portTICK_PERIOD_MS);

    if (bytes_read > 0) {
        // Update last receive time
        last_receive_time = xTaskGetTickCount();
        

        ESP_LOGI("Serial BAP", "rx: ");
        prettyHex((unsigned char*) buf, bytes_read);
        ESP_LOGI("Serial BAP", " [%d]\n", bytes_read);

        // Minimum message size: preamble (2) + reg (1) + len (1) + data (1) + CRC (2) = 7 bytes
        if (bytes_read < 7) {
            ESP_LOGE("Serial BAP", "Message too short");
            is_receiving_data = false;  // Clear flag before returning
            return -1;
        }

        if (buf[0] == 0xFF && buf[1] == 0xAA) {
            uint8_t reg = buf[2];
            uint8_t data_len = buf[3];
            
            // Verify total message length
            if (bytes_read != data_len + 6) { // preamble + reg + len + data + CRC
                ESP_LOGE("Serial BAP", "Invalid message length");
                is_receiving_data = false;  // Clear flag before returning
                return -1;
            }
            
            // Calculate CRC16 of received message (excluding CRC bytes)
            uint16_t calculated_crc = calculate_crc16(&buf[2], bytes_read - 4);  // Start after preamble, exclude CRC bytes
            crcBufferTX[0] = (calculated_crc >> 8) & 0xFF;
            crcBufferTX[1] = calculated_crc & 0xFF;

            
            
            if (calculated_crc != ((buf[bytes_read - 2] << 8) | (buf[bytes_read - 1]))) {
                ESP_LOGE("Serial BAP", "CRC mismatch: received 0x%04X, calculated 0x%04X", 
                         (buf[bytes_read - 2] << 8) | (buf[bytes_read - 1]), calculated_crc);
                SERIAL_send_BAP(crcBufferTX, 2, false);
                is_receiving_data = false;  // Clear flag before returning
                return -1;
            }
            
            // Process the message based on register
            if (data_len == bytes_read - 6) {
                switch (buf[2]) {
                    case LVGL_REG_SETTINGS_HOSTNAME:
                    {
                        ESP_LOGI("Serial BAP", "Received hostname");
                        char hostname[64] = {0};  // Create temporary buffer
                        strncpy(hostname, (const char *)(buf + 4), data_len);
                        hostname[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "Hostname: %s", hostname);
                        nvs_config_set_string(NVS_CONFIG_HOSTNAME, hostname);
                        break;
                    }
                case LVGL_REG_SETTINGS_WIFI_SSID:
                    {
                        ESP_LOGI("Serial BAP", "Received wifi ssid");
                        char ssid[64] = {0};  // Create temporary buffer
                        strncpy(ssid, (const char *)(buf + 4), data_len);
                        ssid[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "SSID: %s", ssid);
                        nvs_config_set_string(NVS_CONFIG_WIFI_SSID, ssid);
                        break;
                    }
                case LVGL_REG_SETTINGS_WIFI_PASSWORD:
                    {
                        ESP_LOGI("Serial BAP", "Received wifi password");
                        char password[64] = {0};  // Create temporary buffer
                        strncpy(password, (const char *)(buf + 4), data_len);
                        password[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "Password: %s", password);
                        nvs_config_set_string(NVS_CONFIG_WIFI_PASS, password);
                        break;
                    }
                case LVGL_REG_SETTINGS_STRATUM_URL_MAIN:
                    {
                        ESP_LOGI("Serial BAP", "Received stratum url main");
                        char url[128] = {0};  // Create temporary buffer
                        strncpy(url, (const char *)(buf + 4), data_len);
                        url[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "URL: %s", url);
                        nvs_config_set_string(NVS_CONFIG_STRATUM_URL, url);
                        break;
                    }
                case LVGL_REG_SETTINGS_STRATUM_PORT_MAIN:
                    ESP_LOGI("Serial BAP", "Received stratum port main");
                    ESP_LOGI("Serial BAP", "Port: %d", buf[4] * 256 + buf[5]);
                    nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, buf[4] * 256 + buf[5]);
                    break;
                case LVGL_REG_SETTINGS_STRATUM_USER_MAIN:
                    {
                        ESP_LOGI("Serial BAP", "Received stratum user main");
                        char user[64] = {0};  // Create temporary buffer
                        strncpy(user, (const char *)(buf + 4), data_len);
                        user[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "User: %s", user);
                        nvs_config_set_string(NVS_CONFIG_STRATUM_USER, user);
                        break;
                    }
                case LVGL_REG_SETTINGS_STRATUM_PASSWORD_MAIN:
                    {
                        ESP_LOGI("Serial BAP", "Received stratum password main");
                        char password[64] = {0};  // Create temporary buffer
                        strncpy(password, (const char *)(buf + 4), data_len);
                        password[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "Password: %s", password);
                        nvs_config_set_string(NVS_CONFIG_STRATUM_PASS, password);
                        break;
                    }
                case LVGL_REG_SETTINGS_STRATUM_URL_FALLBACK:
                    {
                        ESP_LOGI("Serial BAP", "Received stratum url fallback");
                        char url[128] = {0};  // Create temporary buffer
                        strncpy(url, (const char *)(buf + 4), data_len);
                        url[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "URL: %s", url);
                        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_URL, url);
                        break;
                    }
                case LVGL_REG_SETTINGS_STRATUM_PORT_FALLBACK:
                    ESP_LOGI("Serial BAP", "Received stratum port fallback");
                    ESP_LOGI("Serial BAP", "Port: %d", buf[4] * 256 + buf[5]);
                    nvs_config_set_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, buf[4] * 256 + buf[5]);
                    break;
                case LVGL_REG_SETTINGS_STRATUM_USER_FALLBACK:
                    {
                        ESP_LOGI("Serial BAP", "Received stratum user fallback");
                        char user[64] = {0};  // Create temporary buffer
                        strncpy(user, (const char *)(buf + 4), data_len);
                        user[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "User: %s", user);
                        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_USER, user);
                        break;
                    }
                case LVGL_REG_SETTINGS_STRATUM_PASSWORD_FALLBACK:
                    {
                        ESP_LOGI("Serial BAP", "Received stratum password fallback");
                        char password[64] = {0};  // Create temporary buffer
                        strncpy(password, (const char *)(buf + 4), data_len);
                        password[data_len] = '\0';  // Ensure null termination
                        ESP_LOGI("Serial BAP", "Password: %s", password);
                        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_PASS, password);
                        break;
                    }
                case LVGL_REG_SETTINGS_ASIC_VOLTAGE:
                    ESP_LOGI("Serial BAP", "Received asic voltage");
                    ESP_LOGI("Serial BAP", "Voltage: %d", buf[4] * 256 + buf[5]);
                    uint16_t voltage = buf[4] * 256 + buf[5];
                    if (voltage <= 1500 && voltage >= 800) {
                        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, voltage);
                        ESP_LOGI("Serial BAP", "Setting ASIC voltage to %d", voltage);
                    }
                    else 
                    {
                        ESP_LOGE("Serial BAP", "Invalid voltage: %d", voltage);
                    }
                    break;
                case LVGL_REG_SETTINGS_ASIC_FREQ:
                    ESP_LOGI("Serial BAP", "Received asic frequency");
                    ESP_LOGI("Serial BAP", "Frequency: %d", buf[4] * 256 + buf[5]);
                    uint16_t frequency = buf[4] * 256 + buf[5];
                    if (frequency <= 1000 && frequency >= 200) {
                        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, frequency);
                        ESP_LOGI("Serial BAP", "Setting ASIC frequency to %d", frequency);
                    }
                    else 
                    {
                        ESP_LOGE("Serial BAP", "Invalid frequency: %d", frequency);
                    }
                    break;
                case LVGL_REG_SETTINGS_FAN_SPEED:
                    ESP_LOGI("Serial BAP", "Received fan speed");
                    ESP_LOGI("Serial BAP", "Speed: %d", buf[5]);    
                    uint16_t fan_speed = buf[5];
                    if (fan_speed <= 100 && fan_speed >= 0) {
                        nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, fan_speed);
                        ESP_LOGI("Serial BAP", "Setting fan speed to %d", fan_speed);
                    }
                    break;
                case LVGL_REG_SETTINGS_AUTO_FAN_SPEED:
                    ESP_LOGI("Serial BAP", "Received auto fan enable flag");
                    ESP_LOGI("Serial BAP", "RAW HEX: %02X", buf[5]);
                    uint16_t auto_fan_enabled = 0x0000 + buf[5];  // Convert to bool (0 or 1)
                    ESP_LOGI("Serial BAP", "Auto fan enabled: %d", auto_fan_enabled);
                    nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, auto_fan_enabled ? 1 : 0);
                    break;
                case LVGL_REG_SPECIAL_THEME:
                    ESP_LOGI("Serial BAP", "Received theme");
                    ESP_LOGI("Serial BAP", "RAW HEX: %02X", buf[4]);
                    uint16_t theme = 0x0000 + buf[4];
                    ESP_LOGI("Serial BAP", "Theme: %d", theme);
                    nvs_config_set_u16(NVS_CONFIG_THEME_NAME, theme);
                    initializeTheme(theme);
                    break;
                case LVGL_REG_SPECIAL_PRESET:
                    ESP_LOGI("Serial BAP", "Received preset");
                    ESP_LOGI("Serial BAP", "RAW HEX: %02X", buf[4]);
                    char preset[32] = {0};  // Initialize buffer for preset string
                    strncpy(preset, (const char *)(buf + 4), data_len);  // Copy the string data
                    preset[data_len] = '\0';  // Ensure null termination
                    ESP_LOGI("Serial BAP", "Preset: %s", preset);
                    if (apply_preset(GLOBAL_STATE->device_model, preset)) {
                        ESP_LOGI("Serial BAP", "Preset applied successfully");
                    } else {
                        ESP_LOGE("Serial BAP", "Failed to apply preset");
                    }
                    break;
                case LVGL_REG_SPECIAL_RESTART:
                    ESP_LOGI("Serial BAP", "Received restart command");
                    // send crc before restart
                    SERIAL_send_BAP(crcBufferTX, 2, false);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_restart();
                    break;
                case LVGL_FLAG_OVERHEAT_MODE:
                    ESP_LOGI("Serial BAP", "Received overheat mode flag");
                    ESP_LOGI("Serial BAP", "RAW HEX: %02X", buf[4]);
                    uint16_t flag_overheat_mode = 0x0000 + buf[4];
                    ESP_LOGI("Serial BAP", "Overheat mode: %d", flag_overheat_mode);
                    nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, flag_overheat_mode);
                    break;
                // current theme
                case LVGL_REG_THEME_CURRENT:
                    ESP_LOGI("Serial BAP", "Received current theme");
                    char themeName[32] = {0};  // Create temporary buffer
                    strncpy(themeName, (const char *)(buf + 4), data_len);
                    themeName[data_len] = '\0';  // Ensure null termination
                    ESP_LOGI("Serial BAP", "Theme: %s", themeName);
                    theme = themePresetFromString(themeName);
                    initializeTheme(theme);
                    break;
                default:
                        ESP_LOGI("Serial BAP", "Received unknown register");
                        ESP_LOGI("Serial BAP", "RAW HEX: %02X", buf[2]);
                        break;
                }
                SERIAL_send_BAP(crcBufferTX, 2, false);
            }
            else {
                ESP_LOGI("Serial BAP", "Received invalid length");           
                ESP_LOGI("Serial BAP", "Expected: %d, Received: %d", buf[3], bytes_read - 4);
            }
        }
    }
    
    // Clear receiving flag
    is_receiving_data = false;

    return bytes_read;
}

esp_err_t lvglStartupLoopBAP(GlobalState *GLOBAL_STATE) 
{

    static TickType_t lastMonitorUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    
    if ((currentTime - lastMonitorUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS)) 
    {
        return ESP_OK;
    }
    lastMonitorUpdateTime = currentTime;

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    if (!module->startup_done) 
    {
        ESP_LOGI("LVGL", "Sending startup done flag false");
        sendRegisterDataBAP(LVGL_FLAG_STARTUP_DONE, &module->startup_done, sizeof(uint8_t));
        sendRegisterDataBAP(LVGL_REG_IP_ADDR, "", 0);

    }

    return ESP_OK;
}

esp_err_t lvglOverheatLoopBAP(GlobalState *GLOBAL_STATE) {
    static TickType_t lastMonitorUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    
    if ((currentTime - lastMonitorUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS)) 
    {
        return ESP_OK;
    }
    lastMonitorUpdateTime = currentTime;

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    if (module->overheat_mode) 
    {
        ESP_LOGI("LVGL", "Sending overheat mode flag true");
        sendRegisterDataBAP(LVGL_FLAG_OVERHEAT_MODE, &module->overheat_mode, sizeof(uint8_t));
        sendRegisterDataBAP(LVGL_REG_IP_ADDR, "", 0);

    }

    return ESP_OK;
}
// send theme to BAP
esp_err_t lvglSendThemeBAP(char themeName[32]) {
    sendRegisterDataBAP(LVGL_REG_SPECIAL_THEME, themeName, strlen(themeName));
    ESP_LOGI("LVGL", "Sent theme: %s", themeName);
    return ESP_OK;
}

// send preset to BAP
esp_err_t lvglSendPresetBAP() {
    char preset[32] = {0};
    const char* preset_str = nvs_config_get_string(NVS_CONFIG_AUTOTUNE_PRESET, "");
    ESP_LOGI("LVGL", "Getting preset from NVS: %s", preset_str);
    
    strncpy(preset, preset_str, sizeof(preset) - 1);
    preset[sizeof(preset) - 1] = '\0';  // Ensure null termination
    
    size_t preset_len = strlen(preset);
    ESP_LOGI("LVGL", "Sending preset length: %d", preset_len);
    ESP_LOGI("LVGL", "Sending preset: %s", preset);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // clear receiving flag due to bug TODO: Why is this happening when the same sendRegisterDataBAP is used for other registers with no issues?
    is_receiving_data = false;
    esp_err_t ret = sendRegisterDataBAP(LVGL_REG_SPECIAL_PRESET, preset, preset_len);
    if (ret != ESP_OK) {
        ESP_LOGE("LVGL", "Failed to send preset");
        return ret;
    }
    
    return ESP_OK;
}


