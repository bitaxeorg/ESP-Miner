#include "asic.h"
#include "bzm/driver.h"
#include "global_state.h"

static esp_err_t get_measurement(GlobalState *state, uint8_t asic_nr, uint8_t domain_nr,
                                 bool total, asic_domain_measurement_t *measurement)
{
    if (state == NULL || measurement == NULL) return ESP_ERR_INVALID_ARG;
    *measurement = (asic_domain_measurement_t){0};
    if (asic_nr >= state->DEVICE_CONFIG.family.asic_count ||
        (!total && domain_nr >= state->DEVICE_CONFIG.family.asic.hash_domains)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state->DEVICE_CONFIG.family.asic.id == BZM) {
        asic_domain_measurement_t sample = {0};
        if (!BZM_get_hashrate_measurement(state, asic_nr, &sample.time_us, &sample.hashrate)) {
            return ESP_ERR_INVALID_STATE;
        }
        *measurement = sample;
        return ESP_OK;
    }
    HashrateMonitorModule *monitor = &state->HASHRATE_MONITOR_MODULE;
    if (!monitor->is_initialized) return ESP_ERR_INVALID_STATE;
    pthread_mutex_lock(&monitor->lock);
    measurement_t sample = total ? monitor->total_measurement[asic_nr]
                                : monitor->domain_measurements[asic_nr][domain_nr];
    pthread_mutex_unlock(&monitor->lock);
    measurement->time_us = sample.time_us;
    measurement->hashrate = sample.hashrate;
    return ESP_OK;
}

esp_err_t ASIC_get_asic_measurement(GlobalState *state, uint8_t asic_nr,
                                    asic_domain_measurement_t *measurement)
{
    return get_measurement(state, asic_nr, 0, true, measurement);
}

esp_err_t ASIC_get_domain_measurement(GlobalState *state, uint8_t asic_nr, uint8_t domain_nr,
                                      asic_domain_measurement_t *measurement)
{
    return get_measurement(state, asic_nr, domain_nr, false, measurement);
}
