#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "global_state.h"
#include "ota_guard.h"
#include "system.h"

static const char * TAG = "ota_guard";

#define NVS_NAMESPACE "ota_guard"
#define CRASH_LIMIT 2       // crash restarts of a new image before going back
#define HEALTHY_S 120       // this long at a normal hashrate keeps the update
#define VERDICT_S 600       // online but not mining this long after boot: go back
#define MIN_RATE 0.5f       // fraction of the expected hashrate that counts as mining

static bool pending = false;
static char previous_label[17] = "";

static void clear(void)
{
    pending = false;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
}

static void roll_back(const char * why)
{
    const esp_partition_t * previous = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, previous_label);
    esp_app_desc_t desc;
    clear();
    if (previous == NULL || previous == esp_ota_get_running_partition() || esp_ota_get_partition_description(previous, &desc) != ESP_OK) {
        ESP_LOGE(TAG, "%s, but the previous firmware in '%s' is gone, keeping this one", why, previous_label);
        return;
    }
    ESP_LOGE(TAG, "%s, going back to %s in '%s'", why, desc.version, previous->label);
    if (esp_ota_set_boot_partition(previous) != ESP_OK) {
        ESP_LOGE(TAG, "Could not set '%s' as the boot partition", previous->label);
        return;
    }
    // Let the previous firmware serve its own embedded web UI, as after an update
    SYSTEM_reset_custom_www();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
}

void ota_guard_boot(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t label_len = sizeof(previous_label);
    uint8_t sha[32];
    size_t sha_len = sizeof(sha);
    uint8_t tries = 0;
    bool armed = nvs_get_str(handle, "prev", previous_label, &label_len) == ESP_OK
              && nvs_get_blob(handle, "sha", sha, &sha_len) == ESP_OK;
    nvs_get_u8(handle, "tries", &tries);
    nvs_close(handle);
    if (!armed) {
        return;
    }

    // A USB flash or another update since: this isn't the image that was armed
    if (sha_len != sizeof(sha) || memcmp(sha, esp_app_get_description()->app_elf_sha256, sizeof(sha)) != 0) {
        clear();
        return;
    }

    pending = true;
    esp_reset_reason_t reason = esp_reset_reason();
    bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
    if (!crashed) {
        ESP_LOGW(TAG, "New firmware on probation, kept once it mines normally for %d s", HEALTHY_S);
        return;
    }

    tries++;
    ESP_LOGW(TAG, "New firmware crashed at start-up (%u of %d)", tries, CRASH_LIMIT);
    if (tries >= CRASH_LIMIT) {
        roll_back("Crashed at start-up twice");
        return;
    }
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "tries", tries);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

void ota_guard_arm(const esp_partition_t * updated)
{
    const esp_partition_t * running = esp_ota_get_running_partition();
    esp_app_desc_t desc;
    if (running == NULL || esp_ota_get_partition_description(updated, &desc) != ESP_OK) {
        return;
    }
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, "prev", running->label);
    nvs_set_blob(handle, "sha", desc.app_elf_sha256, sizeof(desc.app_elf_sha256));
    nvs_set_u8(handle, "tries", 0);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Update %s armed, '%s' stays as the fallback", desc.version, running->label);
}

static void ota_guard_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    SystemModule * sys = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule * power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    uint32_t healthy_s = 0;

    while (pending) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        bool hashing = GLOBAL_STATE->ASIC_initalized && power->expected_hashrate > 0
                    && sys->hashrate_1m >= MIN_RATE * power->expected_hashrate;
        healthy_s = hashing ? healthy_s + 1 : 0;
        if (healthy_s >= HEALTHY_S) {
            ESP_LOGI(TAG, "New firmware mines normally, keeping it");
            clear();
            break;
        }

        // No verdict while the user paused mining, it's cooling down or the
        // network/pools are down or the pool sends no work: none of that is the
        // firmware's fault
        if (hashing || esp_timer_get_time() / 1000000 < VERDICT_S || sys->mining_paused || sys->overheat_mode
            || !sys->is_connected || sys->pools_unavailable || !GLOBAL_STATE->has_work) {
            continue;
        }
        roll_back(sys->hardware_fault ? "Hardware fault after the update" : "Not mining 10 minutes after the update");
        break;
    }
    vTaskDelete(NULL);
}

void ota_guard_start(GlobalState * GLOBAL_STATE)
{
    if (!pending || GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
        return;
    }
    // Internal stack: the task writes NVS and the boot partition
    if (xTaskCreate(ota_guard_task, "ota_guard", 4096, GLOBAL_STATE, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creating OTA guard task");
    }
}
