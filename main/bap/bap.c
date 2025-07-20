#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_config.h"

#include "bap.h"
#include "device_config.h"
#include "asic.h"

#define BAP_MAX_MESSAGE_LEN 256
#define BAP_UART_NUM UART_NUM_2
#define BAP_BUF_SIZE 1024

#define GPIO_BAP_RX CONFIG_GPIO_BAP_RX
#define GPIO_BAP_TX CONFIG_GPIO_BAP_TX

static const char *TAG = "BAP";

static bap_subscription_t subscriptions[BAP_PARAM_UNKNOWN] = {0};
static GlobalState *global_state = NULL;
static TaskHandle_t uart_receive_task_handle = NULL;
static TaskHandle_t subscription_task_handle = NULL;
static SemaphoreHandle_t subscription_mutex = NULL;

static bap_command_handler_t handlers[BAP_CMD_UNKNOWN + 1] = {0};

static const char *parameter_strings[] = {
    "systemInfo",
    "hashrate",
    "temperature",
    "power",
    "voltage",
    "current",
    "shares",
    "frequency",
    "asic_voltage",
    "ssid",
    "password"
};

bap_parameter_t BAP_parameter_from_string(const char *param_str) {
    for (int i = 0; i < BAP_PARAM_UNKNOWN; ++i) {
        if (strcmp(param_str, parameter_strings[i]) == 0) {
            return (bap_parameter_t)i;
        }
    }
    return BAP_PARAM_UNKNOWN;
}

const char* BAP_parameter_to_string(bap_parameter_t param) {
    if (param >= 0 && param < BAP_PARAM_UNKNOWN) {
        return parameter_strings[param];
    }
    return "unknown";
}

static const char *command_strings[] = {
    "REQ", "RES", "SUB", "UNSUB", "SET", "ACK", "ERR", "CMD", "STA", "LOG"
};

bap_command_t BAP_command_from_string(const char *cmd_str) {
    for (int i = 0; i < BAP_CMD_UNKNOWN; ++i) {
        if (strcmp(cmd_str, command_strings[i]) == 0) {
            return (bap_command_t)i;
        }
    }
    return BAP_CMD_UNKNOWN;
}

const char* BAP_command_to_string(bap_command_t cmd) {
    if (cmd >= 0 && cmd < BAP_CMD_UNKNOWN) {
        return command_strings[cmd];
    }
    return "UNK";
}

uint8_t BAP_calculate_checksum(const char *sentence_body) {
    uint8_t checksum = 0;
    ESP_LOGI(TAG, "Calculating checksum for: %s", sentence_body);
    
    while (*sentence_body) {
        ESP_LOGD(TAG, "XOR: 0x%02X ^ 0x%02X = 0x%02X",
                checksum, (uint8_t)(*sentence_body),
                checksum ^ (uint8_t)(*sentence_body));
        checksum ^= (uint8_t)(*sentence_body++);
    }
    
    ESP_LOGI(TAG, "Final checksum: 0x%02X", checksum);
    return checksum;
}

void BAP_send_message(bap_command_t cmd, const char *parameter, const char *value) {
    char message[BAP_MAX_MESSAGE_LEN];
    char sentence_body[BAP_MAX_MESSAGE_LEN];
    int len;

    if (value && strlen(value) > 0) {
        snprintf(sentence_body, sizeof(sentence_body), "BAP,%s,%s,%s",
                 BAP_command_to_string(cmd), parameter, value);
    } else {
        snprintf(sentence_body, sizeof(sentence_body), "BAP,%s,%s",
                 BAP_command_to_string(cmd), parameter);
    }

    uint8_t checksum = BAP_calculate_checksum(sentence_body);

    len = snprintf(message, sizeof(message), "$%s*%02X\r\n", sentence_body, checksum);

    uart_write_bytes(BAP_UART_NUM, message, len);
    
    // debugging
    ESP_LOGI(TAG, "Sent: %s", message);
}

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

    ESP_LOGI(TAG, "Parsing message: %s", message);
    
    size_t len = strlen(message);
    if (len < 5) { // At minimum need $BAP,X
        ESP_LOGE(TAG, "Parse message: Too short (%d chars)", len);
        return; // Too short to be valid
    }

    // Check start character
    if (message[0] != '$') {
        ESP_LOGE(TAG, "Parse message: Doesn't start with $");
        return;
    }

    // Find '*' for checksum if present
    const char *asterisk = strchr(message, '*');
    char sentence_body[BAP_MAX_MESSAGE_LEN];
    
    if (asterisk) {
        // Checksum is present, validate it
        // Extract checksum from message
        if ((asterisk - message) + 3 > len) {
            ESP_LOGE(TAG, "Parse message: Not enough room for checksum");
            return; // Not enough room for checksum + \r\n
        }

        uint8_t received_checksum = 0;
        sscanf(asterisk + 1, "%2hhX", &received_checksum);

        // Calculate checksum over sentence body (between '$' and '*')
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
            
            // Check if this is a subscription command - be lenient with checksums for SUB commands
            if (strstr(sentence_body, "BAP,SUB,") == sentence_body) {
                ESP_LOGI(TAG, "Subscription command - ignoring checksum mismatch");
            } else {
                ESP_LOGE(TAG, "Non-subscription command with invalid checksum, rejecting");
                return;
            }
        }
    } else {
        // No checksum, just use the body after $ until end or newline
        size_t body_len = len - 1; // Skip the $
        
        // Find end of message (CR or LF)
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
        
        // Only allow subscription commands without checksum
        if (strstr(sentence_body, "BAP,SUB,") == sentence_body ||
            strstr(sentence_body, "BAP,UNSUB,") == sentence_body) {
            ESP_LOGI(TAG, "Subscription command without checksum, accepted");
        } else {
            ESP_LOGE(TAG, "Non-subscription command without checksum, rejecting");
            return;
        }
    }

    // Make a copy of sentence_body for tokenization (strtok_r modifies the string)
    char tokenize_body[BAP_MAX_MESSAGE_LEN];
    strcpy(tokenize_body, sentence_body);
    
    // Tokenize sentence_body: "BAP,CMD,PARAM[,VALUE]"
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

    char *value = strtok_r(NULL, ",", &saveptr); // May be NULL

    bap_command_t cmd = BAP_command_from_string(cmd_str);

    if (cmd == BAP_CMD_UNKNOWN) {
        ESP_LOGE(TAG, "Parse message: Unknown command: %s", cmd_str);
        return;
    }

    if (handlers[cmd]) {
        ESP_LOGI(TAG, "Calling handler for command: %s with parameter: %s", cmd_str, parameter);
        handlers[cmd](parameter, value);
    } else {
        ESP_LOGE(TAG, "No handler registered for command: %s", cmd_str);
    }
}

void BAP_send_init_message(GlobalState *state) {
    const char *init_message = "BAP UART Interface Initialized\r\n";
    esp_err_t ret = uart_write_bytes(BAP_UART_NUM, init_message, strlen(init_message));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to send init message: %d", ret);
    } else {
        ESP_LOGI(TAG, "Init message sent: %s", init_message);
    }
}

void BAP_handle_subscription(const char *parameter, const char *value) {
    ESP_LOGI(TAG, "Handling subscription request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid subscription parameter");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    ESP_LOGI(TAG, "Parameter ID: %d (from string: %s)", param, parameter);
    
    if (param == BAP_PARAM_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown subscription parameter: %s", parameter);
        return;
    }

    // Take the mutex to protect the subscriptions array
    if (subscription_mutex != NULL && xSemaphoreTake(subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        
        // Activate the subscription and set subscribe timestamp
        subscriptions[param].active = true;
        subscriptions[param].last_subscribe = current_time;
        subscriptions[param].last_response = 0;
        subscriptions[param].update_interval_ms = 5000; // will be reduced later if it's safe to do so

        // If value is provided, it's the update interval
        if (value) {
            int interval = atoi(value);
            if (interval > 0) {
                subscriptions[param].update_interval_ms = interval;
            }
        }

        ESP_LOGI(TAG, "Subscription activated for %s with interval %lu ms",
                 parameter_strings[param], subscriptions[param].update_interval_ms);

        ESP_LOGI(TAG, "Current subscription status:");
        for (int i = 0; i < BAP_PARAM_UNKNOWN; i++) {
            ESP_LOGI(TAG, "  %s: active=%d, interval=%lu ms",
                     parameter_strings[i], subscriptions[i].active,
                     subscriptions[i].update_interval_ms);
        }

        // acknowledgment
        BAP_send_message(BAP_CMD_ACK, parameter, "subscribed");

        xSemaphoreGive(subscription_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take subscription mutex");
    }
}

void BAP_handle_unsubscription(const char *parameter, const char *value) {
    ESP_LOGI(TAG, "Handling unsubscription request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid unsubscription parameter");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    if (param == BAP_PARAM_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown unsubscription parameter: %s", parameter);
        return;
    }

    // Take the mutex to protect the subscriptions array
    if (subscription_mutex != NULL && xSemaphoreTake(subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Deactivate the subscription
        subscriptions[param].active = false;
        
        ESP_LOGI(TAG, "Subscription deactivated for %s", parameter_strings[param]);

        // acknowledgment
        BAP_send_message(BAP_CMD_ACK, parameter, "unsubscribed");

        xSemaphoreGive(subscription_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take subscription mutex");
    }
}

void BAP_handle_request(const char *parameter, const char *value) {
    ESP_LOGI(TAG, "Handling request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid request parameter");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    if (param == BAP_PARAM_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown request parameter: %s", parameter);
        return;
    }

    if (!global_state) {
        ESP_LOGE(TAG, "Global state not available for request");
        return;
    }

    BAP_send_request(param, global_state);
}

void BAP_send_request(bap_parameter_t param, GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state for request");
        return;
    }

    ESP_LOGI(TAG, "Sending request response for %s", BAP_parameter_to_string(param));

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
            
        default:
            ESP_LOGE(TAG, "Unsupported request parameter: %d", param);
            break;
    }
}

void BAP_handle_settings(const char *parameter, const char *value) {
    ESP_LOGI(TAG, "Handling settings change for parameter: %s, value: %s",
             parameter ? parameter : "NULL", value ? value : "NULL");
    
    if (!parameter || !value) {
        ESP_LOGE(TAG, "Invalid settings parameters");
        BAP_send_message(BAP_CMD_ERR, parameter ? parameter : "unknown", "missing_parameter");
        return;
    }

    if (!global_state) {
        ESP_LOGE(TAG, "Global state not available for settings");
        BAP_send_message(BAP_CMD_ERR, parameter, "system_not_ready");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    
    switch (param) {
        case BAP_PARAM_FREQUENCY:
            {
                float target_frequency = atof(value);
                
                if (target_frequency < 100.0f || target_frequency > 800.0f) {
                    ESP_LOGE(TAG, "Invalid frequency value: %.2f MHz (valid range: 100-800 MHz)", target_frequency);
                    BAP_send_message(BAP_CMD_ERR, parameter, "invalid_range");
                    return;
                }
                
                ESP_LOGI(TAG, "Setting ASIC frequency to %.2f MHz", target_frequency);
                
                bool success = ASIC_set_frequency(global_state, target_frequency);
                
                if (success) {
                    ESP_LOGI(TAG, "Frequency successfully set to %.2f MHz", target_frequency);
                    
                    global_state->POWER_MANAGEMENT_MODULE.frequency_value = target_frequency;
                    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, target_frequency);
                    
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

                ESP_LOGI(TAG, "Setting ASIC voltage to %d mV", target_voltage_mv);

                nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, target_voltage_mv);
                ESP_LOGI(TAG, "Voltage successfully set to %d mV", target_voltage_mv);

                char voltage_str[32];
                snprintf(voltage_str, sizeof(voltage_str), "%d", target_voltage_mv);
                BAP_send_message(BAP_CMD_ACK, parameter, voltage_str);
            }
            break;

        case BAP_PARAM_SSID:
            {
                char *current_ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "myssid");

                if (current_ssid && strcmp(current_ssid, value) == 0) {
                    ESP_LOGI(TAG, "WiFi SSID is already set to: %s", value);
                    BAP_send_message(BAP_CMD_ACK, parameter, value);
                    free(current_ssid);
                    return;
                } else if (!current_ssid || strcmp(current_ssid, value) != 0) {
                    nvs_config_set_string(NVS_CONFIG_WIFI_SSID, value);
                    ESP_LOGI(TAG, "WiFi SSID set to: %s", value);
                    BAP_send_message(BAP_CMD_ACK, parameter, value);
                    if (current_ssid) free(current_ssid);
                } else {
                    ESP_LOGE(TAG, "Failed to set WiFi SSID");
                    BAP_send_message(BAP_CMD_ERR, parameter, "set_failed");
                    if (current_ssid) free(current_ssid);
                }
            }
            break;
        
        // needs some refinement on how to actually handle the restart and implement a wifi
        // handler for restarting only if a save command has been sent
        case BAP_PARAM_PASSWORD:
            {   
                char *current_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS, "mypass");

                if (current_pass && strcmp(current_pass, value) == 0) {
                    ESP_LOGI(TAG, "WiFi password is already set");
                    BAP_send_message(BAP_CMD_ACK, parameter, "password_already_set");
                    free(current_pass);
                    return;
                } else if (!current_pass || strcmp(current_pass, value) != 0) {
                    nvs_config_set_string(NVS_CONFIG_WIFI_PASS, value);
                    ESP_LOGI(TAG, "WiFi password set");
                    BAP_send_message(BAP_CMD_ACK, parameter, "password_set");
                    vTaskDelay(pdMS_TO_TICKS(100)); // Give some time for the message to be sent
                    ESP_LOGI(TAG, "Restarting to apply new WiFi settings");
                    BAP_send_message(BAP_CMD_STA, "status", "restarting");
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Give some time for the message to be sent
                    // Restart the system to apply new WiFi settings
                    esp_restart(); // Restart to apply new WiFi settings
                    // !TODO: Implement a proper WiFi handler to restart only if a save command has been sent
                } else {
                    ESP_LOGE(TAG, "Failed to set WiFi password");
                    BAP_send_message(BAP_CMD_ERR, parameter, "set_failed");
                }
            }
            
        default:
            ESP_LOGE(TAG, "Unsupported settings parameter: %s", parameter);
            BAP_send_message(BAP_CMD_ERR, parameter, "unsupported_parameter");
            break;
    }
}

static void uart_receive_task(void *pvParameters) {
    uint8_t *data = (uint8_t *) malloc(BAP_BUF_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate memory for UART receive buffer");
        vTaskDelete(NULL);
        return;
    }

    char message[BAP_MAX_MESSAGE_LEN + 1]; // +1 for null terminator
    size_t message_len = 0;
    bool in_message = false;

    while (1) {
        // Read data from UART
        int len = uart_read_bytes(BAP_UART_NUM, data, BAP_BUF_SIZE, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            ESP_LOGD(TAG, "Received %d bytes from UART", len);
            
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                
                if (c == '$') {
                    ESP_LOGI(TAG, "Start of message detected");
                    in_message = true;
                    message_len = 0;
                    message[message_len++] = c;
                }
                // End of message (accept either \r, \n, or both as line ending)
                else if ((c == '\n' || c == '\r') && in_message) {
                    // Only process if we have more than just the $ character
                    if (message_len > 1 && message_len < BAP_MAX_MESSAGE_LEN) {
                        // Add the current character
                        message[message_len++] = c;
                        message[message_len] = '\0';
                        
                        // Process the message regardless of checksum presence
                        ESP_LOGI(TAG, "Received complete message: %s", message);
                        BAP_parse_message(message);
                        
                        // If we got \r, wait for possible \n to follow before resetting
                        if (c == '\r') {
                            // Continue collecting, don't reset in_message yet
                            ESP_LOGD(TAG, "Got CR, waiting for possible LF");
                        } else {
                            // Got \n, reset message state
                            in_message = false;
                        }
                    } else if (message_len >= BAP_MAX_MESSAGE_LEN) {
                        ESP_LOGE(TAG, "Message too long, discarding");
                        in_message = false;
                    }
                }
                // Add character to message
                else if (in_message && message_len < BAP_MAX_MESSAGE_LEN) {
                    message[message_len++] = c;
                    ESP_LOGD(TAG, "Added to message: %c, len now %d", c, message_len);
                }
            }
        }
    }

    free(data);
    vTaskDelete(NULL);
}

esp_err_t BAP_start_uart_receive_task(void) {
    // Register the subscription, unsubscription, and request handlers
    BAP_register_handler(BAP_CMD_SUB, BAP_handle_subscription);
    BAP_register_handler(BAP_CMD_UNSUB, BAP_handle_unsubscription);
    BAP_register_handler(BAP_CMD_REQ, BAP_handle_request);
    BAP_register_handler(BAP_CMD_SET, BAP_handle_settings);
    
    // mutex for subscription access
    subscription_mutex = xSemaphoreCreateMutex();
    if (subscription_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create subscription mutex");
        return ESP_FAIL;
    }
    
    // UART receive task
    BaseType_t ret = xTaskCreate(uart_receive_task, "bap_uart_rx", 8192, NULL, 10, &uart_receive_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART receive task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "UART receive task started");
    return ESP_OK;
}

void BAP_send_subscription_update(GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state");
        return;
    }

    uint32_t current_time = esp_timer_get_time() / 1000; // Convert to milliseconds

    if (subscription_mutex != NULL && xSemaphoreTake(subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        const uint32_t SUBSCRIPTION_TIMEOUT_MS = 5 * 60 * 1000;

        for (int i = 0; i < BAP_PARAM_UNKNOWN; i++) {
            // Check for subscription timeout (5 minutes without refresh)
            if (subscriptions[i].active &&
                (current_time - subscriptions[i].last_subscribe > SUBSCRIPTION_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "Subscription for %s timed out after 5 minutes, deactivating",
                         parameter_strings[i]);
                subscriptions[i].active = false;
                BAP_send_message(BAP_CMD_STA, parameter_strings[i], "subscription_timeout");
                continue;
            }
            
            if (subscriptions[i].active &&
                (current_time - subscriptions[i].last_response >= subscriptions[i].update_interval_ms)) {
                
                ESP_LOGI(TAG, "Sending update for %s", parameter_strings[i]);
                
                subscriptions[i].last_response = current_time;
                
                switch (i) {   
                    case BAP_PARAM_HASHRATE:
                        {
                            char hashrate_str[32];
                            snprintf(hashrate_str, sizeof(hashrate_str), "%.2f", state->SYSTEM_MODULE.current_hashrate);
                            BAP_send_message(BAP_CMD_RES, "hashrate", hashrate_str);
                        }
                        break;
                        
                    case BAP_PARAM_TEMPERATURE:
                        {
                            char temp_str[32];
                            snprintf(temp_str, sizeof(temp_str), "%f", state->POWER_MANAGEMENT_MODULE.chip_temp_avg);
                            BAP_send_message(BAP_CMD_RES, "chipTemp", temp_str);
                            
                            snprintf(temp_str, sizeof(temp_str), "%f", state->POWER_MANAGEMENT_MODULE.vr_temp);
                            BAP_send_message(BAP_CMD_RES, "vrTemp", temp_str);
                        }
                        break;
                        
                    case BAP_PARAM_POWER:
                        {
                            char power_str[32];
                            snprintf(power_str, sizeof(power_str), "%.2f", state->POWER_MANAGEMENT_MODULE.power);
                            BAP_send_message(BAP_CMD_RES, "power", power_str);
                        }
                        break;
                        
                    case BAP_PARAM_VOLTAGE:
                        {
                            char voltage_str[32];
                            snprintf(voltage_str, sizeof(voltage_str), "%.2f", state->POWER_MANAGEMENT_MODULE.voltage);
                            BAP_send_message(BAP_CMD_RES, "voltage", voltage_str);
                        }
                        break;
                        
                    case BAP_PARAM_CURRENT:
                        {
                            char current_str[32];
                            snprintf(current_str, sizeof(current_str), "%.2f", state->POWER_MANAGEMENT_MODULE.current);
                            BAP_send_message(BAP_CMD_RES, "current", current_str);
                        }
                        break;
                        
                    case BAP_PARAM_SHARES:
                        {
                            char shares_str[32];
                            snprintf(shares_str, sizeof(shares_str), "%llu", state->SYSTEM_MODULE.shares_accepted);
                            BAP_send_message(BAP_CMD_RES, "sharesAccepted", shares_str);
                            
                            snprintf(shares_str, sizeof(shares_str), "%llu", state->SYSTEM_MODULE.shares_rejected);
                            BAP_send_message(BAP_CMD_RES, "sharesRejected", shares_str);
                        }
                        break;
                        
                    default:
                        break;
                }
                
            }
        }
        
        xSemaphoreGive(subscription_mutex);
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

esp_err_t BAP_start_subscription_task(GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state");
        return ESP_ERR_INVALID_ARG;
    }
    
    global_state = state;
    
    BaseType_t ret = xTaskCreate(subscription_update_task, "bap_sub_update", 4096, state, 5, &subscription_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create subscription update task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Subscription update task started");
    return ESP_OK;
}

esp_err_t BAP_init(void) {
    ESP_LOGI(TAG, "Initializing BAP UART interface");
    
    memset(handlers, 0, sizeof(handlers));
    
    memset(subscriptions, 0, sizeof(subscriptions));
    
    if (GPIO_BAP_TX > 47 || GPIO_BAP_RX > 47) {
        ESP_LOGE(TAG, "Invalid GPIO pins: TX=%d, RX=%d", GPIO_BAP_TX, GPIO_BAP_RX);
        return ESP_ERR_INVALID_ARG;
    }
    
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    
    esp_err_t ret = uart_param_config(BAP_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters: %d", ret);
        return ret;
    }
    
    ret = uart_set_pin(BAP_UART_NUM, GPIO_BAP_TX, GPIO_BAP_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %d", ret);
        return ret;
    }
    
    ret = uart_driver_install(BAP_UART_NUM, BAP_BUF_SIZE, BAP_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %d", ret);
        return ret;
    }
    
    ESP_LOGI(TAG, "BAP UART interface initialized successfully");
    
    BAP_send_init_message(global_state);
    
    BAP_start_subscription_task(global_state);
    ret = BAP_start_uart_receive_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start UART receive task");
        return ret;
    }
    
    return ESP_OK;
}
