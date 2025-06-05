# JSON Database System for ESP32 Miner

This directory contains a lightweight JSON-based database system designed specifically for the ESP32 miner project. The database manages the data SPIFFS partition separately from the web UI partition, providing persistent storage for themes and event logging.

## Database Structure

The database system uses a JSON file-based approach with the following structure:

```
/data (or /www/data in legacy mode)
├── themes/
│   ├── activeThemes.json      # Currently active theme
│   └── availableThemes.json   # List of all available themes
└── logs/
    ├── recentLogs.json        # Recent event logs (max 100 entries)
    ├── errorLogs.json         # Persistent error logs (no rotation)
    └── criticalLogs.json      # Persistent critical logs (no rotation)
```

### Partition Support

The database automatically detects and supports two partition layouts:

- **New Layout**: 2MB www + 1MB data partitions (recommended)
- **Legacy Layout**: Single 3MB www partition with data subdirectory (backward compatible)

### File Structure Examples

#### activeThemes.json
```json
{
  "activeTheme": "THEME_ACS_DEFAULT",
  "lastUpdated": 1706798400
}
```

#### availableThemes.json
```json
{
  "themes": [
    {
      "name": "THEME_ACS_DEFAULT",
      "description": "ACS Default green theme",
      "version": "1.0"
    },
    {
      "name": "THEME_BITAXE_RED", 
      "description": "Bitaxe red theme",
      "version": "1.0"
    }
  ],
  "lastUpdated": 1706798400
}
```

#### recentLogs.json
```json
{
  "maxEvents": 100,
  "currentCount": 2,
  "lastArchived": 1706798400,
  "events": [
    {
      "timestamp": 1706798400,
      "type": "system",
      "severity": "info",
      "message": "System started",
      "data": {"bootTime": 1706798400}
    }
  ]
}
```

#### errorLogs.json
```json
{
  "totalErrors": 3,
  "lastError": 1706798700,
  "description": "Persistent error logs - no automatic rotation",
  "errors": [
    {
      "timestamp": 1706798600,
      "type": "system",
      "severity": "error",
      "message": "ASIC communication failed",
      "data": {"attempts": 3, "lastResponse": "timeout"}
    },
    {
      "timestamp": 1706798700,
      "type": "network",
      "severity": "critical",
      "message": "Pool connection lost",
      "data": {"reconnectAttempts": 5, "poolUrl": "stratum+tcp://pool.example.com"}
    }
  ]
}
```

#### criticalLogs.json
```json
{
  "totalCritical": 3,
  "lastCritical": 1706798700,
  "description": "Persistent critical logs - no automatic rotation",
  "critical": [
    {
      "timestamp": 1706798600,
      "type": "system",
      "severity": "critical",
      "message": "ASIC overheating",
      "data": {"temperature": 85, "threshold": 80}
    },
    {
      "timestamp": 1706798700,
      "type": "network",
      "severity": "critical",
      "message": "Pool connection timeout",
      "data": {"poolUrl": "stratum+tcp://pool.example.com", "timeout": 30}
    }
  ]
}
```

## Initializing Database

### Main Initialization

The database is initialized through the main initialization function:

```c
#include "dataBase.h"

esp_err_t ret = dataBase_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize database");
    return ret;
}
```

### Initialization Process

The `dataBase_init()` function performs the following steps:

1. **Partition Detection**: Automatically detects whether using new or legacy partition layout
2. **SPIFFS Mounting**: Mounts the data partition (or creates data directory in legacy mode)  
3. **Theme Database**: Initializes theme storage files
4. **Logging Database**: Initializes event logging files
5. **Error Logs Database**: Initializes persistent error logging files
6. **Directory Creation**: Creates necessary directory structure

### Integration with HTTP Server

The database initialization is integrated with the HTTP server startup:

```c
// In start_rest_server()
if (init_fs() != ESP_OK) {
    enter_recovery = true;
}

// Initialize data partition database (independent of www partition)
esp_err_t db_ret = dataBase_init();
if (db_ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize database system, continuing without database features");
}
```

## Reading from Database

### Utility Function

The database provides a general-purpose JSON file reader:

```c
esp_err_t dataBase_read_json_file(const char* path, cJSON** json);
```

**Usage Example:**
```c
cJSON* data;
esp_err_t ret = dataBase_read_json_file("/data/themes/activeThemes.json", &data);
if (ret == ESP_OK) {
    // Process the JSON data
    cJSON* theme = cJSON_GetObjectItem(data, "activeTheme");
    if (cJSON_IsString(theme)) {
        ESP_LOGI(TAG, "Active theme: %s", theme->valuestring);
    }
    cJSON_Delete(data);
}
```

### Error Handling

Reading functions return standard ESP-IDF error codes:
- `ESP_OK`: Success
- `ESP_FAIL`: File not found or JSON parse error
- `ESP_ERR_NO_MEM`: Memory allocation failure

## Writing to Database

### Utility Function

The database provides a general-purpose JSON file writer:

```c
esp_err_t dataBase_write_json_file(const char* path, cJSON* json);
```

**Usage Example:**
```c
cJSON* root = cJSON_CreateObject();
cJSON_AddStringToObject(root, "activeTheme", "THEME_BITAXE_RED");
cJSON_AddNumberToObject(root, "lastUpdated", esp_timer_get_time() / 1000000);

esp_err_t ret = dataBase_write_json_file("/data/themes/activeThemes.json", root);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Successfully saved theme data");
}
cJSON_Delete(root);
```

### Atomic Operations

The write function ensures atomic operations by:
1. Creating parent directories if they don't exist
2. Writing the complete JSON string to file
3. Properly closing the file handle
4. Returning appropriate error codes

## Theme Management Implementation

The theme management system provides persistent storage for theme configurations and selections.

### API Functions

#### Set Active Theme
```c
esp_err_t dataBase_set_active_theme(const char* theme_name);
```

**Example:**
```c
esp_err_t ret = dataBase_set_active_theme("THEME_BITAXE_RED");
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Theme changed successfully");
}
```

#### Get Active Theme
```c
esp_err_t dataBase_get_active_theme(char* theme_name, size_t max_len);
```

**Example:**
```c
char current_theme[32];
esp_err_t ret = dataBase_get_active_theme(current_theme, sizeof(current_theme));
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Current theme: %s", current_theme);
}
```

#### Get Available Themes
```c
esp_err_t dataBase_get_available_themes(cJSON** themes_json);
```

**Example:**
```c
cJSON* themes;
esp_err_t ret = dataBase_get_available_themes(&themes);
if (ret == ESP_OK) {
    cJSON* themes_array = cJSON_GetObjectItem(themes, "themes");
    int theme_count = cJSON_GetArraySize(themes_array);
    ESP_LOGI(TAG, "Found %d available themes", theme_count);
    cJSON_Delete(themes);
}
```

### Integration with Theme API

The theme system integrates with the existing `theme_api.c`:

1. **Loading**: `loadThemefromNVS()` first tries the database, falls back to NVS
2. **Saving**: Theme changes are saved to both database and NVS for compatibility
3. **Logging**: Theme changes are automatically logged to the event system

### Supported Themes

Currently supported themes:
- `THEME_ACS_DEFAULT`: ACS Default green theme
- `THEME_BITAXE_RED`: Bitaxe red theme  
- `THEME_SOLO_MINING_CO`: Solo Mining Co theme

## Event Logging Implementation

The logging system provides persistent storage for system events with automatic rotation and structured data support.

### API Functions

#### Log Event
```c
esp_err_t dataBase_log_event(const char* event_type, const char* severity, 
                             const char* message, const char* data);
```

**Example:**
```c
// Simple event
dataBase_log_event("system", "info", "Miner started", NULL);

// Event with structured data
char event_data[128];
snprintf(event_data, sizeof(event_data), 
         "{\"hashrate\":%.2f,\"temperature\":%d}", 
         current_hashrate, chip_temp);
dataBase_log_event("mining", "info", "Status update", event_data);
```

#### Get Recent Logs
```c
esp_err_t dataBase_get_recent_logs(int max_count, cJSON** logs_json);
```

**Example:**
```c
cJSON* logs;
esp_err_t ret = dataBase_get_recent_logs(10, &logs);
if (ret == ESP_OK) {
    cJSON* events = cJSON_GetObjectItem(logs, "events");
    cJSON* count = cJSON_GetObjectItem(logs, "count");
    ESP_LOGI(TAG, "Retrieved %d log entries", count->valueint);
    cJSON_Delete(logs);
}
```

### Event Categories

Supported event types:
- `system`: System-level events (startup, shutdown, errors)
- `theme`: Theme change events
- `mining`: Mining-related events (hashrate, shares, etc.)
- `network`: Network connectivity events
- `power`: Power management events
- `user`: User-initiated actions

### Severity Levels

Supported severity levels:
- `debug`: Detailed debugging information
- `info`: General information messages
- `warning`: Warning conditions
- `error`: Error conditions
- `critical`: Critical system errors

### Automatic Features

1. **Rotation**: Automatically removes oldest events when limit (100) is reached
2. **Timestamps**: Automatically adds timestamps to all events
3. **Structured Data**: Supports both simple string data and complex JSON objects
4. **Performance**: Efficient file-based storage with minimal memory usage

## Persistent Error Logging Implementation

The persistent error logging system provides long-term storage for error and critical events without automatic rotation. These logs persist across system reboots and are designed for troubleshooting and system health monitoring.

### Key Features

- **Persistent Storage**: No automatic rotation or size limits
- **Automatic Dual Logging**: Error and critical events are automatically logged to both recent logs and error logs
- **Comprehensive Metadata**: Tracks total error count and last error timestamp
- **Manual Management**: Requires explicit clearing via API calls

### API Functions

#### Log Error Event
```c
esp_err_t dataBase_log_error(const char* event_type, const char* severity, 
                            const char* message, const char* data);
```

**Example:**
```c
// Log a critical system error
dataBase_log_error("system", "critical", "ASIC overheating", 
                   "{\"temperature\":85,\"threshold\":80}");

// Log a network error
dataBase_log_error("network", "error", "Pool connection timeout", 
                   "{\"poolUrl\":\"stratum+tcp://pool.example.com\",\"timeout\":30}");
```

#### Get Error Logs
```c
esp_err_t dataBase_get_error_logs(int max_count, cJSON** logs_json);
```

**Example:**
```c
cJSON* error_logs;
// Get last 20 errors (0 = get all errors)
esp_err_t ret = dataBase_get_error_logs(20, &error_logs);
if (ret == ESP_OK) {
    cJSON* errors = cJSON_GetObjectItem(error_logs, "errors");
    cJSON* total = cJSON_GetObjectItem(error_logs, "totalErrors");
    ESP_LOGI(TAG, "Retrieved %d of %d total errors", 
             cJSON_GetArraySize(errors), total->valueint);
    cJSON_Delete(error_logs);
}
```

#### Clear Error Logs
```c
esp_err_t dataBase_clear_error_logs(void);
```

**Example:**
```c
esp_err_t ret = dataBase_clear_error_logs();
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Error logs cleared successfully");
}
```

### Automatic Integration

Error logs are automatically populated when using the standard logging function with error/critical severity:

```c
// This will log to both recentLogs.json AND errorLogs.json
dataBase_log_event("system", "error", "Memory allocation failed", NULL);
dataBase_log_event("mining", "critical", "Hardware failure detected", 
                   "{\"chip\":1,\"errorCode\":\"0xFF\"}");
```

### Response Format

Error logs API returns comprehensive metadata:

```json
{
  "errors": [
    {
      "timestamp": 1706798600,
      "type": "system", 
      "severity": "error",
      "message": "ASIC communication failed",
      "data": {"attempts": 3, "lastResponse": "timeout"}
    }
  ],
  "count": 1,          // Number of errors returned
  "totalErrors": 15,   // Total errors since last clear
  "lastError": 1706798600  // Timestamp of most recent error
}
```

### Use Cases

1. **System Health Monitoring**: Track error patterns over time
2. **Troubleshooting**: Review historical errors for debugging
3. **Quality Assurance**: Monitor system stability metrics
4. **Alert Integration**: Check error counts for alerting systems
5. **Performance Analysis**: Correlate errors with system performance

### Best Practices

1. **Regular Monitoring**: Periodically check error logs via API
2. **Proactive Clearing**: Clear logs after addressing issues
3. **Trend Analysis**: Monitor error frequency patterns
4. **Structured Data**: Include relevant context in error data fields
5. **Severity Classification**: Use appropriate severity levels (error vs critical)

## Critical Logging Implementation

The critical logging system provides dedicated persistent storage specifically for critical-severity events. This system runs in parallel with both regular logs and error logs, ensuring critical events receive special attention.

### Key Features

- **Critical-Only Focus**: Only logs events with "critical" severity
- **Persistent Storage**: No automatic rotation or size limits
- **Triple Logging**: Critical events are logged to recent logs, error logs, AND critical logs
- **Dedicated API**: Separate endpoint for retrieving only critical events
- **System Health**: Designed for monitoring the most severe system issues

### API Functions

#### Log Critical Event
```c
esp_err_t dataBase_log_critical(const char* event_type, const char* severity, 
                               const char* message, const char* data);
```

**Example:**
```c
// Log a critical system failure
dataBase_log_critical("power", "critical", "Overheat mode activated", 
                     "{\"chipTemp\":85.0,\"threshold\":80,\"device\":\"DEVICE_MAX\"}");

// Log a critical network issue
dataBase_log_critical("network", "critical", "Pool connection completely lost", 
                     "{\"downtime\":300,\"poolUrl\":\"stratum+tcp://pool.example.com\"}");
```

#### Get Critical Logs
```c
esp_err_t dataBase_get_critical_logs(int max_count, cJSON** logs_json);
```

**Example:**
```c
cJSON* critical_logs;
// Get last 10 critical events (0 = get all critical events)
esp_err_t ret = dataBase_get_critical_logs(10, &critical_logs);
if (ret == ESP_OK) {
    cJSON* critical = cJSON_GetObjectItem(critical_logs, "critical");
    cJSON* total = cJSON_GetObjectItem(critical_logs, "totalCritical");
    ESP_LOGI(TAG, "Retrieved %d of %d total critical events", 
             cJSON_GetArraySize(critical), total->valueint);
    cJSON_Delete(critical_logs);
}
```

#### Clear Critical Logs
```c
esp_err_t dataBase_clear_critical_logs(void);
```

**Example:**
```c
esp_err_t ret = dataBase_clear_critical_logs();
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Critical logs cleared successfully");
}
```

### Automatic Integration

Critical events are automatically logged to all three systems:

```c
// This will log to recentLogs.json, errorLogs.json, AND criticalLogs.json
dataBase_log_event("system", "critical", "Hardware failure detected", 
                   "{\"component\":\"ASIC\",\"errorCode\":\"0xFF\"}");
```

### Response Format

Critical logs API returns focused metadata:

```json
{
  "critical": [
    {
      "timestamp": 1706798600,
      "type": "power", 
      "severity": "critical",
      "message": "Overheat mode activated",
      "data": {"chipTemp": 85.0, "threshold": 80, "device": "DEVICE_MAX"}
    }
  ],
  "count": 1,              // Number of critical events returned
  "totalCritical": 5,      // Total critical events since last clear
  "lastCritical": 1706798600  // Timestamp of most recent critical event
}
```

### Common Critical Events

The system automatically logs these critical events:

1. **Overheat Protection**: When thermal limits are exceeded
   ```json
   {
     "type": "power",
     "severity": "critical", 
     "message": "Overheat mode activated - VR or ASIC temperature exceeded threshold",
     "data": {"vrTemp": 85.0, "chipTemp": 75.0, "device": "DEVICE_GAMMA"}
   }
   ```

2. **System Failures**: Hardware or software failures
3. **Network Critical Issues**: Complete loss of connectivity
4. **Power Management**: Critical power events

### Use Cases

1. **Emergency Monitoring**: Track only the most severe issues
2. **Alert Systems**: Trigger immediate notifications on critical events
3. **Failure Analysis**: Analyze patterns in critical system failures
4. **Compliance**: Maintain records of critical system events
5. **Diagnostics**: Quick identification of severe problems

### Best Practices

1. **Reserve for Genuine Critical Events**: Don't overuse critical severity
2. **Include Context**: Always provide relevant data for troubleshooting
3. **Monitor Regularly**: Check critical logs frequently for system health
4. **Act Quickly**: Critical events often require immediate attention
5. **Clear After Resolution**: Clear logs once critical issues are addressed

## Error Handling

All database functions follow ESP-IDF error handling conventions:

- Return `ESP_OK` on success
- Return appropriate error codes on failure
- Log errors using ESP-IDF logging system
- Graceful degradation when database is unavailable

## Performance Considerations

1. **Memory Usage**: JSON objects are created and destroyed immediately to minimize heap usage
2. **File I/O**: Files are opened, processed, and closed in single operations
3. **SPIFFS Limitations**: Aware of SPIFFS write endurance and optimizes for minimal writes
4. **Error Recovery**: Robust error handling prevents system crashes from database issues

## Future Enhancements

Potential improvements for future versions:

1. **Data Compression**: Implement compression for log files
2. **Log Archiving**: Archive old logs to prevent partition filling
3. **Data Validation**: Add JSON schema validation
4. **Backup/Restore**: Implement data backup and restore functionality
5. **API Extensions**: Add more sophisticated query capabilities
6. **Performance Monitoring**: Add metrics for database operations 