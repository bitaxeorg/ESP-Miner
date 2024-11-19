#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_config.h"
#include "i2c_bitaxe.h"
#include "global_state.h"

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
    - Historical Hashrate Global_STATE->SYSTEM_MODULE.historical_hashrate_rolling_index
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

Device Status:

    - Uptime current time - GLOBAL_STATE->SYSTEM_MODULE.start_time
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

esp_err_t lvglDisplay_init(void) {
    lastUpdateTime = xTaskGetTickCount();
    return i2c_bitaxe_add_device(lvglDisplayI2CAddr, &lvglDisplay_dev_handle);
}

esp_err_t lvglUpdateDisplayValues(GlobalState *GLOBAL_STATE) 
{
    TickType_t currentTime = xTaskGetTickCount();
    
    // Only update if 5 seconds have passed
    if ((currentTime - lastUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS)) {
        return ESP_OK;
    }
    
    lastUpdateTime = currentTime;
    
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule *power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    uint8_t statusData[8];
    esp_err_t ret;
    
    // Pack hashrate (as fixed point, multiply by 100 to preserve 2 decimal places)
    uint32_t hashrateFixed = (uint32_t)(module->current_hashrate * 100);
    statusData[0] = (hashrateFixed >> 24) & 0xFF;
    statusData[1] = (hashrateFixed >> 16) & 0xFF;
    statusData[2] = (hashrateFixed >> 8) & 0xFF;
    statusData[3] = hashrateFixed & 0xFF;
    
    // Pack temperature (as fixed point, multiply by 10 to preserve 1 decimal place)
    uint16_t tempFixed = (uint16_t)(power->chip_temp_avg * 10);
    statusData[4] = (tempFixed >> 8) & 0xFF;
    statusData[5] = tempFixed & 0xFF;
    
    // Calculate efficiency (J/Th)
    float efficiency = power->power / (module->current_hashrate / 1000.0);
    uint16_t efficiencyFixed = (uint16_t)(efficiency * 10);
    statusData[6] = (efficiencyFixed >> 8) & 0xFF;
    statusData[7] = efficiencyFixed & 0xFF;
    
    // Send hashrate
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, statusData, 4)) != ESP_OK)
        return ret;
    
    // Send temperature and efficiency
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, &statusData[4], 4)) != ESP_OK)
        return ret;
    
    // Send shares count
    uint32_t shares = module->shares_accepted;
    statusData[0] = (shares >> 24) & 0xFF;
    statusData[1] = (shares >> 16) & 0xFF;
    statusData[2] = (shares >> 8) & 0xFF;
    statusData[3] = shares & 0xFF;
    
    return i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, statusData, 4);
}

esp_err_t lvglUpdateDisplayNetwork(GlobalState *GLOBAL_STATE) 
{
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    esp_err_t ret;
    esp_netif_ip_info_t ipInfo;
    char ipAddressStr[IP4ADDR_STRLEN_MAX];
    bool hasChanges = false;
    
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
    }

    // Check IP Address changes
    if (esp_netif_get_ip_info(module->netif, &ipInfo) == ESP_OK) {
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
    }

    // Port changes can use stack allocation since size is fixed
    uint16_t currentPort = module->is_using_fallback ? module->fallback_pool_port : module->pool_port;
    if (lastPoolPort != currentPort || lastFallbackPoolPort != module->fallback_pool_port) {
        lastPoolPort = currentPort;
        lastFallbackPoolPort = module->fallback_pool_port;
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
    }

    return ESP_OK;
}

