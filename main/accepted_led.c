//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Source file : accepted_led.c
// Header file : accepted_led.h
//
// Description : For Open Source Bitaxe ESP-Miner project.
//               implements one-shot timer to flash LED on GPIO upon share acceptance
//
// Requirements : An LED and suitable series resistor should be connected to the allocated GPIO port of the ESP32-S3 MCU
//				  so that the anode of the LED is attached to the ESP32-S3 GPIO and the resistor ties the cathode of the LED to GND.
//                It is intended to use the accesory port on the Bitaxe board to connect an LED to.
//                The default GPIO port for the LED is GPIO39.
//
// accepted_led_init(); should be called in system.c at "SYSTEM_init_peripherals" to initialise the GPIO and Timer
// accepted_led_trigger(); should be called in stratum_task.c when an approprate stratum share accepted message is received
//
// TODO: add error checking
//////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"

#include "esp_timer.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#define accepted_led_duration 200000ULL //200mS 200000uS

#ifndef CONFIG_GPIO_ACCEPTED_LED
#define GPIO_ACCEPTED_LED 39
#else
#define GPIO_ACCEPTED_LED CONFIG_GPIO_ACCEPTED_LED
#endif

static const char * TAG = "Accepted_LED";


static esp_timer_handle_t accepted_led_oneshot_timer;


static void accepted_led_oneshot_timer_cb(void* arg){
    //once the timer has expired the call-back function is called to turn off the LED
    gpio_set_level(GPIO_ACCEPTED_LED, 0); //ensure LED is off
}

esp_err_t accepted_led_init (void){

	//init GPIO port
	gpio_config_t accepted_led_conf = {
	 .intr_type = GPIO_INTR_DISABLE,
	 .mode = GPIO_MODE_OUTPUT,
	 .pin_bit_mask = (1ULL << GPIO_ACCEPTED_LED),
	 .pull_down_en = 0,
	 .pull_up_en = 0,
	};

	gpio_config(&accepted_led_conf);

	gpio_set_level(GPIO_ACCEPTED_LED, 0); //ensure LED is off at init.


	//init OneShot timer

	const esp_timer_create_args_t accepted_led_oneshot_timer_args = {
	 .callback = &accepted_led_oneshot_timer_cb,
	 /* argument specified here will be passed to timer callback function */
	 .arg = (void*) accepted_led_oneshot_timer,
	 .name = "accepted-led-one-shot"
	};

	//create the timer but do not start it until it is first called from a valid share being accepted
	ESP_ERROR_CHECK(esp_timer_create(&accepted_led_oneshot_timer_args, &accepted_led_oneshot_timer));
	ESP_LOGI(TAG, "Accepted LED timer initialised");
	return ESP_OK;

}


esp_err_t accepted_led_trigger(void){

	gpio_set_level(GPIO_ACCEPTED_LED, 1); //turn on the LED

	if (esp_timer_is_active(accepted_led_oneshot_timer)){
	//if the timer is already running then restart it
	esp_timer_restart(accepted_led_oneshot_timer, accepted_led_duration);

	}
	else
	{
	//otherwise start it for the first time
	esp_timer_start_once(accepted_led_oneshot_timer, accepted_led_duration);
	}

	return ESP_OK;

}








