/**
 * @file bap_handlers.c
 * @brief BAP command handlers
 * 
 * Contains handlers for different BAP commands (REQ, SET, SUB, UNSUB, etc.)
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_config.h"
#include "bap_handlers.h"
#include "bap_protocol.h"
#include "bap_uart.h"
#include "bap_subscription.h"
#include "bap.h"
#include "asic.h"

static const char *TAG = "BAP_HANDLERS";

static bap_command_handler_t handlers[BAP_CMD_UNKNOWN + 1] = {0};
static char last_processed_message[BAP_MAX_MESSAGE_LEN] = {0};
static uint32_t last_message_time = 0;

void BAP_register_handler(bap_command_t cmd, bap_command_handler_t handler) {
    if (cmd >= 0 && cmd <= BAP_CMD_UNKNOWN) {
        handlers[cmd] = handler;
    }
}

void BAP_parse_message(const char *message) {
    if (!message) {
        ESP_LOGE(TAG, "Parse message: NULL message");
        return;
    }

    uint32_t current_time = esp_timer_get_time() / 1000;
    if (strcmp(message, last_processed_message) == 0 &&
        (current_time - last_message_time) < 1000) {
        ESP_LOGW(TAG, "Duplicate message detected, ignoring: %s", message);
        return;
    }

    strncpy(last_processed_message, message, sizeof(last_processed_message) - 1);
    last_processed_message[sizeof(last_processed_message) - 1] = '\0';
    last_message_time = current_time;

    //ESP_LOGI(TAG, "Parsing message: %s", message);
    
    size_t len = strlen(message);
    if (len < 5) {
        ESP_LOGE(TAG, "Parse message: Too short (%d chars)", len);
        return;
    }

    if (message[0] != '$') {
        ESP_LOGE(TAG, "Parse message: Doesn't start with $");
        return;
    }

    const char *asterisk = strchr(message, '*');
    char sentence_body[BAP_MAX_MESSAGE_LEN];
    
    if (asterisk) {
        if ((asterisk - message) + 3 > len) {
            ESP_LOGE(TAG, "Parse message: Not enough room for checksum");
            return;
        }

        uint8_t received_checksum = 0;
        sscanf(asterisk + 1, "%2hhX", &received_checksum);

        size_t body_len = asterisk - (message + 1);
        if (body_len >= sizeof(sentence_body)) {
            ESP_LOGE(TAG, "Parse message: Body too long");
            return;
        }

        strncpy(sentence_body, message + 1, body_len);
        sentence_body[body_len] = '\0';

        uint8_t calc_checksum = BAP_calculate_checksum(sentence_body);
        
        if (calc_checksum != received_checksum) {
            ESP_LOGE(TAG, "Parse message: Checksum mismatch (received: 0x%02X, calculated: 0x%02X)",
                     received_checksum, calc_checksum);
            
            if (strstr(sentence_body, "BAP,SUB,") == sentence_body) {
                //ESP_LOGI(TAG, "Subscription command - ignoring checksum mismatch");
            } else {
                ESP_LOGE(TAG, "Non-subscription command with invalid checksum, rejecting");
                return;
            }
        }
    } else {
        size_t body_len = len - 1;
        
        for (size_t i = 1; i < len; i++) {
            if (message[i] == '\r' || message[i] == '\n') {
                body_len = i - 1;
                break;
            }
        }
        
        if (body_len >= sizeof(sentence_body)) {
            ESP_LOGE(TAG, "Parse message: Body too long");
            return;
        }
        
        strncpy(sentence_body, message + 1, body_len);
        sentence_body[body_len] = '\0';
        
        if (strstr(sentence_body, "BAP,SUB,") == sentence_body ||
            strstr(sentence_body, "BAP,UNSUB,") == sentence_body) {
            //ESP_LOGI(TAG, "Subscription command without checksum, accepted");
        } else {
            ESP_LOGE(TAG, "Non-subscription command without checksum, rejecting");
            return;
        }
    }

    char tokenize_body[BAP_MAX_MESSAGE_LEN];
    strcpy(tokenize_body, sentence_body);
    
    char *saveptr;
    char *talker = strtok_r(tokenize_body, ",", &saveptr);
    if (!talker || strcmp(talker, "BAP") != 0) {
        ESP_LOGE(TAG, "Parse message: Invalid talker ID: %s", talker ? talker : "NULL");
        return;
    }

    char *cmd_str = strtok_r(NULL, ",", &saveptr);
    if (!cmd_str) {
        ESP_LOGE(TAG, "Parse message: No command");
        return;
    }

    char *parameter = strtok_r(NULL, ",", &saveptr);
    if (!parameter) {
        ESP_LOGE(TAG, "Parse message: No parameter");
        return;
    }

    char *value = strtok_r(NULL, ",", &saveptr);

    bap_command_t cmd = BAP_command_from_string(cmd_str);

    if (cmd == BAP_CMD_UNKNOWN) {
        ESP_LOGE(TAG, "Parse message: Unknown command: %s", cmd_str);
        return;
    }

    if (handlers[cmd]) {
        //ESP_LOGI(TAG, "Calling handler for command: %s with parameter: %s", cmd_str, parameter);
        handlers[cmd](parameter, value);
    } else {
        ESP_LOGE(TAG, "No handler registered for command: %s", cmd_str);
    }
}

void BAP_handle_subscription(const char *parameter, const char *value) {
    //ESP_LOGI(TAG, "Handling subscription request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid subscription parameter");
        return;
    }

    // Check if we're in AP mode - subscriptions not allowed
    if (!bap_global_state || !bap_global_state->SYSTEM_MODULE.is_connected) {
        ESP_LOGW(TAG, "Subscription not allowed in AP mode");
        BAP_send_message(BAP_CMD_ERR, parameter, "ap_mode_no_subscriptions");
        return;
    }

    BAP_subscription_handle_subscribe(parameter, value);
}

void BAP_handle_unsubscription(const char *parameter, const char *value) {
    //ESP_LOGI(TAG, "Handling unsubscription request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid unsubscription parameter");
        return;
    }

    BAP_subscription_handle_unsubscribe(parameter, value);
}

void BAP_handle_request(const char *parameter, const char *value) {
    //ESP_LOGI(TAG, "Handling request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid request parameter");
        return;
    }

    // Check if we're in AP mode - most requests not allowed
    if (!bap_global_state || !bap_global_state->SYSTEM_MODULE.is_connected) {
        ESP_LOGW(TAG, "Request not allowed in AP mode");
        BAP_send_message(BAP_CMD_ERR, parameter, "ap_mode_no_requests");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    if (param == BAP_PARAM_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown request parameter: %s", parameter);
        return;
    }

    if (!bap_global_state) {
        ESP_LOGE(TAG, "Global state not available for request");
        return;
    }

    BAP_send_request(param, bap_global_state);
}

void BAP_send_request(bap_parameter_t param, GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state for request");
        return;
    }

    //ESP_LOGI(TAG, "Sending request response for %s", BAP_parameter_to_string(param));

    switch (param) {
        case BAP_PARAM_SYSTEM_INFO:
            BAP_send_message(BAP_CMD_RES, "deviceModel", state->DEVICE_CONFIG.family.name);
            BAP_send_message(BAP_CMD_RES, "asicModel", state->DEVICE_CONFIG.family.asic.name);
            char port_str[6];
            snprintf(port_str, sizeof(port_str),"%u", state->SYSTEM_MODULE.pool_port);
            BAP_send_message(BAP_CMD_RES, "pool", state->SYSTEM_MODULE.pool_url);
            BAP_send_message(BAP_CMD_RES, "poolPort", port_str);
            BAP_send_message(BAP_CMD_RES, "poolUser", state->SYSTEM_MODULE.pool_user);
            break;
        case BAP_PARAM_SHARES:
            {
                char shares_ar_str[64];
                snprintf(shares_ar_str, sizeof(shares_ar_str), "%llu/%llu", state->SYSTEM_MODULE.shares_accepted, state->SYSTEM_MODULE.shares_rejected);
                BAP_send_message(BAP_CMD_RES, "shares", shares_ar_str);
            }
            break;
        case BAP_PARAM_BLOCK_HEIGHT:
            {
                char block_height_str[32];
                snprintf(block_height_str, sizeof(block_height_str), "%d", state->block_height);
                BAP_send_message(BAP_CMD_RES, "block_height", block_height_str);
            }
            break;
        default:
            ESP_LOGE(TAG, "Unsupported request parameter: %d", param);
            break;
    }
}

void BAP_handle_settings(const char *parameter, const char *value) {
    //ESP_LOGI(TAG, "Handling settings change for parameter: %s, value: %s",
    //         parameter ? parameter : "NULL", value ? value : "NULL");
    
    if (!parameter || !value) {
        ESP_LOGE(TAG, "Invalid settings parameters");
        BAP_send_message(BAP_CMD_ERR, parameter ? parameter : "unknown", "missing_parameter");
        return;
    }

    if (!bap_global_state) {
        ESP_LOGE(TAG, "Global state not available for settings");
        BAP_send_message(BAP_CMD_ERR, parameter, "system_not_ready");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    
    // In AP mode, only allow SSID and password settings
    if (!bap_global_state->SYSTEM_MODULE.is_connected) {
        if (param != BAP_PARAM_SSID && param != BAP_PARAM_PASSWORD) {
            ESP_LOGW(TAG, "Setting '%s' not allowed in AP mode", parameter);
            BAP_send_message(BAP_CMD_ERR, parameter, "ap_mode_limited_settings");
            return;
        }
        //ESP_LOGI(TAG, "AP mode: allowing WiFi credential setting for %s", parameter);
    }
    
    switch (param) {
        case BAP_PARAM_FREQUENCY:
            {
                float target_frequency = atof(value);
                
                if (target_frequency < 100.0f || target_frequency > 800.0f) {
                    ESP_LOGE(TAG, "Invalid frequency value: %.2f MHz (valid range: 100-800 MHz)", target_frequency);
                    BAP_send_message(BAP_CMD_ERR, parameter, "invalid_range");
                    return;
                }
                
                //ESP_LOGI(TAG, "Setting ASIC frequency to %.2f MHz", target_frequency);
                
                bool success = ASIC_set_frequency(bap_global_state, target_frequency);
                
                if (success) {
                    //ESP_LOGI(TAG, "Frequency successfully set to %.2f MHz", target_frequency);
                    
                    bap_global_state->POWER_MANAGEMENT_MODULE.frequency_value = target_frequency;
                    nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, target_frequency);
                    
                    char freq_str[32];
                    snprintf(freq_str, sizeof(freq_str), "%.2f", target_frequency);
                    BAP_send_message(BAP_CMD_ACK, parameter, freq_str);
                } else {
                    ESP_LOGE(TAG, "Failed to set frequency to %.2f MHz", target_frequency);
                    BAP_send_message(BAP_CMD_ERR, parameter, "set_failed");
                }
            }
            break;

        case BAP_PARAM_ASIC_VOLTAGE:
            {
                uint16_t target_voltage_mv = (uint16_t)atoi(value);

                if (target_voltage_mv < 700 || target_voltage_mv > 1400) {
                    ESP_LOGE(TAG, "Invalid voltage value: %d mV (valid range: 700-1400 mV)", target_voltage_mv);
                    BAP_send_message(BAP_CMD_ERR, parameter, "invalid_range");
                    return;
                }

                //ESP_LOGI(TAG, "Setting ASIC voltage to %d mV", target_voltage_mv);

                nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, target_voltage_mv);
                //ESP_LOGI(TAG, "Voltage successfully set to %d mV", target_voltage_mv);

                char voltage_str[32];
                snprintf(voltage_str, sizeof(voltage_str), "%d", target_voltage_mv);
                BAP_send_message(BAP_CMD_ACK, parameter, voltage_str);
            }
            break;

        case BAP_PARAM_SSID:
            {
                char *current_ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID);

                if (current_ssid && strcmp(current_ssid, value) == 0) {
                    //ESP_LOGI(TAG, "WiFi SSID is already set to: %s", value);
                    BAP_send_message(BAP_CMD_ACK, parameter, value);
                    free(current_ssid);
                    return;
                } else if (!current_ssid || strcmp(current_ssid, value) != 0) {
                    nvs_config_set_string(NVS_CONFIG_WIFI_SSID, value);
                    //ESP_LOGI(TAG, "WiFi SSID set to: %s", value);
                    BAP_send_message(BAP_CMD_ACK, parameter, value);
                    if (current_ssid) free(current_ssid);
                } else {
                    ESP_LOGE(TAG, "Failed to set WiFi SSID");
                    BAP_send_message(BAP_CMD_ERR, parameter, "set_failed");
                    if (current_ssid) free(current_ssid);
                }
            }
            break;
        
        case BAP_PARAM_PASSWORD:
            {   
                char *current_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS);

                if (current_pass && strcmp(current_pass, value) == 0) {
                    //ESP_LOGI(TAG, "WiFi password is already set");
                    BAP_send_message(BAP_CMD_ACK, parameter, "password_already_set");
                    free(current_pass);
                    return;
                } else if (!current_pass || strcmp(current_pass, value) != 0) {
                    nvs_config_set_string(NVS_CONFIG_WIFI_PASS, value);
                    //ESP_LOGI(TAG, "WiFi password set");
                    BAP_send_message(BAP_CMD_ACK, parameter, "password_set");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    //ESP_LOGI(TAG, "Restarting to apply new WiFi settings");
                    BAP_send_message(BAP_CMD_STA, "status", "restarting");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                } else {
                    ESP_LOGE(TAG, "Failed to set WiFi password");
                    BAP_send_message(BAP_CMD_ERR, parameter, "set_failed");
                }
            }
            break;

        case BAP_PARAM_FAN_SPEED:
            {
                uint16_t fan_speed = (uint16_t)atoi(value);

                if (fan_speed > 100) {
                    ESP_LOGE(TAG, "Invalid fan speed value: %d%% (valid range: 0-100%%)", fan_speed);
                    BAP_send_message(BAP_CMD_ERR, parameter, "invalid_range");
                    return;
                }
                //ESP_LOGI(TAG, "Setting fan speed to %d%%", fan_speed);
                nvs_config_set_bool(NVS_CONFIG_AUTO_FAN_SPEED, false);
                nvs_config_set_u16(NVS_CONFIG_MANUAL_FAN_SPEED, fan_speed);
            }
            break;
        case BAP_PARAM_AUTO_FAN_SPEED:
            {
                uint16_t auto_fan_speed = (uint16_t)atoi(value);

                if (auto_fan_speed > 1) {
                    ESP_LOGE(TAG, "Invalid auto fan speed value: %d (valid range: 0-1)", auto_fan_speed);
                    BAP_send_message(BAP_CMD_ERR, parameter, "invalid_range");
                    return;
                }
                //ESP_LOGI(TAG, "Setting auto fan speed to %d", auto_fan_speed);
                nvs_config_set_bool(NVS_CONFIG_AUTO_FAN_SPEED, auto_fan_speed);
                BAP_send_message(BAP_CMD_ACK, parameter, "auto_fan_speed_set");
                return;
            }
            break;
            
        default:
            ESP_LOGE(TAG, "Unsupported settings parameter: %s", parameter);
            BAP_send_message(BAP_CMD_ERR, parameter, "unsupported_parameter");
            break;
    }
}

esp_err_t BAP_handlers_init(GlobalState *state) {
    // Clear all handlers
    memset(handlers, 0, sizeof(handlers));
    
    // Register default handlers
    BAP_register_handler(BAP_CMD_SUB, BAP_handle_subscription);
    BAP_register_handler(BAP_CMD_UNSUB, BAP_handle_unsubscription);
    BAP_register_handler(BAP_CMD_REQ, BAP_handle_request);
    BAP_register_handler(BAP_CMD_SET, BAP_handle_settings);
    
    //ESP_LOGI(TAG, "BAP handlers initialized");
    return ESP_OK;
}
