// Reset-only port of rhapd-bitaxe-bonanza's RP2040 multidrop SWD transport.
#include "bzm/bridge_reset.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "bzm_bridge_reset"
#define CORE0 0x01002927u
#define CORE1 0x11002927u
#define DHCSR 0xe000edf0u
#define DEBUG_KEY 0xa05f0000u

typedef struct {
    gpio_num_t clock, data;
} bridge_swd_t;

static void swd_pulse(const bridge_swd_t *swd)
{
    gpio_set_level(swd->clock, 0);
    esp_rom_delay_us(2);
    gpio_set_level(swd->clock, 1);
    esp_rom_delay_us(2);
}

static void swd_write_bits(const bridge_swd_t *swd, uint32_t value, unsigned count)
{
    for (unsigned bit = 0; bit < count; ++bit) {
        gpio_set_level(swd->data, (value >> bit) & 1u);
        swd_pulse(swd);
    }
}

static uint32_t swd_read_bits(const bridge_swd_t *swd, unsigned count)
{
    uint32_t value = 0;
    for (unsigned bit = 0; bit < count; ++bit) {
        gpio_set_level(swd->clock, 0);
        esp_rom_delay_us(2);
        value |= (uint32_t)(gpio_get_level(swd->data) != 0) << bit;
        gpio_set_level(swd->clock, 1);
        esp_rom_delay_us(2);
    }
    return value;
}

static void swd_input(const bridge_swd_t *swd)
{
    gpio_set_direction(swd->data, GPIO_MODE_INPUT);
    swd_pulse(swd);
}

static void swd_output(const bridge_swd_t *swd)
{
    swd_pulse(swd);
    gpio_set_level(swd->data, 0);
    gpio_set_direction(swd->data, GPIO_MODE_INPUT_OUTPUT);
}

static unsigned parity(uint32_t value)
{
    return (unsigned)__builtin_parity(value);
}

static uint8_t swd_request(bool ap, bool read, uint8_t address)
{
    unsigned bits = (unsigned)ap | ((unsigned)read << 1) | (address & 0x0cu);
    return 0x81u | (bits << 1) | (parity(bits) << 5);
}

static esp_err_t swd_transfer(const bridge_swd_t *swd, bool ap, bool read,
                              uint8_t address, uint32_t *value)
{
    uint8_t request = swd_request(ap, read, address);
    for (unsigned retry = 0; retry < 16; ++retry) {
        swd_write_bits(swd, request, 8);
        swd_input(swd);
        uint32_t ack = swd_read_bits(swd, 3);
        if (ack != 1) {
            swd_output(swd);
            swd_write_bits(swd, 0, 8);
            if (ack == 2) continue;
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (read) {
            *value = swd_read_bits(swd, 32);
            unsigned actual_parity = swd_read_bits(swd, 1);
            swd_output(swd);
            swd_write_bits(swd, 0, 8);
            if (actual_parity != parity(*value)) return ESP_ERR_INVALID_CRC;
        } else {
            swd_output(swd);
            swd_write_bits(swd, *value, 32);
            swd_write_bits(swd, parity(*value), 1);
            swd_write_bits(swd, 0, 8);
        }
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t dp_write(const bridge_swd_t *swd, uint8_t address, uint32_t value)
{
    return swd_transfer(swd, false, false, address, &value);
}

static esp_err_t dp_read(const bridge_swd_t *swd, uint8_t address, uint32_t *value)
{
    return swd_transfer(swd, false, true, address, value);
}

static esp_err_t ap_write(const bridge_swd_t *swd, uint8_t address, uint32_t value)
{
    ESP_RETURN_ON_ERROR(swd_transfer(swd, true, false, address, &value), TAG, "AP write failed");
    return dp_read(swd, 0x0c, &value);
}

static esp_err_t swd_attach(const bridge_swd_t *swd, uint32_t target)
{
    static const uint8_t sequence[] = {
        0xff, 0x75, 0x77, 0x77, 0x67,
        0xff, 0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9, 0xaf, 0xdd, 0xe3,
        0xa2, 0x0e, 0xbc, 0x19, 0xa0, 0xf1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0,
    };
    gpio_set_level(swd->clock, 1);
    gpio_set_level(swd->data, 1);
    gpio_set_direction(swd->data, GPIO_MODE_INPUT_OUTPUT);
    for (size_t i = 0; i < sizeof(sequence); ++i) swd_write_bits(swd, sequence[i], 8);
    swd_write_bits(swd, 0x99, 8);
    swd_input(swd);
    (void)swd_read_bits(swd, 3); // Multidrop TARGETSEL is deliberately not acknowledged.
    swd_output(swd);
    swd_write_bits(swd, target, 32);
    swd_write_bits(swd, parity(target), 1);
    swd_write_bits(swd, 0, 8);
    uint32_t value = 0;
    ESP_RETURN_ON_ERROR(dp_read(swd, 0, &value), TAG, "DPIDR read failed");
    if (value != 0x0bc12477u) return ESP_ERR_INVALID_RESPONSE;
    ESP_RETURN_ON_ERROR(dp_write(swd, 0, 0x1e), TAG, "clear debug errors failed");
    ESP_RETURN_ON_ERROR(dp_write(swd, 8, 0), TAG, "select MEM-AP failed");
    ESP_RETURN_ON_ERROR(dp_write(swd, 4, 0x50000000u), TAG, "debug power request failed");
    for (unsigned retry = 0; retry < 32; ++retry) {
        ESP_RETURN_ON_ERROR(dp_read(swd, 4, &value), TAG, "debug power status failed");
        if ((value & 0xa0000000u) == 0xa0000000u) return ap_write(swd, 0, 0xa2000002u);
        vTaskDelay(1);
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t mem_write(const bridge_swd_t *swd, uint32_t address, uint32_t value)
{
    ESP_RETURN_ON_ERROR(ap_write(swd, 4, address), TAG, "memory address failed");
    return ap_write(swd, 0x0c, value);
}

static esp_err_t mem_read(const bridge_swd_t *swd, uint32_t address, uint32_t *value)
{
    ESP_RETURN_ON_ERROR(ap_write(swd, 4, address), TAG, "memory address failed");
    ESP_RETURN_ON_ERROR(swd_transfer(swd, true, true, 0x0c, value), TAG, "memory read failed");
    return dp_read(swd, 0x0c, value);
}

static esp_err_t reset_attached_bridge(const bridge_swd_t *swd)
{
    ESP_RETURN_ON_ERROR(swd_attach(swd, CORE1), TAG, "attach core 1 failed");
    ESP_RETURN_ON_ERROR(mem_write(swd, DHCSR, DEBUG_KEY), TAG, "release core 1 failed");
    ESP_RETURN_ON_ERROR(swd_attach(swd, CORE0), TAG, "attach core 0 failed");
    ESP_RETURN_ON_ERROR(mem_write(swd, DHCSR, DEBUG_KEY), TAG, "release core 0 failed");
    ESP_RETURN_ON_ERROR(mem_write(swd, 0x40010008u, 0x0001fffcu), TAG, "watchdog domains failed");
    ESP_RETURN_ON_ERROR(mem_write(swd, 0x4005801cu, 0), TAG, "clear SRAM boot vector failed");
    ESP_RETURN_ON_ERROR(ap_write(swd, 4, 0x40058000u), TAG, "watchdog address failed");
    uint32_t trigger = 0x80000000u;
    // The reset drops the debug link, so do not drain this posted write.
    ESP_RETURN_ON_ERROR(swd_transfer(swd, true, false, 0x0c, &trigger), TAG, "watchdog trigger failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(swd_attach(swd, CORE0), TAG, "reattach core 0 failed");
    uint32_t value = 0;
    ESP_RETURN_ON_ERROR(mem_read(swd, 0x40058008u, &value), TAG, "watchdog reason failed");
    if ((value & 2u) == 0) return ESP_ERR_INVALID_RESPONSE;
    ESP_RETURN_ON_ERROR(mem_read(swd, DHCSR, &value), TAG, "core 0 status failed");
    if (value & (1u << 17)) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(swd_attach(swd, CORE1), TAG, "reattach core 1 failed");
    ESP_RETURN_ON_ERROR(mem_read(swd, DHCSR, &value), TAG, "core 1 status failed");
    return value & (1u << 17) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t bzm_bridge_reset(gpio_num_t clock, gpio_num_t data)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(clock) || !GPIO_IS_VALID_OUTPUT_GPIO(data) || clock == data)
        return ESP_ERR_INVALID_ARG;
    bridge_swd_t swd = {.clock = clock, .data = data};
    gpio_set_level(clock, 1);
    gpio_set_level(data, 1);
    gpio_config_t config = {.pin_bit_mask = (1ULL << clock) | (1ULL << data),
        .mode = GPIO_MODE_INPUT_OUTPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    esp_err_t err = gpio_config(&config);
    if (err == ESP_OK) err = reset_attached_bridge(&swd);
    gpio_set_direction(data, GPIO_MODE_INPUT);
    gpio_set_direction(clock, GPIO_MODE_INPUT);
    return err;
}
