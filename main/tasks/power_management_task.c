#include "global_state.h"
#include <string.h>
#include "esp_log.h"
#include "mining.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bm1397.h"
#include "EMC2101.h"
#include "INA260.h"
#include "math.h"
#include "serial.h"

#define POLL_RATE 5000
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 80.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)
#define ASIC_MODEL CONFIG_ASIC_MODEL

static const char * TAG = "power_management";

static float _fbound(float value, float lower_bound, float upper_bound)
{
	if (value < lower_bound)
		return lower_bound;
	if (value > upper_bound)
		return upper_bound;

	return value;
}

// void _power_init(PowerManagementModule * power_management){
//     power_management->frequency_multiplier = 1;
//     power_management->frequency_value = ASIC_FREQUENCY;

// }

void POWER_MANAGEMENT_task(void * pvParameters){

    GlobalState *GLOBAL_STATE = (GlobalState*)pvParameters;
    //bm1397Module * bm1397 = &GLOBAL_STATE->BM1397_MODULE;
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
   // _power_init(power_management);

    int last_frequency_increase = 0;
    while(1){

        power_management->voltage = INA260_read_voltage();
        power_management->power = INA260_read_power() / 1000;
        power_management->current = INA260_read_current();
        power_management->fan_speed = EMC2101_get_fan_speed();

        if(strcmp(ASIC_MODEL, "BM1397") == 0){
            power_management->chip_temp = EMC2101_get_chip_temp();

            // Voltage
            // We'll throttle between 4.9v and 3.5v
            float voltage_multiplier =  _fbound((power_management->voltage - VOLTAGE_MIN_THROTTLE) * (1/(float)VOLTAGE_RANGE), 0, 1);


            // Temperature
            float temperature_multiplier = 1;
            float over_temp = -(THROTTLE_TEMP - power_management->chip_temp);
            if(over_temp > 0){
                temperature_multiplier = (THROTTLE_TEMP_RANGE - over_temp)/THROTTLE_TEMP_RANGE;
            }

            float lowest_multiplier = 1;
            float multipliers[2] = {voltage_multiplier, temperature_multiplier};

            for(int i = 0; i < 2; i++){
                if(multipliers[i] < lowest_multiplier){
                    lowest_multiplier = multipliers[i];
                }
            }



            power_management->frequency_multiplier = lowest_multiplier;


            float target_frequency = _fbound(power_management->frequency_multiplier * ASIC_FREQUENCY, 0, ASIC_FREQUENCY);

            if(target_frequency < 50){
                // TODO: Turn the chip off
            }

            // chip is coming back from a low/no voltage event
            if(power_management->frequency_value <  50 && target_frequency > 50){
                // TODO recover gracefully?
                esp_restart();
            }


            if(power_management->frequency_value > target_frequency){
                power_management->frequency_value = target_frequency;
                last_frequency_increase = 0;
                BM1397_send_hash_frequency(power_management->frequency_value);
                ESP_LOGI(TAG, "target %f, Freq %f, Temp %f, Power %f", target_frequency, power_management->frequency_value, power_management->chip_temp, power_management->power);
            }else{
                if(
                    last_frequency_increase > 120 &&
                    power_management->frequency_value != ASIC_FREQUENCY
                ){
                    float add = (target_frequency + power_management->frequency_value) / 2;
                    power_management->frequency_value += _fbound(add, 2 , 20);
                    BM1397_send_hash_frequency(power_management->frequency_value);
                    ESP_LOGI(TAG, "target %f, Freq %f, Temp %f, Power %f", target_frequency, power_management->frequency_value, power_management->chip_temp, power_management->power);
                    last_frequency_increase = 60;
                }else{
                    last_frequency_increase++;
                }
            }
        }


        //ESP_LOGI(TAG, "target %f, Freq %f, Volt %f, Power %f", target_frequency, power_management->frequency_value, power_management->voltage, power_management->power);
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}