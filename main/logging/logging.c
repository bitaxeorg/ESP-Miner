/**
 * @file logging.c
 * @brief Unified logging implementation
 *
 * Routes log messages to multiple destinations based on category
 * and level configuration. Thread-safe via FreeRTOS mutex.
 */

#include "logging.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "database/dataBase.h"
#else
/* Host testing - use stubs */
#include "esp_log.h"
#include <pthread.h>
#endif

/* ============================================
 * Constants
 * ============================================ */

#define LOG_MSG_MAX_LEN 256
static const char* TAG = "logging";

/* Category name strings - must match log_category_t order */
static const char* const s_category_names[] = {
    "system",
    "power",
    "mining",
    "network",
    "asic",
    "api",
    "theme",
    "settings"
};

/* Level name strings - must match log_level_t order */
static const char* const s_level_names[] = {
    "none",
    "error",
    "warn",
    "info",
    "debug",
    "trace"
};

/* ============================================
 * State
 * ============================================ */

/* Per-category configuration */
static log_category_config_t s_config[LOG_CAT_COUNT];

/* Custom backends (NULL = use default) */
static const log_backend_ops_t* s_serial_backend = NULL;
static const log_backend_ops_t* s_database_backend = NULL;

/* Thread safety */
#ifdef ESP_PLATFORM
static SemaphoreHandle_t s_mutex = NULL;
#else
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static bool s_initialized = false;

/* ============================================
 * Mutex Helpers
 * ============================================ */

static void lock(void)
{
#ifdef ESP_PLATFORM
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
#else
    pthread_mutex_lock(&s_mutex);
#endif
}

static void unlock(void)
{
#ifdef ESP_PLATFORM
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
#else
    pthread_mutex_unlock(&s_mutex);
#endif
}

/* ============================================
 * Default Backends
 * ============================================ */

/**
 * @brief Serial backend - wraps ESP_LOG macros
 */
static void serial_write_message(const char* category, const char* level,
                                  const char* message)
{
    /* Map level string to ESP_LOG call */
    if (strcmp(level, "error") == 0) {
        ESP_LOGE(category, "%s", message);
    } else if (strcmp(level, "warn") == 0) {
        ESP_LOGW(category, "%s", message);
    } else if (strcmp(level, "info") == 0) {
        ESP_LOGI(category, "%s", message);
    } else if (strcmp(level, "debug") == 0) {
        ESP_LOGD(category, "%s", message);
    } else {
        ESP_LOGV(category, "%s", message);
    }
}

static void serial_write_event(const char* category, const char* level,
                                const char* message, const char* json_data)
{
    if (json_data && json_data[0]) {
        char buf[LOG_MSG_MAX_LEN];
        snprintf(buf, sizeof(buf), "%s | %s", message, json_data);
        serial_write_message(category, level, buf);
    } else {
        serial_write_message(category, level, message);
    }
}

static const log_backend_ops_t s_default_serial_backend = {
    .write_message = serial_write_message,
    .write_event = serial_write_event
};

/**
 * @brief Database backend - wraps dataBase_log_event
 */
#ifdef ESP_PLATFORM
static void database_write_message(const char* category, const char* level,
                                    const char* message)
{
    dataBase_log_event(category, level, message, NULL);
}

static void database_write_event(const char* category, const char* level,
                                  const char* message, const char* json_data)
{
    dataBase_log_event(category, level, message, json_data);
}

static const log_backend_ops_t s_default_database_backend = {
    .write_message = database_write_message,
    .write_event = database_write_event
};
#else
/* Host testing - no database backend by default */
static const log_backend_ops_t* const s_default_database_backend_ptr = NULL;
#endif

/* ============================================
 * Public API Implementation
 * ============================================ */

int log_init(void)
{
    if (s_initialized) {
        return 0;
    }

#ifdef ESP_PLATFORM
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create logging mutex");
        return -1;
    }
#endif

    /* Set conservative defaults per owner requirements:
     * - Serial: WARN and above
     * - Database: ERROR and above
     */
    for (int i = 0; i < LOG_CAT_COUNT; i++) {
        s_config[i].serial_level = LOG_LEVEL_WARN;
        s_config[i].database_level = LOG_LEVEL_ERROR;
        s_config[i].destinations = LOG_DEST_SERIAL | LOG_DEST_DATABASE;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Unified logging initialized");
    return 0;
}

void log_set_level(log_category_t category, log_destination_t destination,
                   log_level_t level)
{
    if (category >= LOG_CAT_COUNT) {
        return;
    }

    lock();
    if (destination & LOG_DEST_SERIAL) {
        s_config[category].serial_level = level;
    }
    if (destination & LOG_DEST_DATABASE) {
        s_config[category].database_level = level;
    }
    unlock();
}

void log_set_destinations(log_category_t category, log_destination_t destinations)
{
    if (category >= LOG_CAT_COUNT) {
        return;
    }

    lock();
    s_config[category].destinations = destinations;
    unlock();
}

log_category_config_t log_get_config(log_category_t category)
{
    log_category_config_t config = {
        .serial_level = LOG_LEVEL_WARN,
        .database_level = LOG_LEVEL_ERROR,
        .destinations = LOG_DEST_SERIAL | LOG_DEST_DATABASE
    };

    if (category < LOG_CAT_COUNT) {
        lock();
        config = s_config[category];
        unlock();
    }

    return config;
}

void log_message_v(log_category_t category, log_level_t level,
                   const char* format, va_list args)
{
    if (!s_initialized || category >= LOG_CAT_COUNT || level == LOG_LEVEL_NONE) {
        return;
    }

    /* Format the message */
    char message[LOG_MSG_MAX_LEN];
    vsnprintf(message, sizeof(message), format, args);

    const char* cat_str = log_category_to_string(category);
    const char* level_str = log_level_to_string(level);

    lock();
    log_category_config_t config = s_config[category];
    unlock();

    /* Route to serial if enabled and level meets threshold */
    if ((config.destinations & LOG_DEST_SERIAL) && level <= config.serial_level) {
        const log_backend_ops_t* backend = s_serial_backend ? s_serial_backend
                                                            : &s_default_serial_backend;
        if (backend->write_message) {
            backend->write_message(cat_str, level_str, message);
        }
    }

    /* Route to database if enabled and level meets threshold */
    if ((config.destinations & LOG_DEST_DATABASE) && level <= config.database_level) {
#ifdef ESP_PLATFORM
        const log_backend_ops_t* backend = s_database_backend ? s_database_backend
                                                               : &s_default_database_backend;
        if (backend && backend->write_message) {
            backend->write_message(cat_str, level_str, message);
        }
#else
        if (s_database_backend && s_database_backend->write_message) {
            s_database_backend->write_message(cat_str, level_str, message);
        }
#endif
    }
}

void log_message(log_category_t category, log_level_t level,
                 const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_message_v(category, level, format, args);
    va_end(args);
}

void log_event(log_category_t category, log_level_t level,
               const char* message, const char* json_data)
{
    if (!s_initialized || category >= LOG_CAT_COUNT || level == LOG_LEVEL_NONE) {
        return;
    }

    const char* cat_str = log_category_to_string(category);
    const char* level_str = log_level_to_string(level);

    lock();
    log_category_config_t config = s_config[category];
    unlock();

    /* Route to serial if enabled and level meets threshold */
    if ((config.destinations & LOG_DEST_SERIAL) && level <= config.serial_level) {
        const log_backend_ops_t* backend = s_serial_backend ? s_serial_backend
                                                            : &s_default_serial_backend;
        if (backend->write_event) {
            backend->write_event(cat_str, level_str, message, json_data);
        }
    }

    /* Events always go to database regardless of level (if destination enabled) */
    if (config.destinations & LOG_DEST_DATABASE) {
#ifdef ESP_PLATFORM
        const log_backend_ops_t* backend = s_database_backend ? s_database_backend
                                                               : &s_default_database_backend;
        if (backend && backend->write_event) {
            backend->write_event(cat_str, level_str, message, json_data);
        }
#else
        if (s_database_backend && s_database_backend->write_event) {
            s_database_backend->write_event(cat_str, level_str, message, json_data);
        }
#endif
    }
}

/* ============================================
 * Utility Functions
 * ============================================ */

const char* log_level_to_string(log_level_t level)
{
    if (level < sizeof(s_level_names) / sizeof(s_level_names[0])) {
        return s_level_names[level];
    }
    return "unknown";
}

const char* log_category_to_string(log_category_t category)
{
    if (category < LOG_CAT_COUNT) {
        return s_category_names[category];
    }
    return "unknown";
}

log_level_t log_level_from_string(const char* str)
{
    if (!str) {
        return LOG_LEVEL_INFO;
    }

    /* Case-insensitive comparison */
    for (int i = 0; i < (int)(sizeof(s_level_names) / sizeof(s_level_names[0])); i++) {
        const char* name = s_level_names[i];
        const char* s = str;
        bool match = true;
        while (*name && *s) {
            if (tolower((unsigned char)*name) != tolower((unsigned char)*s)) {
                match = false;
                break;
            }
            name++;
            s++;
        }
        if (match && *name == '\0' && *s == '\0') {
            return (log_level_t)i;
        }
    }

    return LOG_LEVEL_INFO;
}

log_category_t log_category_from_string(const char* str)
{
    if (!str) {
        return LOG_CAT_SYSTEM;
    }

    /* Case-insensitive comparison */
    for (int i = 0; i < LOG_CAT_COUNT; i++) {
        const char* name = s_category_names[i];
        const char* s = str;
        bool match = true;
        while (*name && *s) {
            if (tolower((unsigned char)*name) != tolower((unsigned char)*s)) {
                match = false;
                break;
            }
            name++;
            s++;
        }
        if (match && *name == '\0' && *s == '\0') {
            return (log_category_t)i;
        }
    }

    return LOG_CAT_SYSTEM;
}

/* ============================================
 * Backend Management
 * ============================================ */

void log_set_backend(log_destination_t destination, const log_backend_ops_t* ops)
{
    lock();
    if (destination & LOG_DEST_SERIAL) {
        s_serial_backend = ops;
    }
    if (destination & LOG_DEST_DATABASE) {
        s_database_backend = ops;
    }
    unlock();
}

const log_backend_ops_t* log_get_default_backend(log_destination_t destination)
{
    if (destination == LOG_DEST_SERIAL) {
        return &s_default_serial_backend;
    }
#ifdef ESP_PLATFORM
    if (destination == LOG_DEST_DATABASE) {
        return &s_default_database_backend;
    }
#endif
    return NULL;
}
