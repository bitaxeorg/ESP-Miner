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

static const char *TAG = "BAP_SUBSCRIPTION";

static bap_subscription_t subscriptions[BAP_PARAM_UNKNOWN] = {0};
static TaskHandle_t subscription_task_handle = NULL;

static void subscription_update_task(void *pvParameters);

esp_err_t BAP_subscription_init(void) {
    //ESP_LOGI(TAG, "Initializing BAP subscription management");
    
    memset(subscriptions, 0, sizeof(subscriptions));
    
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
                            BAP_send_message_with_queue(BAP_CMD_RES, "hashrate", hashrate_str);
                        }
                        break;
                        
                    case BAP_PARAM_TEMPERATURE:
                        {
                            char temp_str[32];
                            snprintf(temp_str, sizeof(temp_str), "%f", state->POWER_MANAGEMENT_MODULE.chip_temp_avg);
                            BAP_send_message_with_queue(BAP_CMD_RES, "chipTemp", temp_str);
                            
                            snprintf(temp_str, sizeof(temp_str), "%f", state->POWER_MANAGEMENT_MODULE.vr_temp);
                            BAP_send_message_with_queue(BAP_CMD_RES, "vrTemp", temp_str);
                        }
                        break;
                        
                    case BAP_PARAM_POWER:
                        {
                            char power_str[32];
                            snprintf(power_str, sizeof(power_str), "%.2f", state->POWER_MANAGEMENT_MODULE.power);
                            BAP_send_message_with_queue(BAP_CMD_RES, "power", power_str);
                        }
                        break;
                        
                    case BAP_PARAM_VOLTAGE:
                        {
                            char voltage_str[32];
                            snprintf(voltage_str, sizeof(voltage_str), "%.2f", state->POWER_MANAGEMENT_MODULE.voltage);
                            BAP_send_message_with_queue(BAP_CMD_RES, "voltage", voltage_str);
                        }
                        break;
                        
                    case BAP_PARAM_CURRENT:
                        {
                            char current_str[32];
                            snprintf(current_str, sizeof(current_str), "%.2f", state->POWER_MANAGEMENT_MODULE.current);
                            BAP_send_message_with_queue(BAP_CMD_RES, "current", current_str);
                        }
                        break;
                        
                    case BAP_PARAM_SHARES:
                        {
                            char shares_ar_str[64];
                            snprintf(shares_ar_str, sizeof(shares_ar_str), "%lld/%lld", state->SYSTEM_MODULE.shares_accepted, state->SYSTEM_MODULE.shares_rejected);
                            BAP_send_message_with_queue(BAP_CMD_RES, "shares", shares_ar_str);
                            
                        }
                        break;

                    case BAP_PARAM_FAN_SPEED:
                        {
                            char fan_speed_str[32];
                            snprintf(fan_speed_str, sizeof(fan_speed_str), "%d", state->POWER_MANAGEMENT_MODULE.fan_rpm);
                            BAP_send_message_with_queue(BAP_CMD_RES, "fan_speed", fan_speed_str);
                        }
                        break;
                    
                    case BAP_PARAM_BEST_DIFFICULTY:
                        {
                            char best_diff_str[32];
                            snprintf(best_diff_str, sizeof(best_diff_str), "%s", state->SYSTEM_MODULE.best_diff_string);
                            BAP_send_message_with_queue(BAP_CMD_RES, "best_difficulty", best_diff_str);
                        }
                        break;

                    case BAP_PARAM_WIFI:
                        {
                            char ssid_str[32];
                            char wifi_status_str[256];
                            char rssi_str[32];
                            char ip_str[32];
                            snprintf(ssid_str, sizeof(ssid_str), "%s", state->SYSTEM_MODULE.ssid);
                            snprintf(wifi_status_str, sizeof(wifi_status_str), "%s", state->SYSTEM_MODULE.wifi_status);
                            
                            int8_t current_rssi = -128; // no connection
                            if (state->SYSTEM_MODULE.is_connected) {
                                get_wifi_current_rssi(&current_rssi);
                            }
                            snprintf(rssi_str, sizeof(rssi_str), "%d", current_rssi);
                            
                            snprintf(ip_str, sizeof(ip_str), "%s", state->SYSTEM_MODULE.ip_addr_str);
                            BAP_send_message_with_queue(BAP_CMD_RES, "wifi_ssid", ssid_str);
                            BAP_send_message_with_queue(BAP_CMD_RES, "wifi_rssi", rssi_str);
                            BAP_send_message_with_queue(BAP_CMD_RES, "wifi_ip", ip_str);
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
    
    ESP_LOGI(TAG, "BAP mode management task started");
    
    while (1) {
        bool is_connected = state->SYSTEM_MODULE.is_connected;
        
        // Check for mode transitions
        if (!was_connected && !is_connected) {
            // AP mode - send periodic AP messages
            BAP_send_ap_message(state);
            vTaskDelay(pdMS_TO_TICKS(5000));
        } else if (!was_connected && is_connected) {
            // Transition from AP to connected mode
            ESP_LOGI(TAG, "WiFi connected - switching to normal BAP mode");
            
            // Start subscription task for connected mode
            if (!subscription_task_started) {
                esp_err_t ret = BAP_start_subscription_task(state);
                if (ret == ESP_OK) {
                    subscription_task_started = true;
                    ESP_LOGI(TAG, "Subscription task started for connected mode");
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
            ESP_LOGI(TAG, "WiFi disconnected - switching to AP mode");
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

    ESP_LOGI(TAG, "BAP mode management task started");
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