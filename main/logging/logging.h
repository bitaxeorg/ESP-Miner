/**
 * @file logging.h
 * @brief Unified logging interface for multi-destination logging
 *
 * This module provides a single API for logging to multiple destinations:
 * - Serial (ESP_LOG)
 * - Database (SPIFFS via dataBase_log_event)
 * - WebUI (WebSocket push) - future
 * - Screen (BAP protocol) - future
 *
 * Usage:
 *   LOG_INFO(LOG_CAT_POWER, "Temperature: %.1f°C", temp);
 *   LOG_ERROR(LOG_CAT_MINING, "Pool connection failed: %s", error);
 *
 * For structured/event logging (always goes to database):
 *   log_event(LOG_CAT_SYSTEM, LOG_LEVEL_INFO, "Settings changed",
 *             "{\"key\":\"voltage\",\"old\":1200,\"new\":1250}");
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * Log Destinations
 * ============================================ */

/**
 * @brief Log destination flags (can be OR'd together)
 */
typedef enum {
    LOG_DEST_NONE     = 0,
    LOG_DEST_SERIAL   = (1 << 0),  /**< Serial console via ESP_LOG */
    LOG_DEST_DATABASE = (1 << 1),  /**< SPIFFS database via dataBase_log_event */
    LOG_DEST_WEBUI    = (1 << 2),  /**< WebSocket push to browser (future) */
    LOG_DEST_SCREEN   = (1 << 3),  /**< BAP screen display (future) */
    LOG_DEST_ALL      = 0x0F
} log_destination_t;

/* ============================================
 * Log Levels
 * ============================================ */

/**
 * @brief Log severity levels (compatible with ESP-IDF levels)
 */
typedef enum {
    LOG_LEVEL_NONE = 0,    /**< No logging */
    LOG_LEVEL_ERROR,       /**< Critical errors */
    LOG_LEVEL_WARN,        /**< Warnings */
    LOG_LEVEL_INFO,        /**< Informational */
    LOG_LEVEL_DEBUG,       /**< Debug details */
    LOG_LEVEL_TRACE        /**< Verbose trace */
} log_level_t;

/* ============================================
 * Log Categories
 * ============================================ */

/**
 * @brief Log categories for filtering and routing
 */
typedef enum {
    LOG_CAT_SYSTEM = 0,    /**< System events (startup, shutdown, config) */
    LOG_CAT_POWER,         /**< Power management (temps, voltage, fans) */
    LOG_CAT_MINING,        /**< Mining operations (pools, hashrate, jobs) */
    LOG_CAT_NETWORK,       /**< Network (WiFi, stratum connections) */
    LOG_CAT_ASIC,          /**< ASIC chip operations */
    LOG_CAT_API,           /**< HTTP/WebSocket API */
    LOG_CAT_THEME,         /**< Theme changes */
    LOG_CAT_SETTINGS,      /**< Settings changes */
    LOG_CAT_COUNT          /**< Number of categories (for array sizing) */
} log_category_t;

/* ============================================
 * Configuration
 * ============================================ */

/**
 * @brief Per-category logging configuration
 */
typedef struct {
    log_level_t serial_level;      /**< Minimum level for serial output */
    log_level_t database_level;    /**< Minimum level for database output */
    log_destination_t destinations; /**< Enabled destinations */
} log_category_config_t;

/**
 * @brief Initialize the logging system
 *
 * Must be called before using any logging functions.
 * Sets up default configuration (WARN+ to serial, ERROR+ to database).
 *
 * @return 0 on success, negative on error
 */
int log_init(void);

/**
 * @brief Set minimum log level for a specific destination
 *
 * @param category Log category to configure
 * @param destination Which destination to configure
 * @param level Minimum level to log (messages below this are filtered)
 */
void log_set_level(log_category_t category, log_destination_t destination,
                   log_level_t level);

/**
 * @brief Enable or disable destinations for a category
 *
 * @param category Log category to configure
 * @param destinations Bitmask of destinations to enable
 */
void log_set_destinations(log_category_t category, log_destination_t destinations);

/**
 * @brief Get current configuration for a category
 *
 * @param category Log category
 * @return Current configuration (or default if category invalid)
 */
log_category_config_t log_get_config(log_category_t category);

/* ============================================
 * Logging Functions
 * ============================================ */

/**
 * @brief Log a formatted message
 *
 * Routes message to configured destinations based on category and level.
 *
 * @param category Log category
 * @param level Log severity level
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void log_message(log_category_t category, log_level_t level,
                 const char* format, ...) __attribute__((format(printf, 3, 4)));

/**
 * @brief Log a formatted message (va_list version)
 *
 * @param category Log category
 * @param level Log severity level
 * @param format Printf-style format string
 * @param args Format arguments
 */
void log_message_v(log_category_t category, log_level_t level,
                   const char* format, va_list args);

/**
 * @brief Log a structured event (always includes database)
 *
 * Use for events that should be recorded with structured data.
 * Always logs to database regardless of level configuration.
 *
 * @param category Log category
 * @param level Log severity level
 * @param message Human-readable message
 * @param json_data Optional JSON data string (NULL for none)
 */
void log_event(log_category_t category, log_level_t level,
               const char* message, const char* json_data);

/* ============================================
 * Convenience Macros
 * ============================================ */

/**
 * @brief Log an error message
 * @param cat Log category (e.g., LOG_CAT_POWER)
 * @param fmt Printf-style format string
 */
#define LOG_ERROR(cat, fmt, ...) \
    log_message((cat), LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

/**
 * @brief Log a warning message
 * @param cat Log category
 * @param fmt Printf-style format string
 */
#define LOG_WARN(cat, fmt, ...) \
    log_message((cat), LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)

/**
 * @brief Log an info message
 * @param cat Log category
 * @param fmt Printf-style format string
 */
#define LOG_INFO(cat, fmt, ...) \
    log_message((cat), LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)

/**
 * @brief Log a debug message
 * @param cat Log category
 * @param fmt Printf-style format string
 */
#define LOG_DEBUG(cat, fmt, ...) \
    log_message((cat), LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

/**
 * @brief Log a trace message
 * @param cat Log category
 * @param fmt Printf-style format string
 */
#define LOG_TRACE(cat, fmt, ...) \
    log_message((cat), LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

/* ============================================
 * Utility Functions
 * ============================================ */

/**
 * @brief Get string representation of a log level
 * @param level Log level
 * @return Static string (e.g., "ERROR", "WARN", "INFO")
 */
const char* log_level_to_string(log_level_t level);

/**
 * @brief Get string representation of a category
 * @param category Log category
 * @return Static string (e.g., "power", "mining", "system")
 */
const char* log_category_to_string(log_category_t category);

/**
 * @brief Parse log level from string
 * @param str Level string (case-insensitive)
 * @return Log level, or LOG_LEVEL_INFO on invalid input
 */
log_level_t log_level_from_string(const char* str);

/**
 * @brief Parse category from string
 * @param str Category string (case-insensitive)
 * @return Log category, or LOG_CAT_SYSTEM on invalid input
 */
log_category_t log_category_from_string(const char* str);

/* ============================================
 * Backend Interface (for testing/extension)
 * ============================================ */

/**
 * @brief Log backend operations interface
 *
 * Allows injecting mock backends for testing or adding
 * custom backends (e.g., remote logging).
 */
typedef struct log_backend_ops {
    /**
     * @brief Output a log message
     * @param category Category string (e.g., "power")
     * @param level Level string (e.g., "error")
     * @param message Formatted message
     */
    void (*write_message)(const char* category, const char* level,
                          const char* message);

    /**
     * @brief Output a structured event
     * @param category Category string
     * @param level Level string
     * @param message Human-readable message
     * @param json_data JSON data string (may be NULL)
     */
    void (*write_event)(const char* category, const char* level,
                        const char* message, const char* json_data);
} log_backend_ops_t;

/**
 * @brief Set custom backend for a destination
 *
 * Use NULL to restore default backend.
 *
 * @param destination Which destination to configure
 * @param ops Backend operations (NULL for default)
 */
void log_set_backend(log_destination_t destination, const log_backend_ops_t* ops);

/**
 * @brief Get default backend operations
 *
 * Useful for creating wrapper backends that call through to defaults.
 *
 * @param destination Which destination
 * @return Default backend ops (NULL if destination invalid)
 */
const log_backend_ops_t* log_get_default_backend(log_destination_t destination);

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_H */
