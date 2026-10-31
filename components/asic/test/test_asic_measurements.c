#include <stdlib.h>
#include "unity.h"
#define TEST_STUB_GLOBAL_STATE_H
#include "../../../main/global_state.h"
#include "asic.h"

#define ASIC_get_asic_measurement measurement_test_total
#define ASIC_get_domain_measurement measurement_test_domain
#define BZM_get_hashrate_measurement measurement_test_bzm
#include "../asic_measurement.c"

static bool software_available;
static uint8_t requested_asic;
bool measurement_test_bzm(GlobalState *state, uint8_t asic_nr, uint64_t *time_us, float *hashrate)
{
    (void)state;
    requested_asic = asic_nr;
    *time_us = 3000000;
    *hashrate = 350;
    return software_available;
}

TEST_CASE("ASIC measurement interface returns Bitmain totals and domains without changing accounting", "[asic][hashrate]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    state->DEVICE_CONFIG.family = FAMILY_GAMMA;
    measurement_t total = {.value = 512, .time_us = 2000000, .hashrate = 1200};
    measurement_t domains[4] = {{.time_us = 2000000, .hashrate = 300}};
    measurement_t *domain_pointers[] = {domains};
    state->HASHRATE_MONITOR_MODULE.total_measurement = &total;
    state->HASHRATE_MONITOR_MODULE.domain_measurements = domain_pointers;
    pthread_mutex_init(&state->HASHRATE_MONITOR_MODULE.lock, NULL);
    atomic_init(&state->HASHRATE_MONITOR_MODULE.is_initialized, true);
    asic_domain_measurement_t sample;
    TEST_ASSERT_EQUAL(ESP_OK, ASIC_get_asic_measurement(state, 0, &sample));
    TEST_ASSERT_EQUAL_FLOAT(1200, sample.hashrate);
    TEST_ASSERT_TRUE((uint64_t)(2000000) == (uint64_t)(sample.time_us));
    TEST_ASSERT_EQUAL(ESP_OK, ASIC_get_domain_measurement(state, 0, 0, &sample));
    TEST_ASSERT_EQUAL_FLOAT(300, sample.hashrate);
    TEST_ASSERT_EQUAL_UINT32(512, total.value);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_asic_measurement(state, 1, &sample));
    TEST_ASSERT_EQUAL_FLOAT(0, sample.hashrate);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_domain_measurement(state, 0, 4, &sample));
    atomic_store(&state->HASHRATE_MONITOR_MODULE.is_initialized, false);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ASIC_get_asic_measurement(state, 0, &sample));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_asic_measurement(NULL, 0, &sample));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_asic_measurement(state, 0, NULL));
    state->DEVICE_CONFIG.family = FAMILY_BONANZA;
    software_available = true;
    TEST_ASSERT_EQUAL(ESP_OK, ASIC_get_asic_measurement(state, 3, &sample));
    TEST_ASSERT_EQUAL_UINT8(3, requested_asic);
    TEST_ASSERT_EQUAL_FLOAT(350, sample.hashrate);
    TEST_ASSERT_EQUAL(ESP_OK, ASIC_get_domain_measurement(state, 3, 0, &sample));
    TEST_ASSERT_EQUAL_FLOAT(350, sample.hashrate);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_domain_measurement(state, 3, 1, &sample));
    software_available = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ASIC_get_asic_measurement(state, 0, &sample));
    TEST_ASSERT_EQUAL_FLOAT(0, sample.hashrate);
    pthread_mutex_destroy(&state->HASHRATE_MONITOR_MODULE.lock);
    free(state);
}
