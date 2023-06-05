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


static const char *TAG = "system";

#define BM1397_VOLTAGE CONFIG_BM1397_VOLTAGE
#define HISTORY_LENGTH 100
#define HISTORY_WINDOW_SIZE 5

static char oled_buf[20];
static int screen_page = 0;

static uint16_t shares_accepted = 0;
static uint16_t shares_rejected = 0;
static time_t start_time;

static double duration_start = 0;
static int historical_hashrate_rolling_index = 0;
static double historical_hashrate_time_stamps[HISTORY_LENGTH] = {0.0};
static double historical_hashrate[HISTORY_LENGTH] = {0.0};
static int historical_hashrate_init = 0;
static double current_hashrate = 0;


// void logArrayContents(const double* array, size_t length) {
//     char logMessage[1024];  // Adjust the buffer size as needed
//     int offset = 0;
    
//     offset += snprintf(logMessage + offset, sizeof(logMessage) - offset, "Array Contents: [");
    
//     for (size_t i = 0; i < length; i++) {
//         offset += snprintf(logMessage + offset, sizeof(logMessage) - offset, "%.1f%s", array[i], (i < length - 1) ? ", " : "]");
//     }
    
//     ESP_LOGI(TAG, "%s", logMessage);
// }

void update_hashrate(void){

        if(screen_page != 0){
            return;
        }

        OLED_clearLine(1);
        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "GH/s%s: %.1f", historical_hashrate_init < HISTORY_LENGTH ? "*": "", current_hashrate);
        OLED_writeString(0, 1, oled_buf);
}

void update_shares(void){
    if(screen_page != 0){
            return;
    }
    OLED_clearLine(2);
    memset(oled_buf, 0, 20);
    snprintf(oled_buf, 20, "A/R: %u/%u", shares_accepted, shares_rejected);
    OLED_writeString(0, 2, oled_buf);
}

void notify_system_accepted_share(void){
    shares_accepted++;
    update_shares();
}
void notify_system_rejected_share(void){
    shares_rejected++;
    update_shares();
}




void notify_system_found_nonce(double nonce_diff){

 

    // Calculate the time difference in seconds with sub-second precision
    


    // hashrate = (nonce_difficulty * 2^32) / time_to_find
    
    historical_hashrate[historical_hashrate_rolling_index] = nonce_diff;
    historical_hashrate_time_stamps[historical_hashrate_rolling_index] = esp_timer_get_time();

    historical_hashrate_rolling_index = (historical_hashrate_rolling_index + 1) % HISTORY_LENGTH;

   //ESP_LOGI(TAG, "nonce_diff %.1f, ttf %.1f, res %.1f", nonce_diff, duration, historical_hashrate[historical_hashrate_rolling_index]);
   

    if(historical_hashrate_init < HISTORY_LENGTH){
        historical_hashrate_init++;
    }else{
        duration_start = historical_hashrate_time_stamps[(historical_hashrate_rolling_index + 1) % HISTORY_LENGTH];
    }
    double sum = 0;
    for (int i = 0; i < historical_hashrate_init; i++) {
        sum += historical_hashrate[i];
    }

    double duration = (double)(esp_timer_get_time() - duration_start) / 1000000;

    current_hashrate = (sum * 4294967296) / (duration * 1000000000);

    update_hashrate();

    // logArrayContents(historical_hashrate, HISTORY_LENGTH);
    // logArrayContents(historical_hashrate_time_stamps, HISTORY_LENGTH);
    
}


void init_system(void) {
    
    start_time = time(NULL);

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

    duration_start = esp_timer_get_time();
}

void update_system_info(void) {
    char oled_buf[20];

    uint16_t fan_speed = EMC2101_get_fan_speed();
    float chip_temp = EMC2101_get_chip_temp();
    //float current = INA260_read_current();
    //float voltage = INA260_read_voltage();
    float power = INA260_read_power() / 1000;
    //uint16_t vcore = ADC_get_vcore();

    if (OLED_status()) {
        OLED_clearLine(1);
        OLED_clearLine(2);
        OLED_clearLine(3);

        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "Fan: %d RPM", fan_speed);
        OLED_writeString(0, 1, oled_buf);

        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "Temp: %.2f C", chip_temp);
        OLED_writeString(0, 2, oled_buf);

        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "Pwr: %.2f W", power);
        OLED_writeString(0, 3, oled_buf);
    }

}

void update_esp32_info(void) {
    char oled_buf[20];

    uint32_t free_heap_size = esp_get_free_heap_size();

    if (OLED_status()) {
        OLED_clearLine(1);
        OLED_clearLine(2);
        OLED_clearLine(3);

        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "FH: %u bytes", free_heap_size);
        OLED_writeString(0, 1, oled_buf);

        // memset(oled_buf, 0, 20);
        // snprintf(oled_buf, 20, "Temp: %.2f C", chip_temp);
        // OLED_writeString(0, 2, oled_buf);

        // memset(oled_buf, 0, 20);
        // snprintf(oled_buf, 20, "Pwr: %.2f W", power);
        // OLED_writeString(0, 3, oled_buf);
    }

}

void update_system_performance(){
    
    // Calculate the uptime in seconds
    double uptime_in_seconds = difftime(time(NULL), start_time);
    int uptime_in_days = uptime_in_seconds / (3600 * 24);
    int remaining_seconds = (int)uptime_in_seconds % (3600 * 24);
    int uptime_in_hours = remaining_seconds / 3600;
    remaining_seconds %= 3600;
    int uptime_in_minutes = remaining_seconds / 60;


    if (OLED_status()) {


        OLED_clearLine(3);

        update_hashrate();
        update_shares();


        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "UT: %dd %ih %im", uptime_in_days, uptime_in_hours, uptime_in_minutes);
        OLED_writeString(0, 3, oled_buf);
    }
}




void system_task(void *arg) {

    init_system();

    while(1){
        screen_page = 0;
        update_system_performance();
        vTaskDelay(30000 / portTICK_RATE_MS);

        screen_page = 1;
        update_system_info();
        vTaskDelay(5000 / portTICK_RATE_MS);

        screen_page = 2;
        update_esp32_info();
        vTaskDelay(5000 / portTICK_RATE_MS);
        
    }
}