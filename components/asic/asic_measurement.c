#include "asic.h"
#include "bzm/driver.h"
#include "global_state.h"

asic_hashrate_sample_t ASIC_sample_hashrate(GlobalState *state)
{
    asic_hashrate_sample_t sample = {0};
    if (state->DEVICE_CONFIG.family.asic.id == BZM) {
        sample.interval_valid = BZM_sample_hashrate(state, &sample.interval_hashrate, &sample.current_hashrate);
        return sample;
    }

    HashrateMonitorModule *monitor = &state->HASHRATE_MONITOR_MODULE;
    pthread_mutex_lock(&monitor->lock);
    for (unsigned asic = 0; asic < state->DEVICE_CONFIG.family.asic_count; ++asic) {
        sample.interval_hashrate += monitor->total_measurement[asic].hashrate;
        sample.error_hashrate += monitor->error_measurement[asic].hashrate;
    }
    pthread_mutex_unlock(&monitor->lock);

    sample.current_hashrate = sample.interval_hashrate;
    sample.interval_valid = sample.interval_hashrate > 0.0f;
    return sample;
}

float ASIC_get_temperature(GlobalState *state)
{
    if (state == NULL || !state->DEVICE_CONFIG.family.asic.native_temperature) return -1;
    switch (state->DEVICE_CONFIG.family.asic.id) {
        case BZM:
            return BZM_read_temperature(state);
        default:
            return -1;
    }
}

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
