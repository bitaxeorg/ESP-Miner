#ifndef DEVICE_PINS_H_
#define DEVICE_PINS_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "soc/gpio_num.h"

typedef struct {
    gpio_num_t tx;
    gpio_num_t rx;
} BapPins;

typedef struct {
    gpio_num_t sda;
    gpio_num_t scl;
} I2cPins;

typedef struct {
    gpio_num_t data[8];
    gpio_num_t rd;
    gpio_num_t pwr;
    gpio_num_t wr;
    gpio_num_t cs;
    gpio_num_t dc;
    gpio_num_t rst;
    gpio_num_t bk_light;
} I80Pins;

typedef struct {
    const BapPins * bap;
    const I2cPins * i2c;
    const I80Pins * i80;
} DevicePins;

extern const BapPins DEFAULT_BAP_PINS;
extern const I2cPins DEFAULT_I2C_PINS;
extern const I2cPins BITAXE_COLOR_I2C_PINS;
extern const I80Pins ST7789_I80_PINS;

#define BITAXE_ORIGINAL_DEVICE_PINS { .bap = &DEFAULT_BAP_PINS, .i2c = &DEFAULT_I2C_PINS, .i80 = NULL }
#define BITAXE_COLOR_DEVICE_PINS    { .bap = NULL,              .i2c = &BITAXE_COLOR_I2C_PINS, .i80 = &ST7789_I80_PINS }

#define BITAXE_ORIGINAL_PINS        BITAXE_ORIGINAL_DEVICE_PINS
#define BITAXE_COLOR_PINS           BITAXE_COLOR_DEVICE_PINS

esp_err_t device_pins_init(DevicePins * pins);

#endif /* DEVICE_PINS_H_ */
