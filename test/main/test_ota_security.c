#include "unity.h"
#include "esp_timer.h"
#include "../../main/nvs_config.h"

static int64_t test_time_us;
static bool button_required;

static int64_t test_get_time(void)
{
    return test_time_us;
}

static bool test_get_bool(NvsConfigKey key)
{
    TEST_ASSERT_EQUAL(NVS_CONFIG_OTA_BUTTON_REQUIRED, key);
    return button_required;
}

#define esp_timer_get_time test_get_time
#define nvs_config_get_bool test_get_bool
#include "../../main/ota_security.c"
#undef esp_timer_get_time
#undef nvs_config_get_bool

static void reset_security(void)
{
    test_time_us = 0;
    button_required = true;
    ota_security_lock();
}

TEST_CASE("OTA is locked until a physical button press", "[ota_security]")
{
    reset_security();
    TEST_ASSERT_FALSE(ota_security_is_update_allowed());
    TEST_ASSERT_EQUAL_UINT32(0, ota_security_remaining_seconds());
    ota_security_unlock_from_button();
    TEST_ASSERT_TRUE(ota_security_is_update_allowed());
    TEST_ASSERT_EQUAL_UINT32(120, ota_security_remaining_seconds());
}

TEST_CASE("OTA unlock expires exactly after two minutes", "[ota_security]")
{
    reset_security();
    ota_security_unlock_from_button();
    test_time_us = 120LL * 1000000 - 1;
    TEST_ASSERT_TRUE(ota_security_is_update_allowed());
    TEST_ASSERT_EQUAL_UINT32(1, ota_security_remaining_seconds());
    test_time_us++;
    TEST_ASSERT_FALSE(ota_security_is_update_allowed());
    TEST_ASSERT_EQUAL_UINT32(0, ota_security_remaining_seconds());
}

TEST_CASE("Another button press renews the OTA window after long uptime", "[ota_security]")
{
    reset_security();
    test_time_us = 5000000000LL;
    ota_security_unlock_from_button();
    test_time_us += 119LL * 1000000;
    ota_security_unlock_from_button();
    test_time_us += 2LL * 1000000;
    TEST_ASSERT_TRUE(ota_security_is_update_allowed());
    TEST_ASSERT_EQUAL_UINT32(118, ota_security_remaining_seconds());
}

TEST_CASE("OTA protection opt-out allows remote updates without a press", "[ota_security]")
{
    reset_security();
    button_required = false;
    TEST_ASSERT_TRUE(ota_security_is_update_allowed());
    TEST_ASSERT_EQUAL_UINT32(0, ota_security_remaining_seconds());
    button_required = true;
    TEST_ASSERT_FALSE(ota_security_is_update_allowed());
}

TEST_CASE("Relocking OTA clears a previous physical unlock", "[ota_security]")
{
    reset_security();
    ota_security_unlock_from_button();
    ota_security_lock();
    TEST_ASSERT_FALSE(ota_security_is_update_allowed());
    TEST_ASSERT_EQUAL_UINT32(0, ota_security_remaining_seconds());
}
