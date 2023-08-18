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
#include <stdint.h>
#include <math.h>
#include <inttypes.h>
#include "global_state.h"
#include "esp_timer.h"

static const char *TAG = "SystemModule";

#define BM1397_VOLTAGE CONFIG_BM1397_VOLTAGE

static void _suffix_string(uint64_t, char *, size_t, int);


static void _init_system(SystemModule* module) {

    module->duration_start = 0;
    module->historical_hashrate_rolling_index = 0;
    module->historical_hashrate_init = 0;
    module->current_hashrate = 0;
    module->screen_page = 0;
    module->shares_accepted = 0;
    module->shares_rejected = 0;
    module->best_nonce_diff = 0;
    module->start_time = esp_timer_get_time();
    module->lastClockSync = 0;
    module->FOUND_BLOCK = false;
    module->startup_done = false;

    //set the best diff string to 0
    _suffix_string(0, module->best_diff_string, DIFF_STRING_SIZE, 0);

    //set the ssid string to blank
    memset(module->ssid, 0, 20);

    //set the wifi_status to blank
    memset(module->wifi_status, 0, 20);

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
    vTaskDelay(500 / portTICK_PERIOD_MS);

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
    snprintf(module->oled_buf, 20, module->FOUND_BLOCK ? "!!! BLOCK FOUND !!!" : "BD: %s",  module->best_diff_string);
    OLED_writeString(0, 3, module->oled_buf);
}

static void _clear_display(void){
    OLED_clearLine(0);
    OLED_clearLine(1);
    OLED_clearLine(2);
    OLED_clearLine(3);
}


static void _update_system_info(GlobalState *GLOBAL_STATE) {

    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    if (OLED_status()) {

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, " Fan: %d RPM", power_management->fan_speed);
        OLED_writeString(0, 0, module->oled_buf);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "Temp: %.1f C", power_management->chip_temp);
        OLED_writeString(0, 1, module->oled_buf);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, " Pwr: %.3f W", power_management->power);
        OLED_writeString(0, 2, module->oled_buf);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, " %i mV: %i mA",(int)power_management->voltage, (int)power_management->current);
        OLED_writeString(0, 3, module->oled_buf);
    }

}

static void _update_esp32_info(SystemModule* module) {


    uint32_t free_heap_size = esp_get_free_heap_size();

    uint16_t vcore = ADC_get_vcore();

    if (OLED_status()) {

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "FH: %lu bytes", free_heap_size);
        OLED_writeString(0, 0, module->oled_buf);

        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "vCore: %u mV", vcore);
        OLED_writeString(0, 1, module->oled_buf);

        // memset(oled_buf, 0, 20);
        // snprintf(oled_buf, 20, "Pwr: %.2f W", power);
        // OLED_writeString(0, 2, oled_buf);
    }

}

static void _init_connection(SystemModule* module) {

    if (OLED_status()) {
        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "Connecting to ssid:");
        OLED_writeString(0, 0, module->oled_buf);
    }

}

static void _update_connection(SystemModule* module) {

    if (OLED_status()) {
        OLED_clearLine(2);
        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "%s", module->ssid);
        OLED_writeString(0, 1, module->oled_buf);

        OLED_clearLine(3);
        memset(module->oled_buf, 0, 20);
        snprintf(module->oled_buf, 20, "%s", module->wifi_status);
        OLED_writeString(0, 3, module->oled_buf);
    }

}

static void _update_system_performance(SystemModule* module){


    // Calculate the uptime in seconds
    double uptime_in_seconds = (esp_timer_get_time() - module->start_time) / 1000000;
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



static double _calculate_network_difficulty(uint32_t nBits) {
    uint32_t mantissa = nBits & 0x007fffff;       // Extract the mantissa from nBits
    uint8_t exponent = (nBits >> 24) & 0xff;       // Extract the exponent from nBits

    double target = (double)mantissa * pow(256,(exponent - 3));   // Calculate the target value

    double difficulty = (pow(2, 208) * 65535) / target;    // Calculate the difficulty

    return difficulty;
}

static void _check_for_best_diff(SystemModule * module, double diff, uint32_t nbits){
    if(diff < module->best_nonce_diff){
        return;
    }
    module->best_nonce_diff = diff;
    //make the best_nonce_diff into a string
    _suffix_string((uint64_t)diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    double network_diff = _calculate_network_difficulty(nbits);
    if(diff > network_diff){
        module->FOUND_BLOCK = true;
        ESP_LOGI(TAG, "FOUND BLOCK!!!!!!!!!!!!!!!!!!!!!! %f > %f", diff, network_diff);
    }
    ESP_LOGI(TAG, "Network diff: %f", network_diff);
}

/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
static void _suffix_string(uint64_t val, char *buf, size_t bufsiz, int sigdigits) {
	const double  dkilo = 1000.0;
	const uint64_t kilo = 1000ull;
	const uint64_t mega = 1000000ull;
	const uint64_t giga = 1000000000ull;
	const uint64_t tera = 1000000000000ull;
	const uint64_t peta = 1000000000000000ull;
	const uint64_t exa  = 1000000000000000000ull;
	char suffix[2] = "";
	bool decimal = true;
	double dval;

	if (val >= exa) {
		val /= peta;
		dval = (double)val / dkilo;
		strcpy(suffix, "E");
	} else if (val >= peta) {
		val /= tera;
		dval = (double)val / dkilo;
		strcpy(suffix, "P");
	} else if (val >= tera) {
		val /= giga;
		dval = (double)val / dkilo;
		strcpy(suffix, "T");
	} else if (val >= giga) {
		val /= mega;
		dval = (double)val / dkilo;
		strcpy(suffix, "G");
	} else if (val >= mega) {
		val /= kilo;
		dval = (double)val / dkilo;
		strcpy(suffix, "M");
	} else if (val >= kilo) {
		dval = (double)val / dkilo;
		strcpy(suffix, "k");
	} else {
		dval = val;
		decimal = false;
	}

	if (!sigdigits) {
		if (decimal)
			snprintf(buf, bufsiz, "%.3g%s", dval, suffix);
		else
			snprintf(buf, bufsiz, "%d%s", (unsigned int)dval, suffix);
	} else {
		/* Always show sigdigits + 1, padded on right with zeroes
		 * followed by suffix */
		int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

		snprintf(buf, bufsiz, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
	}
}


void SYSTEM_task(void *pvParameters) {

    GlobalState *GLOBAL_STATE = (GlobalState*)pvParameters;
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;

    _init_system(module);

    _clear_display();
    _init_connection(module);

    //show the connection screen
    while (!module->startup_done) {
        _update_connection(module);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }


    while(1){
        _clear_display();
        module->screen_page = 0;
        _update_system_performance(module);
        vTaskDelay(40000 / portTICK_PERIOD_MS);

        _clear_display();
        module->screen_page = 1;
        _update_system_info(GLOBAL_STATE);
        vTaskDelay(10000 / portTICK_PERIOD_MS);

        _clear_display();
        module->screen_page = 2;
        _update_esp32_info(module);
        vTaskDelay(10000 / portTICK_PERIOD_MS);

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

void SYSTEM_notify_new_ntime(SystemModule* module, uint32_t ntime){
    // Hourly clock sync
    if(module->lastClockSync + (60 * 60) > ntime){
        return;
    }
    ESP_LOGI(TAG, "Syncing clock");
    module->lastClockSync = ntime;
    struct timeval tv;
    tv.tv_sec = ntime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
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

    _check_for_best_diff(module, found_diff, nbits);

}








