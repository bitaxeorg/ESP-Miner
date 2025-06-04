#ifndef DATABASE_H_
#define DATABASE_H_

#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

// Database initialization
esp_err_t dataBase_init(void);

// SPIFFS management
esp_err_t dataBase_init_spiffs(void);
esp_err_t dataBase_check_partition_layout(void);

// Theme management functions
esp_err_t dataBase_init_themes(void);
esp_err_t dataBase_set_active_theme(const char* theme_name);
esp_err_t dataBase_get_active_theme(char* theme_name, size_t max_len);
esp_err_t dataBase_get_available_themes(cJSON** themes_json);
esp_err_t dataBase_add_theme(const char* theme_name, const char* theme_data);

// Logging functions
esp_err_t dataBase_init_logs(void);
esp_err_t dataBase_log_event(const char* event_type, const char* severity, const char* message, const char* data);
esp_err_t dataBase_get_recent_logs(int max_count, cJSON** logs_json);
esp_err_t dataBase_archive_old_logs(void);

// HTTP API endpoints
esp_err_t dataBase_register_api_endpoints(httpd_handle_t server, void* ctx);

// Utility functions
esp_err_t dataBase_read_json_file(const char* path, cJSON** json);
esp_err_t dataBase_write_json_file(const char* path, cJSON* json);

#endif // DATABASE_H_ 