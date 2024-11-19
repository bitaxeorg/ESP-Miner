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


#define lvglDisplayI2CAddr 0x50
#define DISPLAY_UPDATE_INTERVAL_MS 5000

/* Order of the data sent to the display
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
    - Voltage Domain Global_STATE->voltage_domain
    - Uptime current time - GLOBAL_STATE->SYSTEM_MODULE.start_time
    

Device Status:

    
    - Found Block Global_STATE->SYSTEM_MODULE.FOUND_BLOCK
    - Startup Done Global_STATE->SYSTEM_MODULE.startup_done
    - Overheat Mode Global_STATE->SYSTEM_MODULE.overheat_mode
    - Last Clock Sync Global_STATE->SYSTEM_MODULE.lastClockSync
    - Device Model Global_STATE->device_model
    - Device Model String Global_STATE->device_model_str
    - Board Version Global_STATE->board_version
    - ASIC Model Global_STATE->asic_model
    - ASIC Model String Global_STATE->asic_model_str
    



*/

static i2c_master_dev_handle_t lvglDisplay_dev_handle;
static TickType_t lastUpdateTime = 0;


// Static Network Variables
static char lastSsid[32] = {0};
static char lastIpAddress[16] = {0};
static char lastWifiStatus[20] = {0};
static char lastPoolUrl[128] = {0};
static uint16_t lastPoolPort = 0;
static uint16_t lastFallbackPoolPort = 0;
static bool hasChanges = false;

// Static Device Status Variables
static uint8_t lastFlags = 0;
static DeviceModel lastDeviceModel = DEVICE_UNKNOWN;
static AsicModel lastAsicModel = ASIC_UNKNOWN;
static int lastBoardVersion = 0;
static uint32_t lastClockSync = 0;

esp_err_t lvglDisplay_init(void) {
    lastUpdateTime = xTaskGetTickCount();
    return i2c_bitaxe_add_device(lvglDisplayI2CAddr, &lvglDisplay_dev_handle);
}



esp_err_t lvglUpdateDisplayNetwork(GlobalState *GLOBAL_STATE) 
{
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    esp_err_t ret;
    esp_netif_ip_info_t ipInfo;
    char ipAddressStr[IP4ADDR_STRLEN_MAX];
    
    
    // Check SSID changes
    if (strcmp(lastSsid, module->ssid) != 0) {
        strncpy(lastSsid, module->ssid, sizeof(lastSsid) - 1);
        size_t dataLen = strlen(module->ssid);
        uint8_t *networkData = malloc(dataLen + 2); // +2 for register and length
        if (networkData == NULL) return ESP_ERR_NO_MEM;
        
        networkData[0] = LVGL_REG_SSID;
        networkData[1] = dataLen;
        memcpy(&networkData[2], module->ssid, dataLen);
        ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, networkData, dataLen + 2);
        free(networkData);
        if (ret != ESP_OK) return ret;
        hasChanges = true;
        lastSsid = module->ssid;
    }

    // Check IP Address changes
    if (esp_netif_get_ip_info(netif, &ipInfo) == ESP_OK) {
        esp_ip4addr_ntoa(&ipInfo.ip, ipAddressStr, sizeof(ipAddressStr));
        if (strcmp(lastIpAddress, ipAddressStr) != 0) {
            strncpy(lastIpAddress, ipAddressStr, sizeof(lastIpAddress) - 1);
            size_t dataLen = strlen(ipAddressStr);
            uint8_t *networkData = malloc(dataLen + 2);
            if (networkData == NULL) return ESP_ERR_NO_MEM;

            networkData[0] = LVGL_REG_IP_ADDR;
            networkData[1] = dataLen;
            memcpy(&networkData[2], ipAddressStr, dataLen);
            ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, networkData, dataLen + 2);
            free(networkData);
            if (ret != ESP_OK) return ret;
            hasChanges = true;
            lastIpAddress = ipAddressStr;
        }
    }

    // Check Pool URL changes
    const char *currentPoolUrl = module->is_using_fallback ? module->fallback_pool_url : module->pool_url;
    if (strcmp(lastPoolUrl, currentPoolUrl) != 0) {
        strncpy(lastPoolUrl, currentPoolUrl, sizeof(lastPoolUrl) - 1);
        size_t dataLen = strlen(currentPoolUrl);
        uint8_t *networkData = malloc(dataLen + 2);
        if (networkData == NULL) return ESP_ERR_NO_MEM;

        networkData[0] = LVGL_REG_POOL_URL;
        networkData[1] = dataLen;
        memcpy(&networkData[2], currentPoolUrl, dataLen);
        ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, networkData, dataLen + 2);
        free(networkData);
        if (ret != ESP_OK) return ret;
        hasChanges = true;
        lastPoolUrl = currentPoolUrl;
    }

    // Port changes can use stack allocation since size is fixed
    uint16_t currentPort = module->is_using_fallback ? module->fallback_pool_port : module->pool_port;
    if (lastPoolPort != currentPort || lastFallbackPoolPort != module->fallback_pool_port) {
        uint8_t networkData[6];  // Fixed size for ports
        networkData[0] = LVGL_REG_PORTS;
        networkData[1] = 4;  // Length for two ports
        networkData[2] = (currentPort >> 8) & 0xFF;
        networkData[3] = currentPort & 0xFF;
        networkData[4] = (module->fallback_pool_port >> 8) & 0xFF;
        networkData[5] = module->fallback_pool_port & 0xFF;
        if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, networkData, 6)) != ESP_OK)
            return ret;
        hasChanges = true;
        lastPoolPort = currentPort;
        lastFallbackPoolPort = module->fallback_pool_port;
    }

    // Check WiFi Status changes
    if (strcmp(lastWifiStatus, module->wifi_status) != 0) {
        strncpy(lastWifiStatus, module->wifi_status, sizeof(lastWifiStatus) - 1);
        size_t dataLen = strlen(module->wifi_status);
        uint8_t *networkData = malloc(dataLen + 2);
        if (networkData == NULL) return ESP_ERR_NO_MEM;

        networkData[0] = LVGL_REG_WIFI_STATUS;
        networkData[1] = dataLen;
        memcpy(&networkData[2], module->wifi_status, dataLen);
        ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, networkData, dataLen + 2);
        free(networkData);
        if (ret != ESP_OK) return ret;
        hasChanges = true;
        lastWifiStatus = module->wifi_status;
    }

    return ESP_OK;
}

esp_err_t lvglUpdateDisplayMining(GlobalState *GLOBAL_STATE) 
{
    TickType_t currentTime = xTaskGetTickCount();
    if ((currentTime - lastUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS)) {
        return ESP_OK;
    }
    lastUpdateTime = currentTime;

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule *power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    esp_err_t ret;

    // Allocate buffer for largest possible data
    size_t maxLen = strlen(module->best_diff_string);
    if (strlen(module->best_session_diff_string) > maxLen) {
        maxLen = strlen(module->best_session_diff_string);
    }
    uint8_t *miningData = malloc(maxLen + 2);
    if (miningData == NULL) return ESP_ERR_NO_MEM;

    // Send hashrate
    float hashrate = module->current_hashrate;
    miningData[0] = LVGL_REG_HASHRATE;
    miningData[1] = sizeof(float);
    memcpy(&miningData[2], &hashrate, sizeof(float));
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, miningData, sizeof(float) + 2)) != ESP_OK) {
        free(miningData);
        return ret;
    }

    // Send efficiency
    float efficiency = power->power / (hashrate / 1000.0);
    miningData[0] = LVGL_REG_EFFICIENCY;
    miningData[1] = sizeof(float);
    memcpy(&miningData[2], &efficiency, sizeof(float));
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, miningData, sizeof(float) + 2)) != ESP_OK) {
        free(miningData);
        return ret;
    }

    // Send shares (accepted and rejected in one packet)
    miningData[0] = LVGL_REG_SHARES;
    miningData[1] = 8;  // 2 uint32_t values
    uint32_t shares[2] = {module->shares_accepted, module->shares_rejected};
    memcpy(&miningData[2], shares, 8);
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, miningData, 10)) != ESP_OK) {
        free(miningData);
        return ret;
    }

    // Send best difficulty string
    size_t bestDiffLen = strlen(module->best_diff_string);
    miningData = realloc(miningData, bestDiffLen + 2);
    if (miningData == NULL) return ESP_ERR_NO_MEM;

    miningData[0] = LVGL_REG_BEST_DIFF;
    miningData[1] = bestDiffLen;
    memcpy(&miningData[2], module->best_diff_string, bestDiffLen);
    ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, miningData, bestDiffLen + 2);
    free(miningData);
    if (ret != ESP_OK) return ret;

    // Send session difficulty string
    size_t sessionDiffLen = strlen(module->best_session_diff_string);
    miningData = malloc(sessionDiffLen + 2);
    if (miningData == NULL) return ESP_ERR_NO_MEM;

    miningData[0] = LVGL_REG_SESSION_DIFF;
    miningData[1] = sessionDiffLen;
    memcpy(&miningData[2], module->best_session_diff_string, sessionDiffLen);
    ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, miningData, sessionDiffLen + 2);
    free(miningData);
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

    // Calculate uptime in seconds
    uint32_t uptimeSeconds = (esp_timer_get_time() - module->start_time) / 1000000;
    
    // Send uptime
    uint8_t *monitorData = malloc(sizeof(uint32_t) + 2);
    if (monitorData == NULL) return ESP_ERR_NO_MEM;
    
    monitorData[0] = LVGL_REG_UPTIME;
    monitorData[1] = sizeof(uint32_t);
    memcpy(&monitorData[2], &uptimeSeconds, sizeof(uint32_t));
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, monitorData, sizeof(uint32_t) + 2)) != ESP_OK) {
        ESP_LOGI("LVGL", "Sending monitor data type %d, length %d", monitorData[0], monitorData[1]);
        free(monitorData);
        return ret;
    }

    // Calculate temperature data size based on ASIC count
    size_t tempDataSize = (GLOBAL_STATE->asic_count * sizeof(float)) + sizeof(float); // ASICs + avg temp
    monitorData = realloc(monitorData, tempDataSize + 2); // +2 for register and length
    if (monitorData == NULL) return ESP_ERR_NO_MEM;

    // Send temperatures only for actual ASICs plus average
    monitorData[0] = LVGL_REG_TEMPS;
    monitorData[1] = tempDataSize;
    memcpy(&monitorData[2], power->chip_temp, GLOBAL_STATE->asic_count * sizeof(float));
    memcpy(&monitorData[2 + (GLOBAL_STATE->asic_count * sizeof(float))], &power->chip_temp_avg, sizeof(float));
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, monitorData, tempDataSize + 2)) != ESP_OK) {
        ESP_LOGI("LVGL", "Sending monitor data type %d, length %d", monitorData[0], monitorData[1]);
        free(monitorData);
        return ret;
    }
    
    // Send frequency
    monitorData[0] = LVGL_REG_ASIC_FREQ;
    monitorData[1] = sizeof(float);
    memcpy(&monitorData[2], &power->frequency_value, sizeof(float));
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, monitorData, sizeof(float) + 2)) != ESP_OK) {
        ESP_LOGI("LVGL", "Sending monitor data type %d, length %d", monitorData[0], monitorData[1]);
        free(monitorData);
        return ret;
    }

    // Send fan data
    monitorData[0] = LVGL_REG_FAN;
    monitorData[1] = sizeof(float) * 2;  // RPM and percentage
    float fanData[2] = {(float)power->fan_rpm, (float)power->fan_perc};
    memcpy(&monitorData[2], fanData, sizeof(float) * 2);
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, monitorData, (sizeof(float) * 2) + 2)) != ESP_OK) {
        ESP_LOGI("LVGL", "Sending monitor data type %d, length %d", monitorData[0], monitorData[1]);
        free(monitorData);
        return ret;
    }

    // Send power stats (voltage, current, power, vr_temp)
    monitorData[0] = LVGL_REG_POWER_STATS;
    monitorData[1] = sizeof(float) * 4;
    float powerStats[4] = {power->voltage, power->current, power->power, power->vr_temp};
    memcpy(&monitorData[2], powerStats, sizeof(float) * 4);
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, monitorData, (sizeof(float) * 4) + 2)) != ESP_OK) {
        ESP_LOGI("LVGL", "Sending monitor data type %d, length %d", monitorData[0], monitorData[1]);
        free(monitorData);
        return ret;
    }

    // Send ASIC info (count and voltage domain)
    monitorData[0] = LVGL_REG_ASIC_INFO;
    monitorData[1] = 4;  // Two uint16_t values
    uint16_t asicInfo[2] = {GLOBAL_STATE->asic_count, GLOBAL_STATE->voltage_domain};
    memcpy(&monitorData[2], asicInfo, 4);
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, monitorData, 6)) != ESP_OK) {
        ESP_LOGI("LVGL", "Sending monitor data type %d, length %d", monitorData[0], monitorData[1]);
        free(monitorData);
        return ret;
    }

    free(monitorData);
    return ESP_OK;
}

esp_err_t lvglUpdateDisplayDeviceStatus(GlobalState *GLOBAL_STATE) 
{
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    esp_err_t ret;
    bool hasChanges = false;

    // Check flag changes
    uint8_t flags = (module->FOUND_BLOCK ? 0x01 : 0) |
                    (module->startup_done ? 0x02 : 0) |
                    (module->overheat_mode ? 0x04 : 0);
                    
    if (lastFlags != flags) {
        uint8_t *statusData = malloc(2);
        if (statusData == NULL) return ESP_ERR_NO_MEM;

        statusData[0] = LVGL_REG_FLAGS;
        statusData[1] = 1;  // Length is 1 byte
        statusData[2] = flags;
        ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, statusData, 3);
        free(statusData);
        if (ret != ESP_OK) return ret;
        
        lastFlags = flags;
        hasChanges = true;
    }

    // Device info changes (device model and asic model)
    if (lastDeviceModel != GLOBAL_STATE->device_model ||
        lastAsicModel != GLOBAL_STATE->asic_model) {
        
        uint8_t deviceData[4];  // Fixed size for device info
        deviceData[0] = LVGL_REG_DEVICE_INFO;
        deviceData[1] = 2;  // Two bytes
        deviceData[2] = GLOBAL_STATE->device_model;
        deviceData[3] = GLOBAL_STATE->asic_model;
        
        ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, deviceData, 4);
        if (ret != ESP_OK) return ret;

        lastDeviceModel = GLOBAL_STATE->device_model;
        lastAsicModel = GLOBAL_STATE->asic_model;
        hasChanges = true;
    }

    // Board info changes
    if (lastBoardVersion != GLOBAL_STATE->board_version) {
        uint8_t boardData[4];  // Fixed size for board info
        boardData[0] = LVGL_REG_BOARD_INFO;
        boardData[1] = 2;  // Two bytes for board version
        boardData[2] = (GLOBAL_STATE->board_version >> 8) & 0xFF;
        boardData[3] = GLOBAL_STATE->board_version & 0xFF;
        
        ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, boardData, 4);
        if (ret != ESP_OK) return ret;

        lastBoardVersion = GLOBAL_STATE->board_version;
        hasChanges = true;
    }

    if (lastClockSync != module->lastClockSync) {
        uint8_t *clockData = malloc(sizeof(uint32_t) + 2);
        if (clockData == NULL) return ESP_ERR_NO_MEM;

        clockData[0] = LVGL_REG_CLOCK_SYNC;
        clockData[1] = sizeof(uint32_t);
        memcpy(&clockData[2], &module->lastClockSync, sizeof(uint32_t));
        ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, clockData, sizeof(uint32_t) + 2);
        free(clockData);
        if (ret != ESP_OK) return ret;

        lastClockSync = module->lastClockSync;
        hasChanges = true;
    }

    return ESP_OK;
}



