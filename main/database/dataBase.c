#include "dataBase.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "theme_api.h"
#include "nvs_config.h"
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "dataBase";

// Partition layout detection
typedef enum {
    PARTITION_LAYOUT_OLD,  // Single 3MB www partition
    PARTITION_LAYOUT_NEW   // 2MB www + 1MB data partitions
} partition_layout_t;

static partition_layout_t current_layout;

// File paths based on partition layout
#define THEMES_DIR_NEW "/data/themes"
#define LOGS_DIR_NEW "/data/logs"
#define THEMES_DIR_OLD "/www/data/themes"
#define LOGS_DIR_OLD "/www/data/logs"

#define ACTIVE_THEMES_FILE "activeThemes.json"
#define AVAILABLE_THEMES_FILE "availableThemes.json"
#define RECENT_LOGS_FILE "recentLogs.json"
#define ERROR_LOGS_FILE "errorLogs.json"
#define CRITICAL_LOGS_FILE "criticalLogs.json"

static char themes_dir[32];
static char logs_dir[32];

// Helper function to get file path based on partition layout
static void get_file_path(const char* base_dir, const char* filename, char* full_path, size_t max_len) {
    snprintf(full_path, max_len, "%s/%s", base_dir, filename);
}

// Detect partition layout
esp_err_t dataBase_check_partition_layout(void) {
    // Check if the "data" partition exists
    const esp_partition_t *data_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 
        "data"
    );
    
    if (data_partition != NULL) {
        ESP_LOGI(TAG, "Detected new partition layout (2MB www + 1MB data)");
        current_layout = PARTITION_LAYOUT_NEW;
        strcpy(themes_dir, THEMES_DIR_NEW);
        strcpy(logs_dir, LOGS_DIR_NEW);
    } else {
        ESP_LOGI(TAG, "Detected old partition layout (3MB www)");
        current_layout = PARTITION_LAYOUT_OLD;
        strcpy(themes_dir, THEMES_DIR_OLD);
        strcpy(logs_dir, LOGS_DIR_OLD);
    }
    
    return ESP_OK;
}

// Initialize data SPIFFS filesystem only
esp_err_t dataBase_init_spiffs(void) {
    esp_err_t ret = ESP_OK;
    
    if (current_layout == PARTITION_LAYOUT_NEW) {
        // Only mount data partition - www partition is handled by http_server
        esp_vfs_spiffs_conf_t data_conf = {
            .base_path = "/data",
            .partition_label = "data",
            .max_files = 10,
            .format_if_mount_failed = false
        };
        
        ret = esp_vfs_spiffs_register(&data_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mount data partition: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Mounted data partition");
        
        // Log partition info
        size_t data_total = 0, data_used = 0;
        esp_spiffs_info("data", &data_total, &data_used);
        ESP_LOGI(TAG, "Data partition: total: %d, used: %d", data_total, data_used);
        
    } else {
        // For old layout, create data directory structure under www (which is handled by http_server)
        // The www partition should already be mounted by http_server's init_fs()
        mkdir("/www/data", 0755);
        ESP_LOGI(TAG, "Using www partition with data subdirectory (old layout)");
    }
    
    return ESP_OK;
}

// Utility function to read JSON file
esp_err_t dataBase_read_json_file(const char* path, cJSON** json) {
    FILE* file = fopen(path, "r");
    if (!file) {
        ESP_LOGW(TAG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        return ESP_FAIL;
    }
    
    // Allocate buffer and read file
    char* buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(buffer, 1, file_size, file);
    buffer[read_size] = '\0';
    fclose(file);
    
    // Parse JSON
    cJSON* parsed_json = cJSON_Parse(buffer);
    free(buffer);
    
    if (!parsed_json) {
        ESP_LOGE(TAG, "Failed to parse JSON from file: %s", path);
        return ESP_FAIL;
    }
    
    *json = parsed_json;
    return ESP_OK;
}

// Utility function to write JSON file
esp_err_t dataBase_write_json_file(const char* path, cJSON* json) {
    char* json_str = cJSON_Print(json);
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }
    
    // Ensure directory exists
    char dir_path[128];
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir_path, 0755);
    }
    
    FILE* file = fopen(path, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", path);
        free(json_str);
        return ESP_FAIL;
    }
    
    size_t written = fwrite(json_str, 1, strlen(json_str), file);
    fclose(file);
    free(json_str);
    
    if (written == 0) {
        ESP_LOGE(TAG, "Failed to write to file: %s", path);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// Initialize themes database
esp_err_t dataBase_init_themes(void) {
    // Create themes directory if it doesn't exist
    if (mkdir(themes_dir, 0755) != 0) {
        // Directory might already exist, which is fine
    }
    
    char active_themes_path[128];
    get_file_path(themes_dir, ACTIVE_THEMES_FILE, active_themes_path, sizeof(active_themes_path));
    
    // Check if activeThemes.json exists
    FILE* file = fopen(active_themes_path, "r");
    if (!file) {
        ESP_LOGI(TAG, "Creating activeThemes.json");
        
        // Create default active themes file
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "activeTheme", "THEME_ACS_DEFAULT");
        cJSON_AddNumberToObject(root, "lastUpdated", esp_timer_get_time() / 1000000);
        
        esp_err_t ret = dataBase_write_json_file(active_themes_path, root);
        cJSON_Delete(root);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create activeThemes.json");
            return ret;
        }
    } else {
        fclose(file);
    }
    
    char available_themes_path[128];
    get_file_path(themes_dir, AVAILABLE_THEMES_FILE, available_themes_path, sizeof(available_themes_path));
    
    // Check if availableThemes.json exists
    file = fopen(available_themes_path, "r");
    if (!file) {
        ESP_LOGI(TAG, "Creating availableThemes.json");
        
        // Create available themes file with all supported themes
        cJSON* root = cJSON_CreateObject();
        cJSON* themes_array = cJSON_CreateArray();
        
        // Add all available themes from theme_api.h
        const char* theme_names[] = {
            "THEME_ACS_DEFAULT",
            "THEME_BITAXE_RED", 
            "THEME_SOLO_MINING_CO"
        };
        
        for (int i = 0; i < 6; i++) {
            cJSON_AddItemToArray(themes_array, cJSON_CreateString(theme_names[i]));
        }
        
        cJSON_AddItemToObject(root, "themes", themes_array);
        time_t now;
        time(&now);
        cJSON_AddNumberToObject(root, "lastUpdated", now);
        
        esp_err_t ret = dataBase_write_json_file(available_themes_path, root);
        cJSON_Delete(root);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create availableThemes.json");
            return ret;
        }
    } else {
        fclose(file);
    }
    
    ESP_LOGI(TAG, "Themes database initialized successfully");
    return ESP_OK;
}

// Set active theme
esp_err_t dataBase_set_active_theme(const char* theme_name) {
    char active_themes_path[128];
    get_file_path(themes_dir, ACTIVE_THEMES_FILE, active_themes_path, sizeof(active_themes_path));
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "activeTheme", theme_name);
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(root, "lastUpdated", now);      
    
    esp_err_t ret = dataBase_write_json_file(active_themes_path, root);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Active theme set to: %s", theme_name);
    } else {
        ESP_LOGE(TAG, "Failed to set active theme");
    }
    
    return ret;
}

// Get active theme
esp_err_t dataBase_get_active_theme(char* theme_name, size_t max_len) {
    char active_themes_path[128];
    get_file_path(themes_dir, ACTIVE_THEMES_FILE, active_themes_path, sizeof(active_themes_path));
    
    cJSON* root;
    esp_err_t ret = dataBase_read_json_file(active_themes_path, &root);
    if (ret != ESP_OK) {
        // Return default theme if file doesn't exist
        strncpy(theme_name, "THEME_ACS_DEFAULT", max_len - 1);
        theme_name[max_len - 1] = '\0';
        return ESP_OK;
    }
    
    cJSON* active_theme = cJSON_GetObjectItem(root, "activeTheme");
    if (cJSON_IsString(active_theme)) {
        strncpy(theme_name, active_theme->valuestring, max_len - 1);
        theme_name[max_len - 1] = '\0';
        ret = ESP_OK;
    } else {
        strncpy(theme_name, "THEME_ACS_DEFAULT", max_len - 1);
        theme_name[max_len - 1] = '\0';
        ret = ESP_FAIL;
    }
    
    cJSON_Delete(root);
    return ret;
}

// Get available themes
esp_err_t dataBase_get_available_themes(cJSON** themes_json) {
    char available_themes_path[128];
    get_file_path(themes_dir, AVAILABLE_THEMES_FILE, available_themes_path, sizeof(available_themes_path));
    
    return dataBase_read_json_file(available_themes_path, themes_json);
}

// Initialize logs database
esp_err_t dataBase_init_logs(void) {
    // Create logs directory if it doesn't exist
    if (mkdir(logs_dir, 0755) != 0) {
        // Directory might already exist, which is fine
    }
    
    char recent_logs_path[128];
    get_file_path(logs_dir, RECENT_LOGS_FILE, recent_logs_path, sizeof(recent_logs_path));
    
    // Check if recentLogs.json exists
    FILE* file = fopen(recent_logs_path, "r");
    if (!file) {
        ESP_LOGI(TAG, "Creating recentLogs.json");
        
        // Create empty logs file
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "maxEvents", 100);
        cJSON_AddNumberToObject(root, "currentCount", 0);
        cJSON_AddNumberToObject(root, "lastArchived", 0);
        
        cJSON* events_array = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "events", events_array);
        
        esp_err_t ret = dataBase_write_json_file(recent_logs_path, root);
        cJSON_Delete(root);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create recentLogs.json");
            return ret;
        }
    } else {
        fclose(file);
    }
    
    ESP_LOGI(TAG, "Logs database initialized successfully");
    return ESP_OK;
}

// Log an event
esp_err_t dataBase_log_event(const char* event_type, const char* level, const char* message, const char* data) {
    char recent_logs_path[128];
    get_file_path(logs_dir, RECENT_LOGS_FILE, recent_logs_path, sizeof(recent_logs_path));
    
    cJSON* root;
    esp_err_t ret = dataBase_read_json_file(recent_logs_path, &root);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read recent logs file");
        return ret;
    }
    
    cJSON* events_array = cJSON_GetObjectItem(root, "events");
    if (!cJSON_IsArray(events_array)) {
        ESP_LOGE(TAG, "Events array not found in logs file");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Create new event
    cJSON* new_event = cJSON_CreateObject();
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(new_event, "timestamp", now);
    cJSON_AddStringToObject(new_event, "type", event_type);
    cJSON_AddStringToObject(new_event, "level", level);
    cJSON_AddStringToObject(new_event, "message", message);
    
    if (data && strlen(data) > 0) {
        cJSON* data_json = cJSON_Parse(data);
        if (data_json) {
            cJSON_AddItemToObject(new_event, "data", data_json);
        } else {
            cJSON_AddStringToObject(new_event, "data", data);
        }
    }
    
    // Add to events array
    cJSON_AddItemToArray(events_array, new_event);
    
    // Update current count
    cJSON* current_count = cJSON_GetObjectItem(root, "currentCount");
    if (cJSON_IsNumber(current_count)) {
        current_count->valueint++;
    }
    
    // Check if we need to trim (keep max 100 events)
    int array_size = cJSON_GetArraySize(events_array);
    if (array_size > 100) {
        // Remove oldest event
        cJSON_DeleteItemFromArray(events_array, 0);
        if (cJSON_IsNumber(current_count)) {
            current_count->valueint--;
        }
    }
    
    // Write back to file
    ret = dataBase_write_json_file(recent_logs_path, root);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Event logged: %s - %s", event_type, message);
    } else {
        ESP_LOGE(TAG, "Failed to write event log");
    }
    
    // Also log to error logs if level is error or critical
    if (strcmp(level, "error") == 0 || strcmp(level, "critical") == 0) {
        esp_err_t error_ret = dataBase_log_error(event_type, level, message, data);
        if (error_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to log to error logs");
        }
    }
    
    // Also log to critical logs if level is critical
    if (strcmp(level, "critical") == 0) {
        esp_err_t critical_ret = dataBase_log_critical(event_type, level, message, data);
        if (critical_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to log to critical logs");
        }
    }
    
    return ret;
}

// Get recent logs
esp_err_t dataBase_get_recent_logs(int max_count, cJSON** logs_json) {
    char recent_logs_path[128];
    get_file_path(logs_dir, RECENT_LOGS_FILE, recent_logs_path, sizeof(recent_logs_path));
    
    cJSON* root;
    esp_err_t ret = dataBase_read_json_file(recent_logs_path, &root);
    if (ret != ESP_OK) {
        return ret;
    }
    
    cJSON* events_array = cJSON_GetObjectItem(root, "events");
    if (!cJSON_IsArray(events_array)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Create response with limited events
    cJSON* response = cJSON_CreateObject();
    cJSON* limited_events = cJSON_CreateArray();
    
    int array_size = cJSON_GetArraySize(events_array);
    int count = (max_count < array_size) ? max_count : array_size;
    
    // Get the most recent events
    for (int i = array_size - count; i < array_size; i++) {
        cJSON* event = cJSON_GetArrayItem(events_array, i);
        cJSON_AddItemToArray(limited_events, cJSON_Duplicate(event, 1));
    }
    
    cJSON_AddItemToObject(response, "events", limited_events);
    cJSON_AddNumberToObject(response, "count", count);
    
    cJSON_Delete(root);
    *logs_json = response;
    
    return ESP_OK;
}

// Initialize error logs database
esp_err_t dataBase_init_error_logs(void) {
    // Create logs directory if it doesn't exist (should already exist from regular logs init)
    if (mkdir(logs_dir, 0755) != 0) {
        // Directory might already exist, which is fine
    }
    
    char error_logs_path[128];
    get_file_path(logs_dir, ERROR_LOGS_FILE, error_logs_path, sizeof(error_logs_path));
    
    // Check if errorLogs.json exists
    FILE* file = fopen(error_logs_path, "r");
    if (!file) {
        ESP_LOGI(TAG, "Creating errorLogs.json");
        
        // Create empty error logs file
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "totalErrors", 0);
        cJSON_AddNumberToObject(root, "lastError", 0);
        cJSON_AddStringToObject(root, "description", "Persistent error logs - no automatic rotation");
        
        cJSON* errors_array = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "errors", errors_array);
        
        esp_err_t ret = dataBase_write_json_file(error_logs_path, root);
        cJSON_Delete(root);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create errorLogs.json");
            return ret;
        }
    } else {
        fclose(file);
    }
    
    ESP_LOGI(TAG, "Error logs database initialized successfully");
    return ESP_OK;
}

// Log an error event (persistent)
esp_err_t dataBase_log_error(const char* event_type, const char* level, const char* message, const char* data) {
    char error_logs_path[128];
    get_file_path(logs_dir, ERROR_LOGS_FILE, error_logs_path, sizeof(error_logs_path));
    
    cJSON* root;
    esp_err_t ret = dataBase_read_json_file(error_logs_path, &root);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read error logs file");
        return ret;
    }
    
    cJSON* errors_array = cJSON_GetObjectItem(root, "errors");
    if (!cJSON_IsArray(errors_array)) {
        ESP_LOGE(TAG, "Errors array not found in error logs file");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Create new error event
    cJSON* new_error = cJSON_CreateObject();
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(new_error, "timestamp", now);
    cJSON_AddStringToObject(new_error, "type", event_type);
    cJSON_AddStringToObject(new_error, "level", level);
    cJSON_AddStringToObject(new_error, "message", message);
    
    if (data && strlen(data) > 0) {
        cJSON* data_json = cJSON_Parse(data);
        if (data_json) {
            cJSON_AddItemToObject(new_error, "data", data_json);
        } else {
            cJSON_AddStringToObject(new_error, "data", data);
        }
    }
    
    // Add to errors array
    cJSON_AddItemToArray(errors_array, new_error);
    
    // Update total count and last error timestamp
    cJSON* total_errors = cJSON_GetObjectItem(root, "totalErrors");
    if (cJSON_IsNumber(total_errors)) {
        total_errors->valueint++;
    }
    
    cJSON* last_error = cJSON_GetObjectItem(root, "lastError");
    if (cJSON_IsNumber(last_error)) {
        last_error->valueint = now;
    }
    
    // Write back to file (no size limit - persistent storage)
    ret = dataBase_write_json_file(error_logs_path, root);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Error logged: %s - %s", event_type, message);
    } else {
        ESP_LOGE(TAG, "Failed to write error log");
    }
    
    return ret;
}

// Get error logs
esp_err_t dataBase_get_error_logs(int max_count, cJSON** logs_json) {
    char error_logs_path[128];
    get_file_path(logs_dir, ERROR_LOGS_FILE, error_logs_path, sizeof(error_logs_path));
    
    cJSON* root;
    esp_err_t ret = dataBase_read_json_file(error_logs_path, &root);
    if (ret != ESP_OK) {
        return ret;
    }
    
    cJSON* errors_array = cJSON_GetObjectItem(root, "errors");
    if (!cJSON_IsArray(errors_array)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Create response with limited errors
    cJSON* response = cJSON_CreateObject();
    cJSON* limited_errors = cJSON_CreateArray();
    
    int array_size = cJSON_GetArraySize(errors_array);
    int count = (max_count > 0 && max_count < array_size) ? max_count : array_size;
    
    // Get the most recent errors
    for (int i = array_size - count; i < array_size; i++) {
        cJSON* error = cJSON_GetArrayItem(errors_array, i);
        cJSON_AddItemToArray(limited_errors, cJSON_Duplicate(error, 1));
    }
    
    cJSON_AddItemToObject(response, "errors", limited_errors);
    cJSON_AddNumberToObject(response, "count", count);
    cJSON_AddNumberToObject(response, "totalErrors", cJSON_GetObjectItem(root, "totalErrors")->valueint);
    cJSON_AddNumberToObject(response, "lastError", cJSON_GetObjectItem(root, "lastError")->valueint);
    
    cJSON_Delete(root);
    *logs_json = response;
    
    return ESP_OK;
}

// Clear all error logs
esp_err_t dataBase_clear_error_logs(void) {
    char error_logs_path[128];
    get_file_path(logs_dir, ERROR_LOGS_FILE, error_logs_path, sizeof(error_logs_path));
    
    // Create empty error logs file
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "totalErrors", 0);
    cJSON_AddNumberToObject(root, "lastError", 0);
    cJSON_AddStringToObject(root, "description", "Persistent error logs - no automatic rotation");
    
    cJSON* errors_array = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "errors", errors_array);
    
    esp_err_t ret = dataBase_write_json_file(error_logs_path, root);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Error logs cleared successfully");
    } else {
        ESP_LOGE(TAG, "Failed to clear error logs");
    }
    
    return ret;
}

// Initialize critical logs database
esp_err_t dataBase_init_critical_logs(void) {
    // Create logs directory if it doesn't exist (should already exist from regular logs init)
    if (mkdir(logs_dir, 0755) != 0) {
        // Directory might already exist, which is fine
    }
    
    char critical_logs_path[128];
    get_file_path(logs_dir, CRITICAL_LOGS_FILE, critical_logs_path, sizeof(critical_logs_path));
    
    // Check if criticalLogs.json exists
    FILE* file = fopen(critical_logs_path, "r");
    if (!file) {
        ESP_LOGI(TAG, "Creating criticalLogs.json");
        
        // Create empty critical logs file
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "totalCritical", 0);
        cJSON_AddNumberToObject(root, "lastCritical", 0);
        cJSON_AddStringToObject(root, "description", "Persistent critical logs - no automatic rotation");
        
        cJSON* critical_array = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "critical", critical_array);
        
        esp_err_t ret = dataBase_write_json_file(critical_logs_path, root);
        cJSON_Delete(root);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create criticalLogs.json");
            return ret;
        }
    } else {
        fclose(file);
    }
    
    ESP_LOGI(TAG, "Critical logs database initialized successfully");
    return ESP_OK;
}

// Log a critical event (persistent)
esp_err_t dataBase_log_critical(const char* event_type, const char* level, const char* message, const char* data) {
    char critical_logs_path[128];
    get_file_path(logs_dir, CRITICAL_LOGS_FILE, critical_logs_path, sizeof(critical_logs_path));
    
    cJSON* root;
    esp_err_t ret = dataBase_read_json_file(critical_logs_path, &root);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read critical logs file");
        return ret;
    }
    
    cJSON* critical_array = cJSON_GetObjectItem(root, "critical");
    if (!cJSON_IsArray(critical_array)) {
        ESP_LOGE(TAG, "Critical array not found in critical logs file");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Create new critical event
    cJSON* new_critical = cJSON_CreateObject();
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(new_critical, "timestamp", now);
    cJSON_AddStringToObject(new_critical, "type", event_type);
    cJSON_AddStringToObject(new_critical, "level", level);
    cJSON_AddStringToObject(new_critical, "message", message);
    
    if (data && strlen(data) > 0) {
        cJSON* data_json = cJSON_Parse(data);
        if (data_json) {
            cJSON_AddItemToObject(new_critical, "data", data_json);
        } else {
            cJSON_AddStringToObject(new_critical, "data", data);
        }
    }
    
    // Add to critical array
    cJSON_AddItemToArray(critical_array, new_critical);
    
    // Update total count and last critical timestamp
    cJSON* total_critical = cJSON_GetObjectItem(root, "totalCritical");
    if (cJSON_IsNumber(total_critical)) {
        total_critical->valueint++;
    }
    
    cJSON* last_critical = cJSON_GetObjectItem(root, "lastCritical");
    if (cJSON_IsNumber(last_critical)) {
        last_critical->valueint = now;
    }
    
    // Write back to file (no size limit - persistent storage)
    ret = dataBase_write_json_file(critical_logs_path, root);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Critical event logged: %s - %s", event_type, message);
    } else {
        ESP_LOGE(TAG, "Failed to write critical log");
    }
    
    return ret;
}

// Get critical logs
esp_err_t dataBase_get_critical_logs(int max_count, cJSON** logs_json) {
    char critical_logs_path[128];
    get_file_path(logs_dir, CRITICAL_LOGS_FILE, critical_logs_path, sizeof(critical_logs_path));
    
    cJSON* root;
    esp_err_t ret = dataBase_read_json_file(critical_logs_path, &root);
    if (ret != ESP_OK) {
        return ret;
    }
    
    cJSON* critical_array = cJSON_GetObjectItem(root, "critical");
    if (!cJSON_IsArray(critical_array)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Create response with limited critical events
    cJSON* response = cJSON_CreateObject();
    cJSON* limited_critical = cJSON_CreateArray();
    
    int array_size = cJSON_GetArraySize(critical_array);
    int count = (max_count > 0 && max_count < array_size) ? max_count : array_size;
    
    // Get the most recent critical events
    for (int i = array_size - count; i < array_size; i++) {
        cJSON* critical = cJSON_GetArrayItem(critical_array, i);
        cJSON_AddItemToArray(limited_critical, cJSON_Duplicate(critical, 1));
    }
    
    cJSON_AddItemToObject(response, "critical", limited_critical);
    cJSON_AddNumberToObject(response, "count", count);
    cJSON_AddNumberToObject(response, "totalCritical", cJSON_GetObjectItem(root, "totalCritical")->valueint);
    cJSON_AddNumberToObject(response, "lastCritical", cJSON_GetObjectItem(root, "lastCritical")->valueint);
    
    cJSON_Delete(root);
    *logs_json = response;
    
    return ESP_OK;
}

// Clear all critical logs
esp_err_t dataBase_clear_critical_logs(void) {
    char critical_logs_path[128];
    get_file_path(logs_dir, CRITICAL_LOGS_FILE, critical_logs_path, sizeof(critical_logs_path));
    
    // Create empty critical logs file
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "totalCritical", 0);
    cJSON_AddNumberToObject(root, "lastCritical", 0);
    cJSON_AddStringToObject(root, "description", "Persistent critical logs - no automatic rotation");
    
    cJSON* critical_array = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "critical", critical_array);
    
    esp_err_t ret = dataBase_write_json_file(critical_logs_path, root);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Critical logs cleared successfully");
    } else {
        ESP_LOGE(TAG, "Failed to clear critical logs");
    }
    
    return ret;
}

// Main database initialization - only handles data partition
esp_err_t dataBase_init(void) {
    ESP_LOGI(TAG, "Initializing database system (data partition only)...");
    
    // Check partition layout
    esp_err_t ret = dataBase_check_partition_layout();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check partition layout");
        return ret;
    }
    
    // Initialize data partition SPIFFS (only data, not www)
    ret = dataBase_init_spiffs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize data partition SPIFFS");
        return ret;
    }
    
    // Initialize themes database
    ret = dataBase_init_themes();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize themes database");
        return ret;
    }
    
    // Initialize logs database  
    ret = dataBase_init_logs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize logs database");
        return ret;
    }
    
    // Initialize error logs database
    ret = dataBase_init_error_logs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize error logs database");
        return ret;
    }
    
    // Initialize critical logs database
    ret = dataBase_init_critical_logs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize critical logs database");
        return ret;
    }
    
    ESP_LOGI(TAG, "Database system initialized successfully (data partition only)");
    return ESP_OK;
} 