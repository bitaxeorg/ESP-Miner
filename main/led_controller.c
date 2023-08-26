#include <stdio.h>
#include "esp_err.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

// LEDS
#define LEDX_R GPIO_NUM_35
#define LEDX_G GPIO_NUM_36
#define LEDX_B GPIO_NUM_37
#define LEDZ_R GPIO_NUM_5
#define LEDZ_G GPIO_NUM_6
#define LEDZ_B GPIO_NUM_7

void initLEDs(void)
{
    gpio_set_direction(LEDX_R, GPIO_MODE_OUTPUT);
    gpio_set_direction(LEDX_G, GPIO_MODE_OUTPUT);
    gpio_set_direction(LEDX_B, GPIO_MODE_OUTPUT);
    gpio_set_direction(LEDZ_R, GPIO_MODE_OUTPUT);
    gpio_set_direction(LEDZ_G, GPIO_MODE_OUTPUT);
    gpio_set_direction(LEDZ_B, GPIO_MODE_OUTPUT);

    gpio_set_level(LEDX_R, 0);
    gpio_set_level(LEDX_G, 0);
    gpio_set_level(LEDX_B, 0);
    gpio_set_level(LEDZ_R, 0);
    gpio_set_level(LEDZ_G, 0);
    gpio_set_level(LEDZ_B, 0);
}

void ledc_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_12_BIT, // Set duty resolution to 12 bits
        .freq_hz = 5000,                      // Set output frequency at 5 kHz
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel0 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 35, // Define the output GPIO
        .duty = 0,      // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel0));

    ledc_channel_config_t ledc_channel1 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 36, // Define the output GPIO
        .duty = 0,      // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel1));

    ledc_channel_config_t ledc_channel2 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 37, // Define the output GPIO
        .duty = 0,      // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel2));

    ledc_channel_config_t ledc_channel3 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_3,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 38, // Define the output GPIO
        .duty = 0,      // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel3));

    ledc_channel_config_t ledc_channel4 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_4,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 39, // Define the output GPIO
        .duty = 0,      // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel4));

    ledc_channel_config_t ledc_channel5 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_5,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 40, // Define the output GPIO
        .duty = 0,      // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel5));
}

void led_set(void)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 3000)); // r
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1000)); // g
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0));    // b

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 3000)); // r
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, 1000)); // g
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, 0));    // b

    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2));

    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5));
}