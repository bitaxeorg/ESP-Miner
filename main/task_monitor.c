#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "string.h"
#include "global_state.h"

#define MAX_TASKS 40
#define INTERVAL_MS 60000

static const char* TAG = "task_monitor";

void task_monitor_task(void *pvParameters) {
    TaskStatus_t *task_array1 = malloc(MAX_TASKS * sizeof(TaskStatus_t));
    TaskStatus_t *task_array2 = malloc(MAX_TASKS * sizeof(TaskStatus_t));
    if (task_array1 == NULL || task_array2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for task arrays");
        free(task_array1);
        free(task_array2);
        vTaskDelete(NULL);
    }

    while (1) {
        uint32_t total_runtime1 = 0;
        uint32_t num_tasks1 = uxTaskGetSystemState(task_array1, MAX_TASKS, &total_runtime1);
        if (num_tasks1 == 0) {
            ESP_LOGE(TAG, "Failed to get initial task state");
            vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));  // Wait and retry
            continue;
        }

        // Wait for the interval
        vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));

        uint32_t total_runtime2 = 0;
        uint32_t num_tasks2 = uxTaskGetSystemState(task_array2, MAX_TASKS, &total_runtime2);
        if (num_tasks2 == 0) {
            ESP_LOGE(TAG, "Failed to get second task state");
            continue;
        }

        // Compute total delta (elapsed wall time in timer units)
        uint32_t total_delta = total_runtime2 - total_runtime1;
        if (total_delta == 0) {
            ESP_LOGI(TAG, "No runtime change in interval");
            continue;
        }

        // Warn if task count changed (possible create/destroy)
        if (num_tasks2 != num_tasks1) {
            ESP_LOGW(TAG, "Task count changed (%u -> %u); stats may be incomplete for destroyed tasks", num_tasks1, num_tasks2);
        }

        // Print header
        printf("Task Stats (over %d ms):\n", INTERVAL_MS);
        printf("Task Name\t\tDelta Runtime (us)\tDelta %%\t\tLifetime Runtime (us)\tLifetime %%\n");

        // Process each task in array2 (current tasks)
        for (uint32_t j = 0; j < num_tasks2; j++) {
            uint32_t task_delta = task_array2[j].ulRunTimeCounter;

            // Look for match in array1
            for (uint32_t i = 0; i < num_tasks1; i++) {
                if (task_array1[i].xHandle == task_array2[j].xHandle) {
                    task_delta -= task_array1[i].ulRunTimeCounter;
                    break;
                }
            }

            // If not found, it's a new task: delta remains run2 (all during interval)
            double delta_percentage = (task_delta * 100.0) / total_delta;
            uint32_t lifetime_runtime = task_array2[j].ulRunTimeCounter;
            double lifetime_percentage = (lifetime_runtime * 100.0) / total_runtime2;
            printf("%-20s\t%u\t\t\t%.2f%%\t\t%u\t\t\t%.2f%%\n", task_array2[j].pcTaskName, (unsigned int)task_delta, delta_percentage, (unsigned int)lifetime_runtime, lifetime_percentage);
        }
        printf("\n");

        // Swap arrays for next iteration (reuse memory)
        TaskStatus_t *temp = task_array1;
        task_array1 = task_array2;
        task_array2 = temp;
    }

    // Cleanup (though loop is infinite)
    free(task_array1);
    free(task_array2);
    vTaskDelete(NULL);
}

void cpu_monitor_task(void *pvParameters) {
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    float avg_usage = -1.0f;
    const float alpha = 0.5f;

    while (1) {
        float idle_percent = (ulTaskGetIdleRunTimePercentForCore(0) + ulTaskGetIdleRunTimePercentForCore(1)) / 2.0f;
        float current_usage = 100.0f - idle_percent;
        
        if (avg_usage < 0) {
            avg_usage = current_usage; // First sample
        } else {
            avg_usage = (alpha * current_usage) + ((1.0f - alpha) * avg_usage);
        }

        GLOBAL_STATE->SYSTEM_MODULE.cpu_usage = avg_usage;
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
