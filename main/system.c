#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "driver/i2c.h"
#include "driver/gpio.h"

#include "led_controller.h"
#include "DS4432U.h"
#include "EMC2101.h"
#include "INA260.h"
#include "adc.h"
#include "oled.h"
#include <sys/time.h>
#include "system.h"
#include "math.h"


static const char *TAG = "SystemModule";

#define BM1397_VOLTAGE CONFIG_BM1397_VOLTAGE


static void _init_system(SystemModule* module) {

    module->duration_start = 0;
    module->historical_hashrate_rolling_index = 0;
    module->historical_hashrate_init = 0;
    module->current_hashrate = 0;
    module->screen_page = 0;
    module->shares_accepted = 0;
    module->shares_rejected = 0;
    module->best_nonce_diff = 0;
    module->start_time = time(NULL);

    //test the LEDs
    // ESP_LOGI(TAG, "Init LEDs!");
    // ledc_init();
    // led_set();

    //Playing with BI level
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_10, 0);

    //Init I2C
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    ADC_init();

    //DS4432U tests
    DS4432U_set_vcore(BM1397_VOLTAGE / 1000.0);
    
    //Fan Tests
    EMC2101_init();
    EMC2101_set_fan_speed(0.75);
    vTaskDelay(500 / portTICK_RATE_MS);

    //oled
    if (!OLED_init()) {
        ESP_LOGI(TAG, "OLED init failed!");
    } else {
        ESP_LOGI(TAG, "OLED init success!");
        //clear the oled screen
        OLED_fill(0);
    }


}

static void _update_hashrate(SystemModule* module){


        if(module->screen_page != 0){
            return;
        }

        float power = INA260_read_power() / 1000;

        float efficiency = power / (module->current_hashrate/1000.0);

        OLED_clearLine(0);
        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "Gh%s: %.1f W/Th: %.1f", module->historical_hashrate_init < HISTORY_LENGTH ? "*": "", module->current_hashrate, efficiency);
        OLED_writeString(0, 0, module->oled_buf);
}

static void _update_shares(SystemModule* module){
    if(module->screen_page != 0){
            return;
    }
    OLED_clearLine(1);
    memset(module->oled_buf, 0, 20);
    snprintf(module->oled_buf, 20, "A/R: %u/%u", module->shares_accepted, module->shares_rejected);
    OLED_writeString(0, 1, module->oled_buf);
}

static void _update_best_diff(SystemModule* module){
        if(module->screen_page != 0){
            return;
    }
    OLED_clearLine(3);
    memset(module->oled_buf, 0, 20);
    snprintf(module->oled_buf, 20, "BD: %u", module->best_nonce_diff);
    OLED_writeString(0, 3, module->oled_buf);
}

static void _clear_display(void){
    OLED_clearLine(0);
    OLED_clearLine(1);
    OLED_clearLine(2);
    OLED_clearLine(3);
}


static void _update_system_info(SystemModule* module) {


    uint16_t fan_speed = EMC2101_get_fan_speed();
    float chip_temp = EMC2101_get_chip_temp();
    float voltage = INA260_read_voltage();
    float power = INA260_read_power() / 1000;
    float current = INA260_read_current();

    if (OLED_status()) {

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, " Fan: %d RPM", fan_speed);
        OLED_writeString(0, 0, module->oled_buf);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "Temp: %.1f C", chip_temp);
        OLED_writeString(0, 1, module->oled_buf);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, " Pwr: %.3f W", power);
        OLED_writeString(0, 2, module->oled_buf);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, " %i mV: %i mA",(int)voltage, (int)current);
        OLED_writeString(0, 3, module->oled_buf);
    }

}

static void _update_esp32_info(SystemModule* module) {


    uint32_t free_heap_size = esp_get_free_heap_size();

    uint16_t vcore = ADC_get_vcore();

    if (OLED_status()) {

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "FH: %u bytes", free_heap_size);
        OLED_writeString(0, 0, module->oled_buf);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "vCore: %u mV", vcore);
        OLED_writeString(0, 1, module->oled_buf);

        // memset(oled_buf, 0, 20);
        // snprintf(oled_buf, 20, "Pwr: %.2f W", power);
        // OLED_writeString(0, 2, oled_buf);
    }

}

static void _update_system_performance(SystemModule* module){


    // Calculate the uptime in seconds
    double uptime_in_seconds = difftime(time(NULL), module->start_time);
    int uptime_in_days = uptime_in_seconds / (3600 * 24);
    int remaining_seconds = (int)uptime_in_seconds % (3600 * 24);
    int uptime_in_hours = remaining_seconds / 3600;
    remaining_seconds %= 3600;
    int uptime_in_minutes = remaining_seconds / 60;


    if (OLED_status()) {
        
        _update_hashrate(module);
        _update_shares(module);
        _update_best_diff(module);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "UT: %dd %ih %im", uptime_in_days, uptime_in_hours, uptime_in_minutes);
        OLED_writeString(0, 2, module->oled_buf);
    }
}

static double _calculate_network_difficultiy(uint32_t nBits) {
    uint32_t mantissa = nBits & 0x007fffff;       // Extract the mantissa from nBits
    uint8_t exponent = (nBits >> 24) & 0xff;       // Extract the exponent from nBits
    
    double target = (double)mantissa * pow(256,(exponent - 3));   // Calculate the target value
    
    double difficulty = (pow(2, 208) * 65535) / target;    // Calculate the difficulty
    
    return difficulty;
}


static void _check_for_best_diff(SystemModule * module, uint32_t diff, uint32_t nbits){
    if(diff < module->best_nonce_diff){
        return;
    }
    module->best_nonce_diff = diff;
    uint32_t network_diff = _calculate_network_difficultiy(nbits);
    ESP_LOGI(TAG, "Network diff: %u", network_diff);
}


void SYSTEM_task(void *pvParameters) {

    SystemModule *module = (SystemModule*)pvParameters;
    _init_system(module);

    while(1){
        _clear_display();
        module->screen_page = 0;
        _update_system_performance(module);
        vTaskDelay(40000 / portTICK_RATE_MS);

        _clear_display();
        module->screen_page = 1;
        _update_system_info(module);
        vTaskDelay(10000 / portTICK_RATE_MS);

        _clear_display();
        module->screen_page = 2;
        _update_esp32_info(module);
        vTaskDelay(10000 / portTICK_RATE_MS);
        
    }
}


void SYSTEM_notify_accepted_share(SystemModule* module){
    module->shares_accepted++;
    _update_shares(module);
}
void SYSTEM_notify_rejected_share(SystemModule* module){
    module->shares_rejected++;
    _update_shares(module);
}


void SYSTEM_notify_mining_started(SystemModule* module){
    module->duration_start = esp_timer_get_time();
}

void SYSTEM_notify_found_nonce(SystemModule* module, double pool_diff, double found_diff, uint32_t nbits){

 

    // Calculate the time difference in seconds with sub-second precision
    


    // hashrate = (nonce_difficulty * 2^32) / time_to_find
    
    module->historical_hashrate[module->historical_hashrate_rolling_index] = pool_diff;
    module->historical_hashrate_time_stamps[module->historical_hashrate_rolling_index] = esp_timer_get_time();

    module->historical_hashrate_rolling_index = (module->historical_hashrate_rolling_index + 1) % HISTORY_LENGTH;

   //ESP_LOGI(TAG, "nonce_diff %.1f, ttf %.1f, res %.1f", nonce_diff, duration, historical_hashrate[historical_hashrate_rolling_index]);
   

    if(module->historical_hashrate_init < HISTORY_LENGTH){
        module->historical_hashrate_init++;
    }else{
        module->duration_start = module->historical_hashrate_time_stamps[(module->historical_hashrate_rolling_index + 1) % HISTORY_LENGTH];
    }
    double sum = 0;
    for (int i = 0; i < module->historical_hashrate_init; i++) {
        sum += module->historical_hashrate[i];
    }

    double duration = (double)(esp_timer_get_time() - module->duration_start) / 1000000;

    double rolling_rate = (sum * 4294967296) / (duration * 1000000000);
    if(module->historical_hashrate_init < HISTORY_LENGTH){
        module->current_hashrate = rolling_rate;
    }else{
        // More smoothing
        module->current_hashrate = ((module->current_hashrate * 9) + rolling_rate)/10;
    }

    _update_hashrate(module);

    // logArrayContents(historical_hashrate, HISTORY_LENGTH);
    // logArrayContents(historical_hashrate_time_stamps, HISTORY_LENGTH);

    _check_for_best_diff(module, found_diff, 0);
    
}









