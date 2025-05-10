#include "mining_controller.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include <stdlib.h>

#include "asic_result_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "main.h"
#include "power_management_task.h"
#include "stratum_task.h"

#include "asic.h"
#include "power.h"
#include "serial.h"
#include "vcore.h"
#include "work_queue.h"

#include "nvs_config.h"

static const char * TAG = "mining_controller";

esp_err_t start_mining(GlobalState * global_state)
{
    ESP_LOGI(TAG, "Starting mining operations...");

    if (global_state->mining_enabled) {
        ESP_LOGW(TAG, "Mining is already enabled.");
        return ESP_OK;
    }

    BaseType_t task_created;
    bool pm_task_created_in_this_call = false;

    if (global_state->power_management_task_handle == NULL) {
        ESP_LOGI(TAG, "POWER_MANAGEMENT_task not running. Creating new task...");
        task_created = xTaskCreate(POWER_MANAGEMENT_task, "power management", 8192, (void *) global_state, 10,
                                   &global_state->power_management_task_handle);
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create POWER_MANAGEMENT_task");
            global_state->ASIC_initalized = false;
            return ESP_FAIL;
        }
        pm_task_created_in_this_call = true;
        ESP_LOGI(TAG, "POWER_MANAGEMENT_task created.");
    } else {
        ESP_LOGI(TAG, "POWER_MANAGEMENT_task is already running.");
    }

    global_state->mining_enabled = true;
    global_state->abandon_work = 0;

    // 1. Initialize hardware/modules related to mining
    ESP_LOGI(TAG, "Initializing VCORE...");
    if (VCORE_init(global_state) != ESP_OK) {
        ESP_LOGE(TAG, "VCORE_init failed. Cannot start mining.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Initializing Queues...");
    global_state->new_stratum_version_rolling_msg = false;

    queue_init(&global_state->stratum_queue);
    queue_init(&global_state->ASIC_jobs_queue);

    ESP_LOGI(TAG, "Initializing Serial for ASIC...");
    SERIAL_init();

    ESP_LOGI(TAG, "Initializing ASIC...");
    if (ASIC_init(global_state) == 0) {
        global_state->SYSTEM_MODULE.asic_status = "Chip count 0 on restart";
        ESP_LOGE(TAG, "ASIC_init failed (Chip count 0). Cannot start mining.");
        global_state->ASIC_initalized = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Setting ASIC baud rate and clearing buffer...");
    SERIAL_set_baud(ASIC_set_max_baud(global_state));
    SERIAL_clear_buffer();

    global_state->ASIC_initalized = true;

    // 2. Create tasks and store handles
    ESP_LOGI(TAG, "Creating mining tasks...");

    task_created = xTaskCreate(stratum_task, "stratum admin", 8192, (void *) global_state, 5, &global_state->stratum_task_handle);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stratum_task");
        // Rollback previous tasks
        if (pm_task_created_in_this_call && global_state->power_management_task_handle) {
            vTaskDelete(global_state->power_management_task_handle);
            global_state->power_management_task_handle = NULL;
        }
        global_state->ASIC_initalized = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "stratum_task created.");

    task_created =
        xTaskCreate(create_jobs_task, "stratum miner", 8192, (void *) global_state, 10, &global_state->create_jobs_task_handle);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create create_jobs_task");
        // Rollback previous tasks
        if (global_state->stratum_task_handle) {
            vTaskDelete(global_state->stratum_task_handle);
            global_state->stratum_task_handle = NULL;
        }
        if (pm_task_created_in_this_call && global_state->power_management_task_handle) {
            vTaskDelete(global_state->power_management_task_handle);
            global_state->power_management_task_handle = NULL;
        }
        global_state->ASIC_initalized = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "create_jobs_task created.");

    task_created = xTaskCreate(ASIC_task, "asic", 8192, (void *) global_state, 10, &global_state->asic_task_handle);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ASIC_task");
        // Rollback previous tasks
        if (global_state->create_jobs_task_handle) {
            vTaskDelete(global_state->create_jobs_task_handle);
            global_state->create_jobs_task_handle = NULL;
        }
        if (global_state->stratum_task_handle) {
            vTaskDelete(global_state->stratum_task_handle);
            global_state->stratum_task_handle = NULL;
        }
        if (pm_task_created_in_this_call && global_state->power_management_task_handle) {
            vTaskDelete(global_state->power_management_task_handle);
            global_state->power_management_task_handle = NULL;
        }
        global_state->ASIC_initalized = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ASIC_task created.");

    task_created =
        xTaskCreate(ASIC_result_task, "asic result", 8192, (void *) global_state, 15, &global_state->asic_result_task_handle);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ASIC_result_task");
        // Rollback previous tasks
        if (global_state->asic_task_handle) {
            vTaskDelete(global_state->asic_task_handle);
            global_state->asic_task_handle = NULL;
        }
        if (global_state->ASIC_TASK_MODULE.semaphore) {
            vSemaphoreDelete(global_state->ASIC_TASK_MODULE.semaphore);
            global_state->ASIC_TASK_MODULE.semaphore = NULL;
        }
        if (global_state->ASIC_TASK_MODULE.active_jobs) {
            free(global_state->ASIC_TASK_MODULE.active_jobs);
            global_state->ASIC_TASK_MODULE.active_jobs = NULL;
        }
        if (global_state->valid_jobs) {
            free(global_state->valid_jobs);
            global_state->valid_jobs = NULL;
        }

        if (global_state->create_jobs_task_handle) {
            vTaskDelete(global_state->create_jobs_task_handle);
            global_state->create_jobs_task_handle = NULL;
        }
        if (global_state->stratum_task_handle) {
            vTaskDelete(global_state->stratum_task_handle);
            global_state->stratum_task_handle = NULL;
        }
        if (pm_task_created_in_this_call && global_state->power_management_task_handle) {
            vTaskDelete(global_state->power_management_task_handle);
            global_state->power_management_task_handle = NULL;
        }
        global_state->ASIC_initalized = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ASIC_result_task created.");

    ESP_LOGI(TAG, "Mining operations started successfully.");
    return ESP_OK;
}

esp_err_t stop_mining(GlobalState * global_state)
{
    ESP_LOGI(TAG, "Stopping mining operations...");

    if (!global_state->mining_enabled) {
        ESP_LOGW(TAG, "Mining is already disabled.");
        return ESP_OK;
    }

    // 1. Signal tasks that mining is stopping
    global_state->mining_enabled = false;
    global_state->abandon_work = 1;

    // 2. Close network connections and clean primary job queues
    ESP_LOGI(TAG, "Closing stratum connection...");
    stratum_close_connection(global_state);

    // Add a delay to allow tasks to process stop flags before disabling power
    ESP_LOGI(TAG, "Allowing tasks to process flags (150ms delay)...");
    vTaskDelay(pdMS_TO_TICKS(150));

    // 3. Stop ASIC hardware
    if (global_state->ASIC_initalized) {
        float safe_low_frequency = 56.25;
        ESP_LOGI(TAG, "Setting ASIC frequency to a safe low value (%.2f MHz) before power disable...", safe_low_frequency);
        bool freq_set_success = ASIC_set_frequency(global_state, safe_low_frequency);
        if (freq_set_success) {
            ESP_LOGI(TAG, "ASIC frequency set to %.2f MHz. Delaying for frequency to settle (50ms)...", safe_low_frequency);
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            ESP_LOGW(TAG, "Failed to set ASIC to safe low frequency. Proceeding with power disable.");
        }
    } else {
        ESP_LOGI(TAG, "ASIC not initialized, skipping frequency ramp down.");
    }

    ESP_LOGI(TAG, "Disabling power to ASIC...");
    Power_disable(global_state);

    // Add a small delay after power disable for hardware to settle
    ESP_LOGI(TAG, "Delay after Power_disable (50ms)...");
    vTaskDelay(pdMS_TO_TICKS(50));

    // 4. Cleaning up tasks
    ESP_LOGI(TAG, "Cleaning up mining tasks...");
    if (global_state->ASIC_TASK_MODULE.semaphore != NULL) {
        vSemaphoreDelete(global_state->ASIC_TASK_MODULE.semaphore);
        global_state->ASIC_TASK_MODULE.semaphore = NULL;
        ESP_LOGI(TAG, "ASIC task semaphore deleted.");
    }
    if (global_state->ASIC_TASK_MODULE.active_jobs != NULL) {
        free(global_state->ASIC_TASK_MODULE.active_jobs);
        global_state->ASIC_TASK_MODULE.active_jobs = NULL;
        ESP_LOGI(TAG, "ASIC active_jobs memory freed.");
    }
    if (global_state->valid_jobs != NULL) {
        free(global_state->valid_jobs);
        global_state->valid_jobs = NULL;
        ESP_LOGI(TAG, "ASIC valid_jobs memory freed.");
    }

    // 5. Update final state and reset statistics
    global_state->ASIC_initalized = false;

    ESP_LOGI(TAG, "Resetting mining statistics for API reporting.");
    global_state->SYSTEM_MODULE.current_hashrate = 0.0;

    ESP_LOGI(TAG, "Mining operations stopped successfully.");
    return ESP_OK;
}
