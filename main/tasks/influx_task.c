#include <stdint.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "influx.h"
#include "global_state.h"
#include "tasks/influx_task.h"
#include "thermal/thermal.h"

static const char *TAG = "influx_task";

void influx_task(void *pvParameters) {
    GlobalState *state = (GlobalState *)pvParameters;
    influx_client_t *client = (influx_client_t *)state->influx_client;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(30000); // 30 seconds

    ESP_LOGI(TAG, "InfluxDB stats task started");

    while (1) {
        if (client != NULL) {
            // Update stats in the client structure
            client->stats.hashingSpeed = state->SYSTEM_MODULE.current_hashrate;

            // System module stats
            client->stats.invalidShares = state->SYSTEM_MODULE.shares_rejected;
            client->stats.validShares = state->SYSTEM_MODULE.shares_accepted;
            client->stats.difficulty = state->stratum_difficulty;
            client->stats.bestSessionDifficulty = state->SYSTEM_MODULE.best_session_nonce_diff;
            client->stats.accepted = state->SYSTEM_MODULE.shares_accepted;
            client->stats.notAccepted = state->SYSTEM_MODULE.shares_rejected;
            client->stats.totalUptime = (esp_timer_get_time() - state->SYSTEM_MODULE.start_time) / 1000000;
            client->stats.totalBestDifficulty = state->SYSTEM_MODULE.best_nonce_diff;
            client->stats.uptime = state->SYSTEM_MODULE.duration_start;
            client->stats.blocksFound = state->SYSTEM_MODULE.FOUND_BLOCK ? 1 : 0;

            // Power management stats
            client->stats.asicTemp = state->POWER_MANAGEMENT_MODULE.chip_temp_avg;
            client->stats.vrTemp = state->POWER_MANAGEMENT_MODULE.vr_temp;
            client->stats.voltage = state->POWER_MANAGEMENT_MODULE.voltage;
            client->stats.current = state->POWER_MANAGEMENT_MODULE.current;
            client->stats.power = state->POWER_MANAGEMENT_MODULE.power;

            if (!influx_write(client)) {
                ESP_LOGW(TAG, "Failed to write stats to InfluxDB");
            }
        }

        // Wait for the next cycle using vTaskDelayUntil for precise timing
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

bool influx_init_and_start(GlobalState *state, const char *host, int port, const char *token, const char *bucket, const char *org, const char *prefix) {
    if (state == NULL) {
        ESP_LOGE(TAG, "Global state is NULL");
        return false;
    }

    ESP_LOGI(TAG, "Initializing InfluxDB client");

    // Initialize InfluxDB client
    influx_client_t *influx_client = malloc(sizeof(influx_client_t));
    if (influx_client == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for InfluxDB client");
        return false;
    }

    bool influx_success = influx_init(influx_client, host, port, token, bucket, org, prefix);
    if (!influx_success) {
        ESP_LOGE(TAG, "Failed to initialize InfluxDB client");
        free(influx_client);
        state->influx_client = NULL;
        return false;
    }

    ESP_LOGI(TAG, "InfluxDB client initialized successfully");
    state->influx_client = influx_client;
    return true;
}