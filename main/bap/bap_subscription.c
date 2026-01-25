/**
 * @file bap_subscription.c
 * @brief BAP subscription management
 * 
 * Handles parameter subscriptions, periodic updates, and subscription timeouts.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bap_subscription.h"
#include "bap_protocol.h"
#include "bap_uart.h"
#include "bap.h"
#include "connect.h"
#include "nvs_config.h"

static const char *TAG = "BAP_SUBSCRIPTION";

static bap_subscription_t subscriptions[BAP_PARAM_UNKNOWN] = {0};
static TaskHandle_t subscription_task_handle = NULL;

typedef struct {
    char hashrate[32];
    char chip_temp[32];
    char vr_temp[32];
    char power[32];
    char voltage[32];
    char current[32];
    char shares[64];
    char fan_speed[32];
    char best_difficulty[32];
    char block_height[32];
    char wifi_ssid[64];
    char wifi_password[64];
    char wifi_rssi[32];
    char wifi_ip[32];
} bap_last_values_t;

typedef struct {
    bool hashrate;
    bool chip_temp;
    bool vr_temp;
    bool power;
    bool voltage;
    bool current;
    bool shares;
    bool fan_speed;
    bool best_difficulty;
    bool block_height;
    bool wifi_ssid;
    bool wifi_password;
    bool wifi_rssi;
    bool wifi_ip;
} bap_last_values_valid_t;

static bap_last_values_t last_values = {0};
static bap_last_values_valid_t last_values_valid = {0};

static void subscription_update_task(void *pvParameters);

static void BAP_send_if_changed(const char *parameter, const char *value, char *last, size_t last_size, bool *is_valid) {
    const char *safe_value = value ? value : "";

    if (*is_valid && strcmp(last, safe_value) == 0) {
        return;
    }

    snprintf(last, last_size, "%s", safe_value);
    *is_valid = true;
    BAP_send_message_with_queue(BAP_CMD_RES, parameter, safe_value);
}

esp_err_t BAP_subscription_init(void) {
    //ESP_LOGI(TAG, "Initializing BAP subscription management");
    
    memset(subscriptions, 0, sizeof(subscriptions));
    memset(&last_values, 0, sizeof(last_values));
    memset(&last_values_valid, 0, sizeof(last_values_valid));
    
    //ESP_LOGI(TAG, "BAP subscription management initialized");
    return ESP_OK;
}

void BAP_subscription_handle_subscribe(const char *parameter, const char *value) {
    //ESP_LOGI(TAG, "Handling subscription request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid subscription parameter");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    //ESP_LOGI(TAG, "Parameter ID: %d (from string: %s)", param, parameter);
    
    if (param == BAP_PARAM_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown subscription parameter: %s", parameter);
        return;
    }

    // Take the mutex to protect the subscriptions array
    if (bap_subscription_mutex != NULL && xSemaphoreTake(bap_subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        
        subscriptions[param].active = true;
        subscriptions[param].last_subscribe = current_time;
        subscriptions[param].last_response = 0;
        subscriptions[param].update_interval_ms = 3000;

        switch (param) {
            case BAP_PARAM_HASHRATE:
                last_values_valid.hashrate = false;
                break;
            case BAP_PARAM_TEMPERATURE:
                last_values_valid.chip_temp = false;
                last_values_valid.vr_temp = false;
                break;
            case BAP_PARAM_POWER:
                last_values_valid.power = false;
                break;
            case BAP_PARAM_VOLTAGE:
                last_values_valid.voltage = false;
                break;
            case BAP_PARAM_CURRENT:
                last_values_valid.current = false;
                break;
            case BAP_PARAM_SHARES:
                last_values_valid.shares = false;
                break;
            case BAP_PARAM_FAN_SPEED:
                last_values_valid.fan_speed = false;
                break;
            case BAP_PARAM_BEST_DIFFICULTY:
                last_values_valid.best_difficulty = false;
                break;
            case BAP_PARAM_BLOCK_HEIGHT:
                last_values_valid.block_height = false;
                break;
            case BAP_PARAM_WIFI:
                last_values_valid.wifi_ssid = false;
                last_values_valid.wifi_password = false;
                last_values_valid.wifi_rssi = false;
                last_values_valid.wifi_ip = false;
                break;
            default:
                break;
        }

        if (value) {
            int interval = atoi(value);
            if (interval > 0) {
                subscriptions[param].update_interval_ms = interval;
            }
        }

        ESP_LOGI(TAG, "Subscription activated for %s with interval %lu ms",
                 BAP_parameter_to_string(param), subscriptions[param].update_interval_ms);
        
        // Optionally, for debugging purposes, log the current subscription status
        //ESP_LOGI(TAG, "Current subscription status:");
        //for (int i = 0; i < BAP_PARAM_UNKNOWN; i++) {
        //    ESP_LOGI(TAG, "  %s: active=%d, interval=%lu ms",
        //             BAP_parameter_to_string((bap_parameter_t)i), subscriptions[i].active,
        //             subscriptions[i].update_interval_ms);
        //}

        BAP_send_message(BAP_CMD_ACK, parameter, "subscribed");

        xSemaphoreGive(bap_subscription_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take subscription mutex");
    }
}

void BAP_subscription_handle_unsubscribe(const char *parameter, const char *value) {
    //ESP_LOGI(TAG, "Handling unsubscription request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid unsubscription parameter");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    if (param == BAP_PARAM_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown unsubscription parameter: %s", parameter);
        return;
    }

    if (bap_subscription_mutex != NULL && xSemaphoreTake(bap_subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        subscriptions[param].active = false;
        
        //ESP_LOGI(TAG, "Subscription deactivated for %s", BAP_parameter_to_string(param));

        BAP_send_message(BAP_CMD_ACK, parameter, "unsubscribed");

        xSemaphoreGive(bap_subscription_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take subscription mutex");
    }
}

void BAP_send_subscription_update(GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state");
        return;
    }

    uint32_t current_time = esp_timer_get_time() / 1000;

    if (bap_subscription_mutex != NULL && xSemaphoreTake(bap_subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        const uint32_t SUBSCRIPTION_TIMEOUT_MS = 5 * 60 * 1000;

        for (int i = 0; i < BAP_PARAM_UNKNOWN; i++) {
            // Check for subscription timeout (5 minutes without refresh)
            if (subscriptions[i].active &&
                (current_time - subscriptions[i].last_subscribe > SUBSCRIPTION_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "Subscription for %s timed out after 5 minutes, deactivating",
                         BAP_parameter_to_string((bap_parameter_t)i));
                subscriptions[i].active = false;
                BAP_send_message_with_queue(BAP_CMD_STA, BAP_parameter_to_string((bap_parameter_t)i), "subscription_timeout");
                continue;
            }
            
            if (subscriptions[i].active &&
                (current_time - subscriptions[i].last_response >= subscriptions[i].update_interval_ms)) {
                
                //ESP_LOGI(TAG, "Sending update for %s", BAP_parameter_to_string((bap_parameter_t)i));
                
                subscriptions[i].last_response = current_time;
                
                switch (i) {   
                    case BAP_PARAM_HASHRATE:
                        {
                            char hashrate_str[32];
                            snprintf(hashrate_str, sizeof(hashrate_str), "%.2f", state->SYSTEM_MODULE.current_hashrate);
                            BAP_send_if_changed("hashrate", hashrate_str, last_values.hashrate, sizeof(last_values.hashrate), &last_values_valid.hashrate);
                        }
                        break;
                        
                    case BAP_PARAM_TEMPERATURE:
                        {
                            char temp_str[32];
                            snprintf(temp_str, sizeof(temp_str), "%f", state->POWER_MANAGEMENT_MODULE.chip_temp_avg);
                            BAP_send_if_changed("chipTemp", temp_str, last_values.chip_temp, sizeof(last_values.chip_temp), &last_values_valid.chip_temp);
                            
                            snprintf(temp_str, sizeof(temp_str), "%f", state->POWER_MANAGEMENT_MODULE.vr_temp);
                            BAP_send_if_changed("vrTemp", temp_str, last_values.vr_temp, sizeof(last_values.vr_temp), &last_values_valid.vr_temp);
                        }
                        break;
                        
                    case BAP_PARAM_POWER:
                        {
                            char power_str[32];
                            snprintf(power_str, sizeof(power_str), "%.2f", state->POWER_MANAGEMENT_MODULE.power);
                            BAP_send_if_changed("power", power_str, last_values.power, sizeof(last_values.power), &last_values_valid.power);
                        }
                        break;
                        
                    case BAP_PARAM_VOLTAGE:
                        {
                            char voltage_str[32];
                            snprintf(voltage_str, sizeof(voltage_str), "%.2f", state->POWER_MANAGEMENT_MODULE.voltage);
                            BAP_send_if_changed("voltage", voltage_str, last_values.voltage, sizeof(last_values.voltage), &last_values_valid.voltage);
                        }
                        break;
                        
                    case BAP_PARAM_CURRENT:
                        {
                            char current_str[32];
                            snprintf(current_str, sizeof(current_str), "%.2f", state->POWER_MANAGEMENT_MODULE.current);
                            BAP_send_if_changed("current", current_str, last_values.current, sizeof(last_values.current), &last_values_valid.current);
                        }
                        break;
                        
                    case BAP_PARAM_SHARES:
                        {
                            char shares_ar_str[64];
                            snprintf(shares_ar_str, sizeof(shares_ar_str), "%llu/%llu", state->SYSTEM_MODULE.shares_accepted, state->SYSTEM_MODULE.shares_rejected);
                            BAP_send_if_changed("shares", shares_ar_str, last_values.shares, sizeof(last_values.shares), &last_values_valid.shares);
                            
                        }
                        break;

                    case BAP_PARAM_FAN_SPEED:
                        {
                            char fan_speed_str[32];
                            snprintf(fan_speed_str, sizeof(fan_speed_str), "%d", state->POWER_MANAGEMENT_MODULE.fan_rpm);
                            BAP_send_if_changed("fan_speed", fan_speed_str, last_values.fan_speed, sizeof(last_values.fan_speed), &last_values_valid.fan_speed);
                        }
                        break;
                    
                    case BAP_PARAM_BEST_DIFFICULTY:
                        {
                            char best_diff_str[32];
                            snprintf(best_diff_str, sizeof(best_diff_str), "%s", state->SYSTEM_MODULE.best_diff_string);
                            BAP_send_if_changed("best_difficulty", best_diff_str, last_values.best_difficulty, sizeof(last_values.best_difficulty), &last_values_valid.best_difficulty);
                        }
                        break;

                    case BAP_PARAM_BLOCK_HEIGHT:
                        {
                            char block_height_str[32];
                            snprintf(block_height_str, sizeof(block_height_str), "%d", state->block_height);
                            BAP_send_if_changed("block_height", block_height_str, last_values.block_height, sizeof(last_values.block_height), &last_values_valid.block_height);
                        }
                        break;

                    case BAP_PARAM_WIFI:
                        {
                            char wifi_status_str[256];
                            char rssi_str[32];
                            char ip_str[32];
                            snprintf(wifi_status_str, sizeof(wifi_status_str), "%s", state->SYSTEM_MODULE.wifi_status);
                            
                            int8_t current_rssi = -128; // no connection
                            if (state->SYSTEM_MODULE.is_connected) {
                                get_wifi_current_rssi(&current_rssi);
                            }
                            snprintf(rssi_str, sizeof(rssi_str), "%d", current_rssi);
                            
                            snprintf(ip_str, sizeof(ip_str), "%s", state->SYSTEM_MODULE.ip_addr_str);
                            char *wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS);
                            if (!wifi_pass) {
                                wifi_pass = strdup("");
                            }
                            BAP_send_if_changed("wifi_ssid", state->SYSTEM_MODULE.ssid, last_values.wifi_ssid, sizeof(last_values.wifi_ssid), &last_values_valid.wifi_ssid);
                            BAP_send_if_changed("wifi_password", wifi_pass, last_values.wifi_password, sizeof(last_values.wifi_password), &last_values_valid.wifi_password);
                            BAP_send_if_changed("wifi_rssi", rssi_str, last_values.wifi_rssi, sizeof(last_values.wifi_rssi), &last_values_valid.wifi_rssi);
                            BAP_send_if_changed("wifi_ip", ip_str, last_values.wifi_ip, sizeof(last_values.wifi_ip), &last_values_valid.wifi_ip);
                            free(wifi_pass);
                        }

                    default:
                        break;
                }
            }
        }
        
        xSemaphoreGive(bap_subscription_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take subscription mutex");
    }
}

static void subscription_update_task(void *pvParameters) {
    GlobalState *state = (GlobalState *)pvParameters;
    
    while (1) {
        BAP_send_subscription_update(state);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    vTaskDelete(NULL);
}

static void mode_management_task(void *pvParameters) {
    GlobalState *state = (GlobalState *)pvParameters;
    bool was_connected = false;
    bool subscription_task_started = false;
    // will go into log dev mode
    //ESP_LOGI(TAG, "BAP mode management task started");
    
    while (1) {
        bool is_connected = state->SYSTEM_MODULE.is_connected;
        
        // Check for mode transitions
        if (!was_connected && !is_connected) {
            // AP mode - send periodic AP messages
            BAP_send_ap_message(state);
            vTaskDelay(pdMS_TO_TICKS(5000));
        } else if (!was_connected && is_connected) {
            // Transition from AP to connected mode
            //ESP_LOGI(TAG, "WiFi connected - switching to normal BAP mode");
            
            // Start subscription task for connected mode
            if (!subscription_task_started) {
                esp_err_t ret = BAP_start_subscription_task(state);
                if (ret == ESP_OK) {
                    subscription_task_started = true;
                    //ESP_LOGI(TAG, "Subscription task started for connected mode");
                } else {
                    ESP_LOGE(TAG, "Failed to start subscription task");
                }
            }
            
            was_connected = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (was_connected && is_connected) {
            // Normal connected mode - subscription task handles updates
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (was_connected && !is_connected) {
            // Transition from connected to AP mode (connection lost)
            //ESP_LOGI(TAG, "WiFi disconnected - switching to AP mode");
            was_connected = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    vTaskDelete(NULL);
}

esp_err_t BAP_start_mode_management_task(GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state");
        return ESP_ERR_INVALID_ARG;
    }
    
    xTaskCreateWithCaps(
        mode_management_task,
        "bap_mode_mgmt",
        8192,
        state,
        5,
        NULL,
        MALLOC_CAP_SPIRAM
    );

    //ESP_LOGI(TAG, "BAP mode management task started");
    return ESP_OK;
}

esp_err_t BAP_start_subscription_task(GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state");
        return ESP_ERR_INVALID_ARG;
    }
    
    xTaskCreate(
        subscription_update_task,
        "subscription_up",
        8192,
        state,
        5,
        &subscription_task_handle
    );

    //ESP_LOGI(TAG, "Subscription update task started");
    return ESP_OK;
}
