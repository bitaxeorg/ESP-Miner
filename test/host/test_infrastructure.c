/**
 * @file test_infrastructure.c
 * @brief Tests to verify the test infrastructure is working correctly
 *
 * These tests validate that:
 * - Unity framework is properly configured
 * - Mock NVS works correctly
 * - Fake time is controllable
 * - ESP-IDF stubs compile and link
 */

#include "unity.h"
#include "mock_nvs.h"
#include "fake_time.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "test_infra";

/* ============================================
 * Unity Framework Tests
 * ============================================ */

static void test_unity_true_is_true(void)
{
    TEST_ASSERT_TRUE(1);
}

static void test_unity_false_is_false(void)
{
    TEST_ASSERT_FALSE(0);
}

static void test_unity_integers_equal(void)
{
    TEST_ASSERT_EQUAL_INT(42, 42);
}

static void test_unity_floats_equal(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14159f, 3.14159f);
}

static void test_unity_strings_equal(void)
{
    TEST_ASSERT_EQUAL_STRING("hello", "hello");
}

/* ============================================
 * Mock NVS Tests
 * ============================================ */

static void test_mock_nvs_init(void)
{
    mock_nvs_init();
    TEST_ASSERT_EQUAL_INT(0, mock_nvs_get_commit_count());
}

static void test_mock_nvs_set_and_get_u16(void)
{
    mock_nvs_init();

    nvs_handle_t handle;
    TEST_ASSERT_EQUAL_INT(0, nvs_open("test_ns", NVS_READWRITE, &handle));

    TEST_ASSERT_EQUAL_INT(0, nvs_set_u16(handle, "test_key", 12345));
    TEST_ASSERT_EQUAL_INT(0, nvs_commit(handle));

    uint16_t value;
    TEST_ASSERT_EQUAL_INT(0, nvs_get_u16(handle, "test_key", &value));
    TEST_ASSERT_EQUAL_UINT16(12345, value);

    nvs_close(handle);
}

static void test_mock_nvs_set_and_get_string(void)
{
    mock_nvs_init();

    nvs_handle_t handle;
    TEST_ASSERT_EQUAL_INT(0, nvs_open("test_ns", NVS_READWRITE, &handle));

    const char* test_str = "Hello, NVS!";
    TEST_ASSERT_EQUAL_INT(0, nvs_set_str(handle, "str_key", test_str));

    char buffer[64];
    size_t len = sizeof(buffer);
    TEST_ASSERT_EQUAL_INT(0, nvs_get_str(handle, "str_key", buffer, &len));
    TEST_ASSERT_EQUAL_STRING(test_str, buffer);

    nvs_close(handle);
}

static void test_mock_nvs_not_found(void)
{
    mock_nvs_init();

    nvs_handle_t handle;
    TEST_ASSERT_EQUAL_INT(0, nvs_open("test_ns", NVS_READWRITE, &handle));

    uint16_t value;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NVS_NOT_FOUND, nvs_get_u16(handle, "nonexistent", &value));

    nvs_close(handle);
}

static void test_mock_nvs_write_tracking(void)
{
    mock_nvs_init();

    nvs_handle_t handle;
    nvs_open("test_ns", NVS_READWRITE, &handle);

    /* Key hasn't been written yet */
    TEST_ASSERT_FALSE(mock_nvs_was_written("test_ns", "tracked_key"));

    nvs_set_u16(handle, "tracked_key", 100);

    /* Now it should be marked as written */
    TEST_ASSERT_TRUE(mock_nvs_was_written("test_ns", "tracked_key"));
    TEST_ASSERT_EQUAL_INT(1, mock_nvs_get_write_count("test_ns", "tracked_key"));

    /* Write again */
    nvs_set_u16(handle, "tracked_key", 200);
    TEST_ASSERT_EQUAL_INT(2, mock_nvs_get_write_count("test_ns", "tracked_key"));

    nvs_close(handle);
}

static void test_mock_nvs_prepopulate(void)
{
    mock_nvs_init();

    /* Pre-populate values (simulating existing config) */
    mock_nvs_set_u16("config", "asic_voltage", 1200);
    mock_nvs_set_string("config", "pool_url", "stratum+tcp://pool.example.com");

    /* Now open and read */
    nvs_handle_t handle;
    nvs_open("config", NVS_READONLY, &handle);

    uint16_t voltage;
    TEST_ASSERT_EQUAL_INT(0, nvs_get_u16(handle, "asic_voltage", &voltage));
    TEST_ASSERT_EQUAL_UINT16(1200, voltage);

    char url[128];
    size_t len = sizeof(url);
    TEST_ASSERT_EQUAL_INT(0, nvs_get_str(handle, "pool_url", url, &len));
    TEST_ASSERT_EQUAL_STRING("stratum+tcp://pool.example.com", url);

    nvs_close(handle);
}

/* ============================================
 * Fake Time Tests
 * ============================================ */

static void test_fake_time_init(void)
{
    fake_time_init();
    TEST_ASSERT_EQUAL_INT64(0, fake_time_get_us());
    TEST_ASSERT_EQUAL_UINT32(0, fake_time_get_ticks());
}

static void test_fake_time_set_and_get(void)
{
    fake_time_init();
    fake_time_set_us(1000000);  /* 1 second */
    TEST_ASSERT_EQUAL_INT64(1000000, fake_time_get_us());
}

static void test_fake_time_advance(void)
{
    fake_time_init();

    fake_time_advance_ms(100);  /* 100ms */
    TEST_ASSERT_EQUAL_INT64(100000, fake_time_get_us());

    fake_time_advance_sec(1);   /* 1 second */
    TEST_ASSERT_EQUAL_INT64(1100000, fake_time_get_us());
}

static void test_fake_time_ticks(void)
{
    fake_time_init();

    fake_time_set_us(5000000);  /* 5 seconds = 5000ms = 5000 ticks */
    TEST_ASSERT_EQUAL_UINT32(5000, fake_time_get_ticks());
}

/* ============================================
 * ESP-IDF Stub Tests
 * ============================================ */

static void test_esp_err_to_name(void)
{
    TEST_ASSERT_EQUAL_STRING("ESP_OK", esp_err_to_name(ESP_OK));
    TEST_ASSERT_EQUAL_STRING("ESP_FAIL", esp_err_to_name(ESP_FAIL));
    TEST_ASSERT_EQUAL_STRING("ESP_ERR_NO_MEM", esp_err_to_name(ESP_ERR_NO_MEM));
    TEST_ASSERT_EQUAL_STRING("ESP_ERR_INVALID_ARG", esp_err_to_name(ESP_ERR_INVALID_ARG));
}

static void test_freertos_tick_macros(void)
{
    /* Test tick conversion macros */
    TEST_ASSERT_EQUAL_UINT32(1000, pdMS_TO_TICKS(1000));  /* 1000ms = 1000 ticks at 1kHz */
    TEST_ASSERT_EQUAL_UINT32(500, pdMS_TO_TICKS(500));
}

static void test_esp_log_compiles(void)
{
    /* Just verify that log macros compile and don't crash */
    ESP_LOGI(TAG, "Info message: %d", 42);
    ESP_LOGW(TAG, "Warning message");
    ESP_LOGE(TAG, "Error message");
    TEST_PASS();
}

/* ============================================
 * Test Suite Runner
 * ============================================ */

void run_infrastructure_tests(void)
{
    /* Unity Framework Tests */
    RUN_TEST(test_unity_true_is_true);
    RUN_TEST(test_unity_false_is_false);
    RUN_TEST(test_unity_integers_equal);
    RUN_TEST(test_unity_floats_equal);
    RUN_TEST(test_unity_strings_equal);

    /* Mock NVS Tests */
    RUN_TEST(test_mock_nvs_init);
    RUN_TEST(test_mock_nvs_set_and_get_u16);
    RUN_TEST(test_mock_nvs_set_and_get_string);
    RUN_TEST(test_mock_nvs_not_found);
    RUN_TEST(test_mock_nvs_write_tracking);
    RUN_TEST(test_mock_nvs_prepopulate);

    /* Fake Time Tests */
    RUN_TEST(test_fake_time_init);
    RUN_TEST(test_fake_time_set_and_get);
    RUN_TEST(test_fake_time_advance);
    RUN_TEST(test_fake_time_ticks);

    /* ESP-IDF Stub Tests */
    RUN_TEST(test_esp_err_to_name);
    RUN_TEST(test_freertos_tick_macros);
    RUN_TEST(test_esp_log_compiles);
}
