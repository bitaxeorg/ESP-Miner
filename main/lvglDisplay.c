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
#define DISPLAY_UPDATE_INTERVAL_MS 2500
#define MAX_BUFFER_SIZE 256  // Adjust size based on your largest needed buffer

// Add to the register definitions at the top
#define LVGL_REG_UPTIME         0x45  // Moved from 0x51 to 0x45

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
    - Voltage Domain Global_STATE->voltage_domain
    - Uptime current time - GLOBAL_STATE->SYSTEM_MODULE.start_time
    

Device Status:

    
    - Found Block Global_STATE->SYSTEM_MODULE.FOUND_BLOCK
    - Startup Done Global_STATE->SYSTEM_MODULE.startup_done
    - Overheat Mode Global_STATE->SYSTEM_MODULE.overheat_mode
    - Last Clock Sync Global_STATE->SYSTEM_MODULE.lastClockSync
    - Device Model String Global_STATE->device_model_str
    - Board Version Global_STATE->board_version
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
static float tempBuffer[8];  // For temperature data
static float powerBuffer[4]; // For power stats
static uint16_t infoBuffer[2]; // For ASIC info
static char boardInfo[128]; // For board info

static esp_err_t sendRegisterData(uint8_t reg, const void* data, size_t dataLen) {
    if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    
    // Clear entire buffer first
    memset(displayBuffer, 0, MAX_BUFFER_SIZE);
    
    // Prepare data
    displayBuffer[0] = reg;
    displayBuffer[1] = dataLen;
    if (data != NULL && dataLen > 0) {
        memcpy(&displayBuffer[2], data, dataLen);
    }
    
    // Send data
    return i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, displayBuffer, dataLen + 2);
}

esp_err_t lvglDisplay_init(void) {
    lastUpdateTime = xTaskGetTickCount();
    return i2c_bitaxe_add_device(lvglDisplayI2CAddr, &lvglDisplay_dev_handle);
}



esp_err_t lvglUpdateDisplayNetwork(GlobalState *GLOBAL_STATE) 
{
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    esp_err_t ret;
    esp_netif_ip_info_t ip_info;
    char ip_address_str[IP4ADDR_STRLEN_MAX];
    
    // LVGL_REG_SSID (0x21)
    if (strcmp(lastSsid, module->ssid) != 0) {
        size_t dataLen = strlen(module->ssid);
        if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
        
        ret = sendRegisterData(LVGL_REG_SSID, module->ssid, dataLen);
        if (ret != ESP_OK) return ret;
        
        strncpy(lastSsid, module->ssid, sizeof(lastSsid) - 1);
        lastSsid[sizeof(lastSsid) - 1] = '\0';
    }

    // LVGL_REG_IP_ADDR (0x22)
    esp_netif_get_ip_info(netif, &ip_info);
    esp_ip4addr_ntoa(&ip_info.ip, ip_address_str, IP4ADDR_STRLEN_MAX);
    
    if (strcmp(lastIpAddress, ip_address_str) != 0) {
        size_t dataLen = strlen(ip_address_str);
        ret = sendRegisterData(LVGL_REG_IP_ADDR, ip_address_str, dataLen);
        if (ret != ESP_OK) return ret;
        
        strncpy(lastIpAddress, ip_address_str, sizeof(lastIpAddress) - 1);
        lastIpAddress[sizeof(lastIpAddress) - 1] = '\0';
    }

    // LVGL_REG_WIFI_STATUS (0x23)
    if (strcmp(lastWifiStatus, module->wifi_status) != 0) {
        size_t dataLen = strlen(module->wifi_status);
        if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;

        ret = sendRegisterData(LVGL_REG_WIFI_STATUS, module->wifi_status, dataLen);
        if (ret != ESP_OK) return ret;
        
        strncpy(lastWifiStatus, module->wifi_status, sizeof(lastWifiStatus) - 1);
        lastWifiStatus[sizeof(lastWifiStatus) - 1] = '\0';
    }

    // LVGL_REG_POOL_URL (0x24)
    const char *currentPoolUrl = module->pool_url;
    if (strcmp(lastPoolUrl, currentPoolUrl) != 0) {
        size_t dataLen = strlen(currentPoolUrl);
        if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;

        ret = sendRegisterData(LVGL_REG_POOL_URL, currentPoolUrl, dataLen);
        if (ret != ESP_OK) return ret;
        
        strncpy(lastPoolUrl, currentPoolUrl, sizeof(lastPoolUrl) - 1);
        lastPoolUrl[sizeof(lastPoolUrl) - 1] = '\0';
    }

    // LVGL_REG_FALLBACK_URL (0x25)
    if (strcmp(lastFallbackUrl, module->fallback_pool_url) != 0) {
        size_t dataLen = strlen(module->fallback_pool_url);
        if (dataLen + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;

        ret = sendRegisterData(LVGL_REG_FALLBACK_URL, module->fallback_pool_url, dataLen);
        if (ret != ESP_OK) return ret;
        
        strncpy(lastFallbackUrl, module->fallback_pool_url, sizeof(lastFallbackUrl) - 1);
        lastFallbackUrl[sizeof(lastFallbackUrl) - 1] = '\0';
    }

    // LVGL_REG_POOL_PORTS (0x26)
    uint16_t ports[2] = {
        module->pool_port,
        module->fallback_pool_port
    };
    ret = sendRegisterData(LVGL_REG_POOL_PORTS, ports, sizeof(uint16_t) * 2);
    if (ret != ESP_OK) return ret;

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
        power->vr_temp     // VR Temperature
    };
    ret = sendRegisterData(LVGL_REG_POWER_STATS, powerStats, sizeof(float) * 4);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_ASIC_INFO (0x44)
    if (sizeof(uint16_t) * 2 + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    uint16_t asicInfo[2] = {
        GLOBAL_STATE->asic_count,      // ASIC Count
        GLOBAL_STATE->voltage_domain   // Voltage Domain
    };
    ret = sendRegisterData(LVGL_REG_ASIC_INFO, asicInfo, sizeof(uint16_t) * 2);
    if (ret != ESP_OK) return ret;

    // LVGL_REG_UPTIME (0x45)
    if (sizeof(uint32_t) + 2 > MAX_BUFFER_SIZE) return ESP_ERR_NO_MEM;
    uint32_t uptimeSeconds = (esp_timer_get_time() - module->start_time) / 1000000;
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



