#include "unity.h"
#include "bzm/hashrate.h"
#include "device_config.h"
#include "hashrate_monitor_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_heap_caps.h"

#define TEST_STUB_GLOBAL_STATE_H
#include "../../../main/global_state.h"

static unsigned allocations, fail_allocation, outstanding, deletes;
static unsigned average_scan_checks;
static int monitor_isnan(float value)
{
    ++average_scan_checks;
    return isnan(value);
}
static void *monitor_calloc(size_t count, size_t size, uint32_t caps)
{
    ++allocations;
    TEST_ASSERT_EQUAL_UINT32(MALLOC_CAP_SPIRAM, caps);
    if (allocations == fail_allocation) return NULL;
    void *memory = calloc(count, size);
    if (memory) ++outstanding;
    return memory;
}
static void *monitor_malloc(size_t size, uint32_t caps)
{
    void *memory = monitor_calloc(1, size, caps);
    if (memory) memset(memory, 0xa5, size);
    return memory;
}
static void monitor_free(void *memory)
{
    if (memory) { --outstanding; free(memory); }
}
static void monitor_delete(TaskHandle_t task) { (void)task; ++deletes; }
static void monitor_noinit(SystemModule *system, float interval_hashrate);
static void monitor_delay(TickType_t ticks);
static void monitor_delay_until(TickType_t *wake, TickType_t ticks);
#define heap_caps_calloc monitor_calloc
#define heap_caps_malloc monitor_malloc
#define heap_caps_free monitor_free
#define ASIC_read_registers monitor_read
#define ASIC_get_domain_measurement monitor_domain_measurement
#define ASIC_get_asic_measurement monitor_total_measurement
#define SYSTEM_noinit_update monitor_noinit
#define vTaskDelay monitor_delay
#define vTaskDelete monitor_delete
#undef vTaskDelayUntil
#define vTaskDelayUntil monitor_delay_until
#define hashrate_monitor_task monitor_task
#define hashrate_monitor_reset_measurements monitor_reset
#define hashrate_monitor_register_read monitor_register_read
#define hashrate_monitor_get_asic_rate monitor_asic_rate
#define hashrate_monitor_get_domain_rate monitor_domain_rate
#define update_hashrate monitor_update_hashrate
#define update_hash_counter monitor_update_counter
#undef isnanf
#define isnanf monitor_isnan
#include "../../../main/tasks/hashrate_monitor_task.c"
#define system_api_get_full_doc monitor_full_doc
#include "../../../main/http_server/system_api_json.c"

static GlobalState *state;
static jmp_buf task_done;
static unsigned sample, test_asics, test_domains;
static unsigned pause_sample, end_sample, headline_count;
static bool from_shares, direct_register;
static uint32_t counters[4][9];
static float *history, headline_history[60];
static double sums[4][9], headline_sum;
static unsigned domain_measurement_calls;
static bzm_hashrate_t (*software_rates)[9];

static esp_err_t get_measurement(GlobalState *s, uint8_t asic, unsigned series,
                                 asic_domain_measurement_t *measurement)
{
    if (!s || !measurement) return ESP_ERR_INVALID_ARG;
    *measurement = (asic_domain_measurement_t){0};
    if (!s->HASHRATE_MONITOR_MODULE.is_initialized || asic >= test_asics || series > test_domains)
        return ESP_ERR_INVALID_STATE;
    if (from_shares) {
        measurement->hashrate = s->ASIC_initalized && !s->SYSTEM_MODULE.mining_paused
            ? software_rates[asic][series].rate : 0;
        measurement->time_us = software_rates[asic][series].time_us;
    } else {
        measurement_t *raw = series == 0 ? &s->HASHRATE_MONITOR_MODULE.total_measurement[asic]
            : &s->HASHRATE_MONITOR_MODULE.domain_measurements[asic][series - 1];
        measurement->hashrate = raw->hashrate;
        measurement->time_us = raw->time_us;
    }
    return ESP_OK;
}

esp_err_t monitor_total_measurement(GlobalState *s, uint8_t asic, asic_domain_measurement_t *measurement)
{ return get_measurement(s, asic, 0, measurement); }

esp_err_t monitor_domain_measurement(GlobalState *s, uint8_t asic, uint8_t domain,
                                   asic_domain_measurement_t *measurement)
{
    ++domain_measurement_calls;
    return get_measurement(s, asic, domain + 1, measurement);
}

static void sample_software_rates(GlobalState *s)
{
    if (!from_shares) return;
    for (unsigned a = 0; a < test_asics; ++a) {
        for (unsigned d = 0; d <= test_domains; ++d) {
            bzm_hashrate_sample(&software_rates[a][d], counters[a][d],
                (uint64_t)(sample + 1) * 1000000,
                s->ASIC_initalized && !s->SYSTEM_MODULE.mining_paused);
        }
    }
}

static uint32_t credits(unsigned asic, unsigned domain)
{
    if (!from_shares && sample % 3 == 0) return 0;
    uint32_t total = asic == 0 ? (sample % 2 ? 512 : 0) : (sample % 2 ? 0 : 1024);
    if (asic == 3) total = sample % 2 ? 0 : 16384;
    if (domain == 0 || test_domains == 1) return total;
    return domain == 1 ? total / 4 : total * 3 / 4;
}
static uint32_t hashrate_register(unsigned asic)
{
    return sample % 3 == 0 ? 0 : (sample % 2 ? 1000000 : 2000000) * (asic + 1);
}
void monitor_read(GlobalState *s)
{
    uint64_t time_us = (uint64_t)(sample + 1) * 1000000;
    for (unsigned a = 0; a < test_asics; ++a) {
        if (direct_register) {
            monitor_register_read(s, REGISTER_HASHRATE, a, hashrate_register(a), time_us);
            continue;
        }
        for (unsigned d = 0; d <= test_domains; ++d) {
            counters[a][d] += credits(a, d);
            monitor_register_read(s, d == 0 ? REGISTER_TOTAL_COUNT : REGISTER_DOMAIN_0_COUNT + d - 1,
                                  a, counters[a][d], time_us);
        }
    }
}
static void monitor_delay(TickType_t ticks) { (void)ticks; sample_software_rates(state); }

static void assert_json_rates(void)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    domain_measurement_calls = 0;
    unsigned scans = average_scan_checks;
    system_api_add_hashrate_monitor(doc, root, state);
    TEST_ASSERT_EQUAL_UINT32(test_asics * test_domains, domain_measurement_calls);
    TEST_ASSERT_EQUAL_UINT32(scans, average_scan_checks);
    yyjson_mut_val *asics = yyjson_mut_obj_get(yyjson_mut_obj_get(root, "hashrateMonitor"), "asics");
    TEST_ASSERT_EQUAL_UINT32(test_asics, yyjson_mut_arr_size(asics));
    for (unsigned a = 0; a < test_asics; ++a) {
        yyjson_mut_val *asic = yyjson_mut_arr_get(asics, a);
        TEST_ASSERT_FLOAT_WITHIN(0.002f, monitor_asic_rate(state, a), yyjson_mut_get_real(yyjson_mut_obj_get(asic, "total")));
        yyjson_mut_val *domains = yyjson_mut_obj_get(asic, "domains");
        TEST_ASSERT_EQUAL_UINT32(test_domains, yyjson_mut_arr_size(domains));
        for (unsigned d = 0; d < test_domains; ++d) {
            TEST_ASSERT_FLOAT_WITHIN(0.002f, monitor_domain_rate(state, a, d), yyjson_mut_get_real(yyjson_mut_arr_get(domains, d)));
        }
    }
    yyjson_mut_doc_free(doc);
}
static void monitor_noinit(SystemModule *system, float interval_hashrate)
{
    const float unit_ghs = 4294967296.0 / 1e9;
    unsigned count = sample < 60 ? sample + 1 : 60;
    float total_raw = 0, total_mean = 0;
    bool visible = state->ASIC_initalized && !system->mining_paused;
    for (unsigned a = 0; a < test_asics; ++a) {
        for (unsigned d = 0; d <= test_domains; ++d) {
            float raw = sample > 0 && sample <= pause_sample
                ? (direct_register ? hashrate_register(a) * (1048576.0f / 1e9f) : credits(a, d) * unit_ghs) : 0;
            if (a == 3 && sample == 2) TEST_ASSERT_TRUE(raw > UINT16_MAX);
            float mean = 0;
            if (from_shares) {
                float *series = history + (a * (test_domains + 1) + d) * 60;
                sums[a][d] -= series[sample % 60];
                series[sample % 60] = visible ? raw : 0;
                sums[a][d] += visible ? raw : 0;
                mean = sums[a][d] / count;
            }
            measurement_t *measurement = d == 0 ? &state->HASHRATE_MONITOR_MODULE.total_measurement[a]
                : &state->HASHRATE_MONITOR_MODULE.domain_measurements[a][d - 1];
            TEST_ASSERT_FLOAT_WITHIN(0.002f, raw, measurement->hashrate);
            float tolerance = fmaxf(0.002f, mean * 1e-6f);
            unsigned scans = average_scan_checks;
            float reported = d == 0 ? monitor_asic_rate(state, a) : monitor_domain_rate(state, a, d - 1);
            TEST_ASSERT_EQUAL_UINT32(scans, average_scan_checks);
            float expected = from_shares ? (visible ? mean : 0) : raw;
            TEST_ASSERT_FLOAT_WITHIN(fmaxf(tolerance, raw * 1e-6f), expected, reported);
            if (d == 0) { total_raw += raw; total_mean += mean; }
        }
    }
    TEST_ASSERT_FLOAT_WITHIN(0.002f, total_raw, interval_hashrate);
    {
        unsigned slot = headline_count % 60;
        headline_sum -= headline_history[slot];
        headline_history[slot] = total_raw;
        headline_sum += total_raw;
        ++headline_count;
    }
    float headline_mean = headline_count ? headline_sum / (headline_count < 60 ? headline_count : 60) : 0;
    float tolerance = fmaxf(0.002f, headline_mean * 1e-6f);

    TEST_ASSERT_FLOAT_WITHIN(tolerance, headline_mean, system->hashrate_1m);
    float expected_current = visible ? (from_shares ? total_mean : total_raw) : 0;
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected_current, system->current_hashrate);
    TEST_ASSERT_EQUAL_UINT32(headline_count, poll_count);
    if (from_shares && test_asics == 1 && (sample == 59 || sample == 599 || sample == 3599)) {
        float mean = 256 * unit_ghs;
        TEST_ASSERT_FLOAT_WITHIN(0.002f, mean, system->hashrate_1m);
        TEST_ASSERT_FLOAT_WITHIN(0.002f, mean, system->hashrate_10m);
        TEST_ASSERT_FLOAT_WITHIN(0.002f, mean, system->hashrate_1h);
    }
    if (from_shares && test_asics == 1 && sample == 7199) {
        TEST_ASSERT_EQUAL_FLOAT(0, system->hashrate_1m);
        TEST_ASSERT_EQUAL_FLOAT(0, system->hashrate_10m);
        TEST_ASSERT_EQUAL_FLOAT(0, system->hashrate_1h);
    }
    if (!from_shares && sample == end_sample - 1) {
        TEST_ASSERT_EQUAL_FLOAT(0, system->hashrate_1m);
        TEST_ASSERT_EQUAL_FLOAT(0, system->current_hashrate);
    }
    if (sample == 1 || sample == 2 || sample == 59 || sample == pause_sample ||
        sample == pause_sample + 1 || sample == pause_sample + 2 || sample == end_sample - 1) {
        assert_json_rates();
    }
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_asic_rate(NULL, 0));
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_asic_rate(state, test_asics));
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_domain_rate(state, 0, test_domains));
}
static void monitor_delay_until(TickType_t *wake, TickType_t ticks)
{
    (void)wake;
    TEST_ASSERT_EQUAL_UINT32(pdMS_TO_TICKS(1000), ticks);
    ++sample;
    if (sample == pause_sample) state->SYSTEM_MODULE.mining_paused = true;
    if (sample == pause_sample + 1) state->ASIC_initalized = false;
    if (sample == pause_sample + 2) state->SYSTEM_MODULE.mining_paused = false;
    if (!state->ASIC_initalized) sample_software_rates(state);
    if (sample == end_sample) longjmp(task_done, 1);
}
static void run_monitor(AsicConfig asic, unsigned asics, unsigned domains)
{
    state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    state->ASIC_initalized = true;
    state->DEVICE_CONFIG.family.asic = asic;
    from_shares = asic.id == BZM;
    direct_register = !from_shares && asic.id == BM1397;
    pause_sample = from_shares ? 3600 : 119;
    end_sample = from_shares ? 7200 : 180;
    state->DEVICE_CONFIG.family.asic_count = test_asics = asics;
    state->DEVICE_CONFIG.family.asic.hash_domains = test_domains = domains;
    sample = poll_count = allocations = fail_allocation = deletes = headline_count = 0;
    software_rates = from_shares ? calloc(asics, sizeof(*software_rates)) : NULL;
    if (from_shares) TEST_ASSERT_NOT_NULL(software_rates);
    headline_sum = 0;
    memset(headline_history, 0, sizeof(headline_history));
    TEST_ASSERT_EQUAL_UINT32(0, outstanding);
    memset(counters, 0, sizeof(counters));
    history = from_shares ? calloc(asics * (domains + 1) * 60, sizeof(*history)) : NULL;
    if (from_shares) TEST_ASSERT_NOT_NULL(history);
    memset(sums, 0, sizeof(sums));
    if (setjmp(task_done) == 0) monitor_task(state);
    TEST_ASSERT_EQUAL_UINT32(4, allocations);
    HashrateMonitorModule *monitor = &state->HASHRATE_MONITOR_MODULE;
    monitor_free(monitor->total_measurement);
    monitor_free(monitor->domain_measurements[0]);
    monitor_free(monitor->domain_measurements);
    monitor_free(monitor->error_measurement);
    pthread_mutex_destroy(&monitor->lock);
    TEST_ASSERT_EQUAL_UINT32(0, outstanding);
    free(state);
    state = NULL;
    free(history);
    history = NULL;
    free(software_rates);
    software_rates = NULL;
}

TEST_CASE("Current hashrate averages sparse raw intervals and cuts off on pause and startup", "[asic][hashrate]")
{
    run_monitor(ASIC_BZM, 1, 1);
}

TEST_CASE("ASIC and domain JSON rates share the headline rolling window and pause cutoff", "[asic][hashrate]")
{
    run_monitor(ASIC_BZM, 2, 2);
}

TEST_CASE("Four software-measured ASICs expose rates without common smoothing allocations", "[asic][hashrate]")
{
    run_monitor(ASIC_BZM, 4, 1);

}

TEST_CASE("Register-backed Bitmain ASICs keep instantaneous rates and allocate no smoothing history", "[asic][hashrate]")
{
    const AsicConfig asics[] = {ASIC_BM1397, ASIC_BM1366, ASIC_BM1368, ASIC_BM1370, ASIC_BM1373};
    for (unsigned i = 0; i < sizeof(asics) / sizeof(asics[0]); ++i) {
        run_monitor(asics[i], 2, asics[i].hash_domains);
    }
}

TEST_CASE("Hashrate averaging allocation failures clean up and leave telemetry unavailable", "[asic][hashrate]")
{
    const AsicConfig asics[] = {ASIC_BZM, ASIC_BM1397, ASIC_BM1370};
    for (unsigned i = 0; i < sizeof(asics) / sizeof(asics[0]); ++i) {
        for (unsigned failed = 1; failed <= 4; ++failed) {
            state = calloc(1, sizeof(*state));
            TEST_ASSERT_NOT_NULL(state);
            state->ASIC_initalized = true;
            state->DEVICE_CONFIG.family.asic = asics[i];
            state->DEVICE_CONFIG.family.asic_count = 2;
            state->DEVICE_CONFIG.family.asic.hash_domains = 2;
            allocations = deletes = 0;
            fail_allocation = failed;
            monitor_task(state);
            TEST_ASSERT_FALSE(state->HASHRATE_MONITOR_MODULE.is_initialized);
            TEST_ASSERT_EQUAL_UINT32(0, outstanding);
            TEST_ASSERT_EQUAL_UINT32(1, deletes);
            TEST_ASSERT_EQUAL_FLOAT(0, monitor_asic_rate(state, 0));
            TEST_ASSERT_EQUAL_FLOAT(0, monitor_domain_rate(state, 0, 0));
            free(state);
            state = NULL;
        }
    }
}
