#include "unity.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include <string.h>

static uint8_t input_bits[512];
static size_t input_count, input_cursor;
static gpio_mode_t clock_mode, data_mode;
static esp_err_t configure_result;
static unsigned configured;
static const struct { uint8_t request; uint32_t word; } expected_writes[] = {
    {0x99, 0x01002927}, {0x99, 0x11002927}, {0x8b, 0x40010008},
    {0xbb, 0x0001fffc}, {0x8b, 0x40058000}, {0xbb, 0x80000000},
};
static bool observed_writes[sizeof(expected_writes) / sizeof(expected_writes[0])];
static uint64_t output_window;
static size_t output_count;
static bool clock_level, data_level;

static esp_err_t reset_test_level(gpio_num_t pin, uint32_t level)
{
    TEST_ASSERT_TRUE(pin == GPIO_NUM_14 || pin == GPIO_NUM_15);
    if (pin == GPIO_NUM_15) data_level = level != 0;
    else {
        if (!clock_level && level && data_mode == GPIO_MODE_INPUT_OUTPUT) {
            output_window = (output_window >> 1) | ((uint64_t)data_level << 40);
            ++output_count;
            for (unsigned i = 0; i < sizeof(expected_writes) / sizeof(expected_writes[0]); ++i) {
                uint32_t word = expected_writes[i].word;
                if (output_count >= 41 && (uint8_t)output_window == expected_writes[i].request &&
                    (uint32_t)(output_window >> 8) == word &&
                    ((output_window >> 40) & 1u) == (unsigned)__builtin_parity(word)) observed_writes[i] = true;
            }
        }
        clock_level = level != 0;
    }
    return ESP_OK;
}
static int reset_test_read(gpio_num_t pin)
{
    TEST_ASSERT_EQUAL(GPIO_NUM_15, pin);
    TEST_ASSERT_LESS_THAN(input_count, input_cursor);
    unsigned bit = (input_bits[input_cursor / 8] >> (input_cursor % 8)) & 1u;
    ++input_cursor;
    return bit;
}
static esp_err_t reset_test_direction(gpio_num_t pin, gpio_mode_t mode)
{
    if (pin == GPIO_NUM_14) clock_mode = mode;
    else { TEST_ASSERT_EQUAL(GPIO_NUM_15, pin); data_mode = mode; }
    return ESP_OK;
}
static esp_err_t reset_test_config(const gpio_config_t *config)
{
    ++configured;
    TEST_ASSERT_TRUE(config->pin_bit_mask == ((1ULL << 14) | (1ULL << 15)));
    clock_mode = data_mode = config->mode;
    return configure_result;
}
static void reset_test_delay(uint32_t us) { TEST_ASSERT_EQUAL_UINT32(2, us); }

#define gpio_set_level reset_test_level
#define gpio_get_level reset_test_read
#define gpio_set_direction reset_test_direction
#define gpio_config reset_test_config
#define esp_rom_delay_us reset_test_delay
#define bzm_bridge_reset reset_test_bridge
#include "components/asic/bzm/port/esp/bridge_reset.c"

static void append_bits(uint32_t value, unsigned count)
{
    TEST_ASSERT_TRUE(input_count + count <= sizeof(input_bits) * 8);
    for (unsigned bit = 0; bit < count; ++bit) {
        input_bits[input_count / 8] |= ((value >> bit) & 1u) << (input_count % 8);
        ++input_count;
    }
}
static void read_reply(uint32_t value)
{
    append_bits(1, 3);
    append_bits(value, 32);
    append_bits(__builtin_parity(value), 1);
}
static void posted_write(void) { append_bits(1, 3); read_reply(0); }
static void memory_write(void) { posted_write(); posted_write(); }
static void memory_read(uint32_t value) { posted_write(); read_reply(0); read_reply(value); }
static void attach_reply(void)
{
    append_bits(7, 3);
    read_reply(0x0bc12477);
    append_bits(1, 3);
    append_bits(1, 3);
    append_bits(1, 3);
    read_reply(0xa0000000);
    posted_write();
}
static void reset_replies(uint32_t reason, uint32_t core0, uint32_t core1)
{
    attach_reply(); memory_write();
    attach_reply(); memory_write();
    memory_write(); memory_write(); posted_write();
    append_bits(1, 3);
    attach_reply(); memory_read(reason);
    if ((reason & 2u) == 0) return;
    memory_read(core0);
    if (core0 & (1u << 17)) return;
    attach_reply(); memory_read(core1);
}
static void reset_fixture(void)
{
    memset(input_bits, 0, sizeof(input_bits));
    input_count = input_cursor = configured = 0;
    output_count = 0;
    output_window = 0;
    memset(observed_writes, 0, sizeof(observed_writes));
    clock_level = data_level = false;
    configure_result = ESP_OK;
    clock_mode = data_mode = GPIO_MODE_DISABLE;
}

static bool wrote_word(uint8_t request, uint32_t word)
{
    for (unsigned i = 0; i < sizeof(expected_writes) / sizeof(expected_writes[0]); ++i)
        if (expected_writes[i].request == request && expected_writes[i].word == word) return observed_writes[i];
    return false;
}
static void pins_released(void)
{
    TEST_ASSERT_EQUAL(GPIO_MODE_INPUT, clock_mode);
    TEST_ASSERT_EQUAL(GPIO_MODE_INPUT, data_mode);
    TEST_ASSERT_EQUAL_UINT(input_count, input_cursor);
}

TEST_CASE("Bridge watchdog reset verifies its reason and both running cores", "[asic][bonanza][bridge]")
{
    reset_fixture();
    reset_replies(2, 0, 0);
    TEST_ASSERT_EQUAL(ESP_OK, reset_test_bridge(GPIO_NUM_14, GPIO_NUM_15));
    TEST_ASSERT_EQUAL_HEX8(0xa5, swd_request(false, true, 0));
    TEST_ASSERT_EQUAL_HEX8(0x99, swd_request(false, false, 0x0c));
    TEST_ASSERT_EQUAL_HEX8(0x9f, swd_request(true, true, 0x0c));
    TEST_ASSERT_TRUE(wrote_word(0x99, CORE0));
    TEST_ASSERT_TRUE(wrote_word(0x99, CORE1));
    TEST_ASSERT_TRUE(wrote_word(0x8b, 0x40010008));
    TEST_ASSERT_TRUE(wrote_word(0xbb, 0x0001fffc));
    TEST_ASSERT_TRUE(wrote_word(0x8b, 0x40058000));
    TEST_ASSERT_TRUE(wrote_word(0xbb, 0x80000000));
    pins_released();
}

TEST_CASE("Bridge watchdog reset rejects unconfirmed reset and halted cores", "[asic][bonanza][bridge]")
{
    for (unsigned failure = 0; failure < 3; ++failure) {
        reset_fixture();
        reset_replies(failure == 0 ? 0 : 2, failure == 1 ? 1u << 17 : 0, failure == 2 ? 1u << 17 : 0);
        TEST_ASSERT_NOT_EQUAL(ESP_OK, reset_test_bridge(GPIO_NUM_14, GPIO_NUM_15));
        pins_released();
    }
}

TEST_CASE("Bridge SWD retries WAIT finitely and releases pins on failure", "[asic][bonanza][bridge]")
{
    for (unsigned failure = 0; failure < 3; ++failure) {
        reset_fixture();
        append_bits(7, 3);
        if (failure == 0) {
            for (unsigned retry = 0; retry < 16; ++retry) append_bits(2, 3);
        } else if (failure == 1) {
            read_reply(0x0bc12477);
            input_bits[(input_count - 1) / 8] ^= 1u << ((input_count - 1) % 8);
        } else {
            read_reply(0x12345678);
        }
        TEST_ASSERT_NOT_EQUAL(ESP_OK, reset_test_bridge(GPIO_NUM_14, GPIO_NUM_15));
        pins_released();
    }
}

TEST_CASE("Bridge reset rejects invalid pins and handles GPIO setup failure", "[asic][bonanza][bridge]")
{
    reset_fixture();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, reset_test_bridge(GPIO_NUM_14, GPIO_NUM_14));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, reset_test_bridge(GPIO_NUM_NC, GPIO_NUM_15));
    TEST_ASSERT_EQUAL_UINT32(0, configured);
    configure_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, reset_test_bridge(GPIO_NUM_14, GPIO_NUM_15));
    pins_released();
}
