#include <string.h>

#include <esp_log.h>

#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"

#include "asic.h"

static const double NONCE_SPACE = 4294967296.0; //  2^32

static const char *TAG = "asic";

// .init_fn = BM1366_init,
uint8_t ASIC_init(GlobalState * GLOBAL_STATE) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            return BM1397_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, BITAXE_MAX_ASIC_COUNT);
        case DEVICE_ULTRA:
            return BM1366_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, BITAXE_ULTRA_ASIC_COUNT);
        case DEVICE_SUPRA:
            return BM1368_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, BITAXE_SUPRA_ASIC_COUNT);
        case DEVICE_GAMMA:
            return BM1370_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, BITAXE_GAMMA_ASIC_COUNT);
        case DEVICE_GAMMATURBO:
            return BM1370_init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, BITAXE_GAMMATURBO_ASIC_COUNT);
        default:
    }
    return ESP_OK;
}

uint8_t ASIC_get_asic_count(GlobalState * GLOBAL_STATE) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            return BITAXE_MAX_ASIC_COUNT;
        case DEVICE_ULTRA:
            return BITAXE_ULTRA_ASIC_COUNT;
        case DEVICE_SUPRA:
            return BITAXE_SUPRA_ASIC_COUNT;
        case DEVICE_GAMMA:
            return BITAXE_GAMMA_ASIC_COUNT;
        case DEVICE_GAMMATURBO:
            return BITAXE_GAMMATURBO_ASIC_COUNT;
        default:
    }
    return 0;
}

uint16_t ASIC_get_small_core_count(GlobalState * GLOBAL_STATE) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            return BM1397_SMALL_CORE_COUNT;
        case DEVICE_ULTRA:
            return BM1366_SMALL_CORE_COUNT;
        case DEVICE_SUPRA:
            return BM1368_SMALL_CORE_COUNT;
        case DEVICE_GAMMA:
            return BM1370_SMALL_CORE_COUNT;
        case DEVICE_GAMMATURBO:
            return BM1370_SMALL_CORE_COUNT;
        default:
    }
    return 0;
}

// .receive_result_fn = BM1366_proccess_work,
task_result * ASIC_proccess_work(GlobalState * GLOBAL_STATE) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            return BM1397_proccess_work(GLOBAL_STATE);
        case DEVICE_ULTRA:
            return BM1366_proccess_work(GLOBAL_STATE);
        case DEVICE_SUPRA:
            return BM1368_proccess_work(GLOBAL_STATE);
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            return BM1370_proccess_work(GLOBAL_STATE);
        default:
    }
    return NULL;
}

// .set_max_baud_fn = BM1366_set_max_baud,
int ASIC_set_max_baud(GlobalState * GLOBAL_STATE) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            return BM1397_set_max_baud();
        case DEVICE_ULTRA:
            return BM1366_set_max_baud();
        case DEVICE_SUPRA:
            return BM1368_set_max_baud();
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            return BM1370_set_max_baud();
        default:
    return 0;
    }
}

// .set_difficulty_mask_fn = BM1366_set_job_difficulty_mask,
void ASIC_set_job_difficulty_mask(GlobalState * GLOBAL_STATE, uint8_t mask) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            BM1397_set_job_difficulty_mask(mask);
            break;
        case DEVICE_ULTRA:
            BM1366_set_job_difficulty_mask(mask);
            break;
        case DEVICE_SUPRA:
            BM1368_set_job_difficulty_mask(mask);
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            BM1370_set_job_difficulty_mask(mask);
            break;
        default:
    }
}

// .send_work_fn = BM1366_send_work,
void ASIC_send_work(GlobalState * GLOBAL_STATE, void * next_job) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            BM1397_send_work(GLOBAL_STATE, next_job);
            break;
        case DEVICE_ULTRA:
            BM1366_send_work(GLOBAL_STATE, next_job);
            break;
        case DEVICE_SUPRA:
            BM1368_send_work(GLOBAL_STATE, next_job);
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            BM1370_send_work(GLOBAL_STATE, next_job);
            break;
        default:
    return;
    }
}

// .set_version_mask = BM1366_set_version_mask
void ASIC_set_version_mask(GlobalState * GLOBAL_STATE, uint32_t mask) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            BM1397_set_version_mask(mask);
            break;
        case DEVICE_ULTRA:
            BM1366_set_version_mask(mask);
            break;
        case DEVICE_SUPRA:
            BM1368_set_version_mask(mask);
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            BM1370_set_version_mask(mask);
            break;
        default:
    return;
    }
}

esp_err_t ASIC_set_device_model(GlobalState * GLOBAL_STATE) {

    if (GLOBAL_STATE->device_model_str == NULL) {
        ESP_LOGE(TAG, "No device model string found");
        return ESP_FAIL;
    }

    if (strcmp(GLOBAL_STATE->device_model_str, "max") == 0) {
        GLOBAL_STATE->asic_model = ASIC_BM1397;
        GLOBAL_STATE->valid_model = true;
        GLOBAL_STATE->asic_job_frequency_ms = (NONCE_SPACE / (double) (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * BM1397_SMALL_CORE_COUNT * 1000)) / (double) ASIC_get_asic_count(GLOBAL_STATE); // no version-rolling so same Nonce Space is splitted between Small Cores
        GLOBAL_STATE->ASIC_difficulty = BM1397_ASIC_DIFFICULTY;
        ESP_LOGI(TAG, "DEVICE: bitaxeMax");
        ESP_LOGI(TAG, "ASIC: %dx BM1397 (%" PRIu64 " cores)", BITAXE_MAX_ASIC_COUNT, BM1397_CORE_COUNT);
        GLOBAL_STATE->device_model = DEVICE_MAX;

    } else if (strcmp(GLOBAL_STATE->device_model_str, "ultra") == 0) {
        GLOBAL_STATE->asic_model = ASIC_BM1366;
        GLOBAL_STATE->valid_model = true;
        //GLOBAL_STATE.asic_job_frequency_ms = (NONCE_SPACE / (double) (GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value * BM1366_CORE_COUNT * 1000)) / (double) BITAXE_ULTRA_ASIC_COUNT; // version-rolling so Small Cores have different Nonce Space
        GLOBAL_STATE->asic_job_frequency_ms = 2000; //ms
        GLOBAL_STATE->ASIC_difficulty = BM1366_ASIC_DIFFICULTY;
        ESP_LOGI(TAG, "DEVICE: bitaxeUltra");
        ESP_LOGI(TAG, "ASIC: %dx BM1366 (%" PRIu64 " cores)", BITAXE_ULTRA_ASIC_COUNT, BM1366_CORE_COUNT);
        GLOBAL_STATE->device_model = DEVICE_ULTRA;

    } else if (strcmp(GLOBAL_STATE->device_model_str, "supra") == 0) {
        GLOBAL_STATE->asic_model = ASIC_BM1368;
        GLOBAL_STATE->valid_model = true;
        //GLOBAL_STATE.asic_job_frequency_ms = (NONCE_SPACE / (double) (GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value * BM1368_CORE_COUNT * 1000)) / (double) BITAXE_SUPRA_ASIC_COUNT; // version-rolling so Small Cores have different Nonce Space
        GLOBAL_STATE->asic_job_frequency_ms = 500; //ms
        GLOBAL_STATE->ASIC_difficulty = BM1368_ASIC_DIFFICULTY;
        ESP_LOGI(TAG, "DEVICE: bitaxeSupra");
        ESP_LOGI(TAG, "ASIC: %dx BM1368 (%" PRIu64 " cores)", BITAXE_SUPRA_ASIC_COUNT, BM1368_CORE_COUNT);
        GLOBAL_STATE->device_model = DEVICE_SUPRA;

    } else if (strcmp(GLOBAL_STATE->device_model_str, "gamma") == 0) {
        GLOBAL_STATE->asic_model = ASIC_BM1370;
        GLOBAL_STATE->valid_model = true;
        //GLOBAL_STATE.asic_job_frequency_ms = (NONCE_SPACE / (double) (GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value * BM1370_CORE_COUNT * 1000)) / (double) BITAXE_GAMMA_ASIC_COUNT; // version-rolling so Small Cores have different Nonce Space
        GLOBAL_STATE->asic_job_frequency_ms = 500; //ms
        GLOBAL_STATE->ASIC_difficulty = BM1370_ASIC_DIFFICULTY;
        ESP_LOGI(TAG, "DEVICE: bitaxeGamma");
        ESP_LOGI(TAG, "ASIC: %dx BM1370 (%" PRIu64 " cores)", BITAXE_GAMMA_ASIC_COUNT, BM1370_CORE_COUNT);
        GLOBAL_STATE->device_model = DEVICE_GAMMA;

    } else if (strcmp(GLOBAL_STATE->device_model_str, "gammaturbo") == 0) {
        GLOBAL_STATE->asic_model = ASIC_BM1370;
        GLOBAL_STATE->valid_model = true;
        //GLOBAL_STATE.asic_job_frequency_ms = (NONCE_SPACE / (double) (GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value * BM1370_CORE_COUNT * 1000)) / (double) BITAXE_GAMMATURBO_ASIC_COUNT; // version-rolling so Small Cores have different Nonce Space
        GLOBAL_STATE->asic_job_frequency_ms = 500; //ms
        GLOBAL_STATE->ASIC_difficulty = BM1370_ASIC_DIFFICULTY;
        ESP_LOGI(TAG, "DEVICE: bitaxeGammaTurbo");
        ESP_LOGI(TAG, "ASIC: %dx BM1370 (%" PRIu64 " cores)", BITAXE_GAMMATURBO_ASIC_COUNT, BM1370_CORE_COUNT);
        GLOBAL_STATE->device_model = DEVICE_GAMMATURBO;

    } else {
        ESP_LOGE(TAG, "Invalid DEVICE model");
        // maybe should return here to now execute anything with a faulty device parameter !
        // this stops crashes/reboots and allows dev testing without an asic
        GLOBAL_STATE->device_model = DEVICE_UNKNOWN;
        GLOBAL_STATE->valid_model = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}
