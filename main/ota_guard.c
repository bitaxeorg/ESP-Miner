#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "global_state.h"
#include "nvs_config.h"
#include "ota_guard.h"
#include "pool.h"
#include "system.h"

static const char * TAG = "ota_guard";

#define CRASH_LIMIT 2       // crash restarts of a new image before going back
#define HEALTHY_S 120       // this long at a normal hashrate keeps the update
#define VERDICT_S 600       // online but not mining this long after boot: go back
#define MIN_RATE 0.5f       // fraction of the expected hashrate that counts as mining
#define REQUEST_MAGIC 0x4f544152 // "OTAR"

// The watcher task has its stack in PSRAM, so it can't write flash itself
// (NVS and the boot partition). It leaves the verdict here and restarts; the
// next boot switches partitions from app_main.
typedef struct {
    uint32_t magic;
    char why[48];
} rollback_request_t;

static __NOINIT_ATTR rollback_request_t request;

static bool pending = false;
static char previous_label[17] = "";

static void sha_to_hex(const uint8_t sha[32], char hex[65])
{
    for (int i = 0; i < 32; i++) {
        snprintf(&hex[i * 2], 3, "%02x", sha[i]);
    }
}

// Goes through the NVS task, so it is safe from any stack
static void clear(void)
{
    pending = false;
    nvs_config_set_string(NVS_CONFIG_OTA_PREVIOUS, "");
    nvs_config_set_string(NVS_CONFIG_OTA_SHA, "");
    nvs_config_set_u16(NVS_CONFIG_OTA_TRIES, 0);
}

// Only from app_main (internal stack): writes the boot partition
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
    // Give the NVS task time to write the cleared state
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
}

void ota_guard_boot(void)
{
    bool requested = request.magic == REQUEST_MAGIC;
    char why[sizeof(request.why)];
    if (requested) {
        strlcpy(why, request.why, sizeof(why));
    }
    request.magic = 0;

    char * previous = nvs_config_get_string(NVS_CONFIG_OTA_PREVIOUS);
    char * sha = nvs_config_get_string(NVS_CONFIG_OTA_SHA);
    bool armed = previous && sha && previous[0] != '\0' && sha[0] != '\0';
    char running_sha[65];
    sha_to_hex(esp_app_get_description()->app_elf_sha256, running_sha);
    bool same_image = armed && strcmp(sha, running_sha) == 0;
    if (armed) {
        strlcpy(previous_label, previous, sizeof(previous_label));
    }
    free(previous);
    free(sha);
    if (!armed) {
        return;
    }

    // A USB flash or another update since: this isn't the image that was armed
    if (!same_image) {
        clear();
        return;
    }

    if (requested) {
        roll_back(why);
        return;
    }

    pending = true;
    esp_reset_reason_t reason = esp_reset_reason();
    bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
    if (!crashed) {
        ESP_LOGW(TAG, "New firmware on probation, kept once it mines normally for %d s", HEALTHY_S);
        return;
    }

    uint16_t tries = nvs_config_get_u16(NVS_CONFIG_OTA_TRIES) + 1;
    ESP_LOGW(TAG, "New firmware crashed at start-up (%u of %d)", tries, CRASH_LIMIT);
    if (tries >= CRASH_LIMIT) {
        roll_back("Crashed at start-up twice");
        return;
    }
    nvs_config_set_u16(NVS_CONFIG_OTA_TRIES, tries);
}

void ota_guard_arm(const esp_partition_t * updated)
{
    const esp_partition_t * running = esp_ota_get_running_partition();
    esp_app_desc_t desc;
    if (running == NULL || esp_ota_get_partition_description(updated, &desc) != ESP_OK) {
        return;
    }
    char sha[65];
    sha_to_hex(desc.app_elf_sha256, sha);
    nvs_config_set_string(NVS_CONFIG_OTA_PREVIOUS, running->label);
    nvs_config_set_string(NVS_CONFIG_OTA_SHA, sha);
    nvs_config_set_u16(NVS_CONFIG_OTA_TRIES, 0);
    ESP_LOGI(TAG, "Update %s armed, '%s' stays as the fallback", desc.version, running->label);
}

static void request_rollback(const char * why)
{
    ESP_LOGE(TAG, "%s, restarting to go back to '%s'", why, previous_label);
    strlcpy(request.why, why, sizeof(request.why));
    request.magic = REQUEST_MAGIC;
    esp_restart();
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
        const PoolSession * session = POOL_get_active_session(GLOBAL_STATE);
        bool has_work = session != NULL && session->work_received > 0;
        if (hashing || esp_timer_get_time() / 1000000 < VERDICT_S || sys->mining_paused || sys->overheat_mode
            || !sys->is_connected || sys->pools_unavailable || !has_work) {
            continue;
        }
        request_rollback(sys->hardware_fault ? "Hardware fault after the update" : "Not mining 10 minutes after the update");
    }
    vTaskDeleteWithCaps(NULL);
}

void ota_guard_start(GlobalState * GLOBAL_STATE)
{
    if (!pending || GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
        return;
    }
    if (xTaskCreateWithCaps(ota_guard_task, "ota_guard", 4096, GLOBAL_STATE, 2, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Error creating OTA guard task");
    }
}
