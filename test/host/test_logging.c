/**
 * @file test_logging.c
 * @brief Unit tests for unified logging module
 *
 * Tests log routing, filtering, configuration, and backend injection.
 */

#include "unity.h"
#include "logging.h"
#include <string.h>
#include <stdio.h>

/* Test group runner */
void run_logging_tests(void);

/* ============================================
 * Mock Backend for Testing
 * ============================================ */

/* Captured log data */
static char s_last_category[64];
static char s_last_level[64];
static char s_last_message[256];
static char s_last_json_data[256];
static int s_message_count = 0;
static int s_event_count = 0;

static void mock_reset(void)
{
    s_last_category[0] = '\0';
    s_last_level[0] = '\0';
    s_last_message[0] = '\0';
    s_last_json_data[0] = '\0';
    s_message_count = 0;
    s_event_count = 0;
}

static void mock_write_message(const char* category, const char* level,
                                const char* message)
{
    if (category) strncpy(s_last_category, category, sizeof(s_last_category) - 1);
    if (level) strncpy(s_last_level, level, sizeof(s_last_level) - 1);
    if (message) strncpy(s_last_message, message, sizeof(s_last_message) - 1);
    s_message_count++;
}

static void mock_write_event(const char* category, const char* level,
                              const char* message, const char* json_data)
{
    if (category) strncpy(s_last_category, category, sizeof(s_last_category) - 1);
    if (level) strncpy(s_last_level, level, sizeof(s_last_level) - 1);
    if (message) strncpy(s_last_message, message, sizeof(s_last_message) - 1);
    if (json_data) strncpy(s_last_json_data, json_data, sizeof(s_last_json_data) - 1);
    else s_last_json_data[0] = '\0';
    s_event_count++;
}

static const log_backend_ops_t s_mock_backend = {
    .write_message = mock_write_message,
    .write_event = mock_write_event
};

/* ============================================
 * Test Fixtures
 * ============================================ */

static void setup_mock_backends(void)
{
    mock_reset();
    log_init();
    /* Use mock for both serial and database during tests */
    log_set_backend(LOG_DEST_SERIAL, &s_mock_backend);
    log_set_backend(LOG_DEST_DATABASE, &s_mock_backend);
}

/* ============================================
 * Utility Function Tests
 * ============================================ */

static void test_log_level_to_string_valid(void)
{
    TEST_ASSERT_EQUAL_STRING("none", log_level_to_string(LOG_LEVEL_NONE));
    TEST_ASSERT_EQUAL_STRING("error", log_level_to_string(LOG_LEVEL_ERROR));
    TEST_ASSERT_EQUAL_STRING("warn", log_level_to_string(LOG_LEVEL_WARN));
    TEST_ASSERT_EQUAL_STRING("info", log_level_to_string(LOG_LEVEL_INFO));
    TEST_ASSERT_EQUAL_STRING("debug", log_level_to_string(LOG_LEVEL_DEBUG));
    TEST_ASSERT_EQUAL_STRING("trace", log_level_to_string(LOG_LEVEL_TRACE));
}

static void test_log_level_to_string_invalid(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", log_level_to_string((log_level_t)99));
}

static void test_log_category_to_string_valid(void)
{
    TEST_ASSERT_EQUAL_STRING("system", log_category_to_string(LOG_CAT_SYSTEM));
    TEST_ASSERT_EQUAL_STRING("power", log_category_to_string(LOG_CAT_POWER));
    TEST_ASSERT_EQUAL_STRING("mining", log_category_to_string(LOG_CAT_MINING));
    TEST_ASSERT_EQUAL_STRING("network", log_category_to_string(LOG_CAT_NETWORK));
    TEST_ASSERT_EQUAL_STRING("asic", log_category_to_string(LOG_CAT_ASIC));
    TEST_ASSERT_EQUAL_STRING("api", log_category_to_string(LOG_CAT_API));
    TEST_ASSERT_EQUAL_STRING("theme", log_category_to_string(LOG_CAT_THEME));
    TEST_ASSERT_EQUAL_STRING("settings", log_category_to_string(LOG_CAT_SETTINGS));
}

static void test_log_category_to_string_invalid(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", log_category_to_string((log_category_t)99));
}

static void test_log_level_from_string_valid(void)
{
    TEST_ASSERT_EQUAL(LOG_LEVEL_ERROR, log_level_from_string("error"));
    TEST_ASSERT_EQUAL(LOG_LEVEL_WARN, log_level_from_string("warn"));
    TEST_ASSERT_EQUAL(LOG_LEVEL_INFO, log_level_from_string("info"));
    TEST_ASSERT_EQUAL(LOG_LEVEL_DEBUG, log_level_from_string("debug"));
    TEST_ASSERT_EQUAL(LOG_LEVEL_TRACE, log_level_from_string("trace"));
}

static void test_log_level_from_string_case_insensitive(void)
{
    TEST_ASSERT_EQUAL(LOG_LEVEL_ERROR, log_level_from_string("ERROR"));
    TEST_ASSERT_EQUAL(LOG_LEVEL_WARN, log_level_from_string("WARN"));
    TEST_ASSERT_EQUAL(LOG_LEVEL_INFO, log_level_from_string("Info"));
    TEST_ASSERT_EQUAL(LOG_LEVEL_DEBUG, log_level_from_string("DeBuG"));
}

static void test_log_level_from_string_invalid(void)
{
    TEST_ASSERT_EQUAL(LOG_LEVEL_INFO, log_level_from_string("invalid"));
    TEST_ASSERT_EQUAL(LOG_LEVEL_INFO, log_level_from_string(NULL));
}

static void test_log_category_from_string_valid(void)
{
    TEST_ASSERT_EQUAL(LOG_CAT_SYSTEM, log_category_from_string("system"));
    TEST_ASSERT_EQUAL(LOG_CAT_POWER, log_category_from_string("power"));
    TEST_ASSERT_EQUAL(LOG_CAT_MINING, log_category_from_string("mining"));
    TEST_ASSERT_EQUAL(LOG_CAT_NETWORK, log_category_from_string("network"));
}

static void test_log_category_from_string_invalid(void)
{
    TEST_ASSERT_EQUAL(LOG_CAT_SYSTEM, log_category_from_string("invalid"));
    TEST_ASSERT_EQUAL(LOG_CAT_SYSTEM, log_category_from_string(NULL));
}

/* ============================================
 * Configuration Tests
 * ============================================ */

static void test_log_init_sets_defaults(void)
{
    log_init();

    log_category_config_t config = log_get_config(LOG_CAT_POWER);
    TEST_ASSERT_EQUAL(LOG_LEVEL_WARN, config.serial_level);
    TEST_ASSERT_EQUAL(LOG_LEVEL_ERROR, config.database_level);
    TEST_ASSERT_TRUE(config.destinations & LOG_DEST_SERIAL);
    TEST_ASSERT_TRUE(config.destinations & LOG_DEST_DATABASE);
}

static void test_log_set_level_serial(void)
{
    log_init();

    log_set_level(LOG_CAT_POWER, LOG_DEST_SERIAL, LOG_LEVEL_DEBUG);

    log_category_config_t config = log_get_config(LOG_CAT_POWER);
    TEST_ASSERT_EQUAL(LOG_LEVEL_DEBUG, config.serial_level);
    TEST_ASSERT_EQUAL(LOG_LEVEL_ERROR, config.database_level); /* Unchanged */
}

static void test_log_set_level_database(void)
{
    log_init();

    log_set_level(LOG_CAT_MINING, LOG_DEST_DATABASE, LOG_LEVEL_INFO);

    log_category_config_t config = log_get_config(LOG_CAT_MINING);
    TEST_ASSERT_EQUAL(LOG_LEVEL_WARN, config.serial_level); /* Unchanged */
    TEST_ASSERT_EQUAL(LOG_LEVEL_INFO, config.database_level);
}

static void test_log_set_destinations(void)
{
    log_init();

    log_set_destinations(LOG_CAT_API, LOG_DEST_SERIAL);

    log_category_config_t config = log_get_config(LOG_CAT_API);
    TEST_ASSERT_TRUE(config.destinations & LOG_DEST_SERIAL);
    TEST_ASSERT_FALSE(config.destinations & LOG_DEST_DATABASE);
}

static void test_log_get_config_invalid_category(void)
{
    log_init();

    /* Should return defaults for invalid category */
    log_category_config_t config = log_get_config((log_category_t)99);
    TEST_ASSERT_EQUAL(LOG_LEVEL_WARN, config.serial_level);
    TEST_ASSERT_EQUAL(LOG_LEVEL_ERROR, config.database_level);
}

/* ============================================
 * Message Routing Tests
 * ============================================ */

static void test_log_message_routes_to_serial_when_level_meets_threshold(void)
{
    setup_mock_backends();

    /* Default serial level is WARN, so ERROR should be logged */
    log_message(LOG_CAT_POWER, LOG_LEVEL_ERROR, "Test error message");

    TEST_ASSERT_EQUAL(1, s_message_count);
    TEST_ASSERT_EQUAL_STRING("power", s_last_category);
    TEST_ASSERT_EQUAL_STRING("error", s_last_level);
    TEST_ASSERT_EQUAL_STRING("Test error message", s_last_message);
}

static void test_log_message_filtered_when_level_below_threshold(void)
{
    setup_mock_backends();

    /* Default serial level is WARN, so DEBUG should be filtered */
    log_message(LOG_CAT_POWER, LOG_LEVEL_DEBUG, "Test debug message");

    TEST_ASSERT_EQUAL(0, s_message_count);
}

static void test_log_message_with_formatting(void)
{
    setup_mock_backends();

    log_message(LOG_CAT_MINING, LOG_LEVEL_ERROR, "Value: %d, String: %s", 42, "test");

    TEST_ASSERT_EQUAL(1, s_message_count);
    TEST_ASSERT_EQUAL_STRING("Value: 42, String: test", s_last_message);
}

static void test_log_macros_work_correctly(void)
{
    setup_mock_backends();

    LOG_ERROR(LOG_CAT_SYSTEM, "Error message");
    TEST_ASSERT_EQUAL(1, s_message_count);
    TEST_ASSERT_EQUAL_STRING("error", s_last_level);

    mock_reset();
    LOG_WARN(LOG_CAT_SYSTEM, "Warning message");
    TEST_ASSERT_EQUAL(1, s_message_count);
    TEST_ASSERT_EQUAL_STRING("warn", s_last_level);
}

static void test_log_message_not_sent_when_destination_disabled(void)
{
    setup_mock_backends();

    /* Disable serial for this category */
    log_set_destinations(LOG_CAT_NETWORK, LOG_DEST_DATABASE);

    /* Even ERROR should not go to serial */
    log_message(LOG_CAT_NETWORK, LOG_LEVEL_ERROR, "Test message");

    /* Message count will still be 1 from database backend (using same mock) */
    TEST_ASSERT_EQUAL(1, s_message_count);
    /* Restore default for other tests */
    log_set_destinations(LOG_CAT_NETWORK, LOG_DEST_SERIAL | LOG_DEST_DATABASE);
}

/* ============================================
 * Event Routing Tests
 * ============================================ */

static void test_log_event_includes_json_data(void)
{
    setup_mock_backends();

    log_event(LOG_CAT_SETTINGS, LOG_LEVEL_INFO,
              "Settings changed", "{\"key\":\"voltage\",\"value\":1200}");

    TEST_ASSERT_EQUAL(1, s_event_count);
    TEST_ASSERT_EQUAL_STRING("settings", s_last_category);
    TEST_ASSERT_EQUAL_STRING("info", s_last_level);
    TEST_ASSERT_EQUAL_STRING("Settings changed", s_last_message);
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"voltage\",\"value\":1200}", s_last_json_data);
}

static void test_log_event_with_null_json(void)
{
    setup_mock_backends();

    log_event(LOG_CAT_SYSTEM, LOG_LEVEL_INFO, "Simple event", NULL);

    TEST_ASSERT_EQUAL(1, s_event_count);
    TEST_ASSERT_EQUAL_STRING("Simple event", s_last_message);
    TEST_ASSERT_EQUAL_STRING("", s_last_json_data);
}

static void test_log_event_always_goes_to_database(void)
{
    setup_mock_backends();

    /* Set database level to ERROR */
    log_set_level(LOG_CAT_THEME, LOG_DEST_DATABASE, LOG_LEVEL_ERROR);

    /* Events should still go to database even at INFO level */
    log_event(LOG_CAT_THEME, LOG_LEVEL_INFO, "Theme changed", NULL);

    /* At least 1 event (from database - events bypass level filter for database) */
    TEST_ASSERT_TRUE(s_event_count >= 1);
}

/* ============================================
 * Backend Injection Tests
 * ============================================ */

static int s_custom_call_count = 0;

static void custom_write_message(const char* category, const char* level,
                                  const char* message)
{
    (void)category;
    (void)level;
    (void)message;
    s_custom_call_count++;
}

static void custom_write_event(const char* category, const char* level,
                                const char* message, const char* json_data)
{
    (void)category;
    (void)level;
    (void)message;
    (void)json_data;
    s_custom_call_count++;
}

static const log_backend_ops_t s_custom_backend = {
    .write_message = custom_write_message,
    .write_event = custom_write_event
};

static void test_log_set_backend_replaces_default(void)
{
    log_init();
    s_custom_call_count = 0;

    /* Set custom backend for serial */
    log_set_backend(LOG_DEST_SERIAL, &s_custom_backend);

    log_message(LOG_CAT_POWER, LOG_LEVEL_ERROR, "Test");

    TEST_ASSERT_TRUE(s_custom_call_count >= 1);

    /* Restore NULL to use default */
    log_set_backend(LOG_DEST_SERIAL, NULL);
}

static void test_log_set_backend_null_restores_default(void)
{
    log_init();

    /* Set then clear */
    log_set_backend(LOG_DEST_SERIAL, &s_custom_backend);
    log_set_backend(LOG_DEST_SERIAL, NULL);

    /* Should not crash when logging - default backend used */
    log_message(LOG_CAT_POWER, LOG_LEVEL_ERROR, "Test");
    /* No assertion - just testing no crash */
}

static void test_log_get_default_backend_serial(void)
{
    log_init();

    const log_backend_ops_t* backend = log_get_default_backend(LOG_DEST_SERIAL);

    TEST_ASSERT_NOT_NULL(backend);
    TEST_ASSERT_NOT_NULL(backend->write_message);
    TEST_ASSERT_NOT_NULL(backend->write_event);
}

/* ============================================
 * Edge Case Tests
 * ============================================ */

static void test_log_message_invalid_category_ignored(void)
{
    setup_mock_backends();

    log_message((log_category_t)99, LOG_LEVEL_ERROR, "Invalid category");

    TEST_ASSERT_EQUAL(0, s_message_count);
}

static void test_log_message_level_none_ignored(void)
{
    setup_mock_backends();

    log_message(LOG_CAT_POWER, LOG_LEVEL_NONE, "Level none");

    TEST_ASSERT_EQUAL(0, s_message_count);
}

static void test_log_event_invalid_category_ignored(void)
{
    setup_mock_backends();

    log_event((log_category_t)99, LOG_LEVEL_ERROR, "Invalid", NULL);

    TEST_ASSERT_EQUAL(0, s_event_count);
}

/* ============================================
 * Test Runner
 * ============================================ */

void run_logging_tests(void)
{
    /* Utility function tests */
    RUN_TEST(test_log_level_to_string_valid);
    RUN_TEST(test_log_level_to_string_invalid);
    RUN_TEST(test_log_category_to_string_valid);
    RUN_TEST(test_log_category_to_string_invalid);
    RUN_TEST(test_log_level_from_string_valid);
    RUN_TEST(test_log_level_from_string_case_insensitive);
    RUN_TEST(test_log_level_from_string_invalid);
    RUN_TEST(test_log_category_from_string_valid);
    RUN_TEST(test_log_category_from_string_invalid);

    /* Configuration tests */
    RUN_TEST(test_log_init_sets_defaults);
    RUN_TEST(test_log_set_level_serial);
    RUN_TEST(test_log_set_level_database);
    RUN_TEST(test_log_set_destinations);
    RUN_TEST(test_log_get_config_invalid_category);

    /* Message routing tests */
    RUN_TEST(test_log_message_routes_to_serial_when_level_meets_threshold);
    RUN_TEST(test_log_message_filtered_when_level_below_threshold);
    RUN_TEST(test_log_message_with_formatting);
    RUN_TEST(test_log_macros_work_correctly);
    RUN_TEST(test_log_message_not_sent_when_destination_disabled);

    /* Event routing tests */
    RUN_TEST(test_log_event_includes_json_data);
    RUN_TEST(test_log_event_with_null_json);
    RUN_TEST(test_log_event_always_goes_to_database);

    /* Backend injection tests */
    RUN_TEST(test_log_set_backend_replaces_default);
    RUN_TEST(test_log_set_backend_null_restores_default);
    RUN_TEST(test_log_get_default_backend_serial);

    /* Edge case tests */
    RUN_TEST(test_log_message_invalid_category_ignored);
    RUN_TEST(test_log_message_level_none_ignored);
    RUN_TEST(test_log_event_invalid_category_ignored);
}
