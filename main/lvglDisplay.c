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

static i2c_master_dev_handle_t lvglDisplay_dev_handle;
static TickType_t lastUpdateTime = 0;

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
