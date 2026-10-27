#include "unity.h"
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
static size_t history_bytes, cache_bytes;
static unsigned average_scan_checks;
static int monitor_isnan(float value)
{
    ++average_scan_checks;
    return isnan(value);
}
static void *monitor_calloc(size_t count, size_t size, uint32_t caps)
{
    ++allocations;
    TEST_ASSERT_EQUAL_UINT32(allocations == 6 ? MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT : MALLOC_CAP_SPIRAM, caps);
    if (allocations == 5) history_bytes = count * size;
    if (allocations == 6) cache_bytes = count * size;
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
static float retained_10m, retained_1h;
static unsigned domain_measurement_calls;

esp_err_t monitor_domain_measurement(GlobalState *s, uint8_t asic, uint8_t domain,
                                   asic_domain_measurement_t *measurement)
{
    ++domain_measurement_calls;
    const measurement_t *raw = &s->HASHRATE_MONITOR_MODULE.domain_measurements[asic][domain];
    measurement->hashrate = raw->hashrate;
    measurement->time_us = raw->time_us;
    return ESP_OK;
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
static void monitor_delay(TickType_t ticks) { (void)ticks; }

static void assert_json_rates(void)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    domain_measurement_calls = 0;
    unsigned scans = average_scan_checks;
    system_api_add_hashrate_monitor(doc, root, state);
    TEST_ASSERT_EQUAL_UINT32(from_shares ? 0 : test_asics * test_domains, domain_measurement_calls);
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
                series[sample % 60] = raw;
                sums[a][d] += raw;
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
    if (from_shares || total_raw > 0) {
        unsigned slot = headline_count % 60;
        headline_sum -= headline_history[slot];
        headline_history[slot] = total_raw;
        headline_sum += total_raw;
        ++headline_count;
    }
    float headline_mean = headline_count ? headline_sum / (headline_count < 60 ? headline_count : 60) : 0;
    float tolerance = fmaxf(0.002f, headline_mean * 1e-6f);
    if (from_shares) TEST_ASSERT_FLOAT_WITHIN(tolerance, total_mean, headline_mean);
    TEST_ASSERT_FLOAT_WITHIN(tolerance, headline_mean, system->hashrate_1m);
    float expected_current = from_shares ? (visible ? headline_mean : 0) : total_raw;
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected_current, system->current_hashrate);
    TEST_ASSERT_EQUAL_UINT32(headline_count, poll_count);
    if (!from_shares && sample == pause_sample) {
        retained_10m = system->hashrate_10m;
        retained_1h = system->hashrate_1h;
    }
    if (!from_shares && sample > pause_sample) {
        TEST_ASSERT_EQUAL_FLOAT(retained_10m, system->hashrate_10m);
        TEST_ASSERT_EQUAL_FLOAT(retained_1h, system->hashrate_1h);
    }
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
        TEST_ASSERT_TRUE(system->hashrate_1m > 0);
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
    if (sample == end_sample) longjmp(task_done, 1);
}
static void run_monitor(AsicConfig asic, unsigned asics, unsigned domains)
{
    state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    state->ASIC_initalized = true;
    state->DEVICE_CONFIG.family.asic = asic;
    from_shares = asic.hashrate_from_shares;
    direct_register = !from_shares && asic.id == BM1397;
    pause_sample = from_shares ? 3600 : 119;
    end_sample = from_shares ? 7200 : 180;
    state->DEVICE_CONFIG.family.asic_count = test_asics = asics;
    state->DEVICE_CONFIG.family.asic.hash_domains = test_domains = domains;
    sample = poll_count = allocations = fail_allocation = deletes = headline_count = 0;
    history_bytes = cache_bytes = 0;
    headline_sum = 0;
    memset(headline_history, 0, sizeof(headline_history));
    TEST_ASSERT_EQUAL_UINT32(0, outstanding);
    memset(counters, 0, sizeof(counters));
    history = from_shares ? calloc(asics * (domains + 1) * 60, sizeof(*history)) : NULL;
    if (from_shares) TEST_ASSERT_NOT_NULL(history);
    memset(sums, 0, sizeof(sums));
    if (setjmp(task_done) == 0) monitor_task(state);
    unsigned series = asics * (domains == 1 ? 1 : domains + 1);
    TEST_ASSERT_EQUAL_UINT32(from_shares ? series * 60 * sizeof(float) : 0, history_bytes);
    TEST_ASSERT_EQUAL_UINT32(from_shares ? series * sizeof(struct HashrateCache) : 0, cache_bytes);
    TEST_ASSERT_EQUAL_UINT32(from_shares ? 6 : 4, allocations);
    if (!from_shares) TEST_ASSERT_NULL(state->HASHRATE_MONITOR_MODULE.hashrate_history);
    HashrateMonitorModule *monitor = &state->HASHRATE_MONITOR_MODULE;
    if (from_shares) TEST_ASSERT_TRUE(atomic_is_lock_free(&monitor->hashrate_cache[0].rate));
    monitor_free(monitor->total_measurement);
    monitor_free(monitor->domain_measurements[0]);
    monitor_free(monitor->domain_measurements);
    monitor_free(monitor->error_measurement);
    monitor_free(monitor->hashrate_history);
    monitor_free(monitor->hashrate_cache);
    pthread_mutex_destroy(&monitor->lock);
    TEST_ASSERT_EQUAL_UINT32(0, outstanding);
    free(state);
    state = NULL;
    free(history);
    history = NULL;
}

TEST_CASE("Current hashrate averages sparse raw intervals and cuts off on pause and startup", "[asic][hashrate]")
{
    TEST_ASSERT_TRUE(ASIC_BZM.hashrate_from_shares);
    run_monitor(ASIC_BZM, 1, 1);
}

TEST_CASE("ASIC and domain JSON rates share the headline rolling window and pause cutoff", "[asic][hashrate]")
{
    run_monitor(ASIC_BZM, 2, 2);
}

TEST_CASE("Four single-domain ASICs share 1 KiB of cached history without clipping sparse samples", "[asic][hashrate]")
{
    run_monitor(ASIC_BZM, 4, 1);
    TEST_ASSERT_EQUAL_UINT32(960, history_bytes);
    TEST_ASSERT_EQUAL_UINT32(64, cache_bytes);
}

TEST_CASE("Register-backed Bitmain ASICs keep instantaneous rates and allocate no smoothing history", "[asic][hashrate]")
{
    const AsicConfig asics[] = {ASIC_BM1397, ASIC_BM1366, ASIC_BM1368, ASIC_BM1370, ASIC_BM1373};
    for (unsigned i = 0; i < sizeof(asics) / sizeof(asics[0]); ++i) {
        TEST_ASSERT_FALSE(asics[i].hashrate_from_shares);
        run_monitor(asics[i], 2, asics[i].hash_domains);
    }
}

TEST_CASE("Share-rate smoothing follows ASIC capability rather than the BZM identifier", "[asic][hashrate]")
{
    AsicConfig asic = ASIC_BM1370;
    asic.hashrate_from_shares = true;
    run_monitor(asic, 2, 2);
}

TEST_CASE("Hashrate averaging allocation failures clean up and leave telemetry unavailable", "[asic][hashrate]")
{
    const AsicConfig asics[] = {ASIC_BZM, ASIC_BM1397, ASIC_BM1370};
    for (unsigned i = 0; i < sizeof(asics) / sizeof(asics[0]); ++i) {
        for (unsigned failed = 1; failed <= (asics[i].hashrate_from_shares ? 6 : 4); ++failed) {
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

#undef vTaskDelay
#undef vTaskDelete

typedef struct {
    GlobalState *state;
    atomic_bool stop, done, invalid_rate;
    atomic_uint reads;
} cache_reader_t;

static void cache_reader(void *context)
{
    cache_reader_t *reader = context;
    bool check_json = true;
    while (!atomic_load(&reader->stop)) {
        float total = monitor_asic_rate(reader->state, 0);
        float domain = monitor_domain_rate(reader->state, 0, 0);
        if (!(total >= 1 && total <= 16 && domain >= 1 && domain <= 16)) {
            atomic_store(&reader->invalid_rate, true);
        }
        if (check_json) {
            yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
            if (doc) {
                yyjson_mut_val *root = yyjson_mut_obj(doc);
                system_api_add_hashrate_monitor(doc, root, reader->state);
                yyjson_mut_val *asics = yyjson_mut_obj_get(yyjson_mut_obj_get(root, "hashrateMonitor"), "asics");
                yyjson_mut_val *asic = yyjson_mut_arr_get(asics, 0);
                float json_total = yyjson_mut_get_real(yyjson_mut_obj_get(asic, "total"));
                float json_domain = yyjson_mut_get_real(yyjson_mut_arr_get(yyjson_mut_obj_get(asic, "domains"), 0));
                if (json_total != 8 || json_domain != 8) atomic_store(&reader->invalid_rate, true);
                yyjson_mut_doc_free(doc);
            } else {
                atomic_store(&reader->invalid_rate, true);
            }
            check_json = false;
        }
        atomic_fetch_add(&reader->reads, 1);
        vTaskDelay(1);
    }
    atomic_store(&reader->done, true);
    vTaskSuspend(NULL);
}

TEST_CASE("Cached share rates and JSON never wait for the raw mutex during concurrent updates and resets", "[asic][hashrate]")
{
    GlobalState *s = calloc(1, sizeof(*s));
    TEST_ASSERT_NOT_NULL(s);
    s->DEVICE_CONFIG.family.asic = ASIC_BZM;
    s->DEVICE_CONFIG.family.asic_count = 1;
    s->ASIC_initalized = true;
    measurement_t total = {.hashrate = 8}, domain = {0}, errors = {0};
    measurement_t *domains[] = {&domain};
    struct HashrateCache cached = {0};
    float samples[HASHRATE_1M_SIZE] = {0};
    HashrateMonitorModule *monitor = &s->HASHRATE_MONITOR_MODULE;
    monitor->total_measurement = &total;
    monitor->domain_measurements = domains;
    monitor->error_measurement = &errors;
    monitor->hashrate_history = samples;
    monitor->hashrate_cache = &cached;
    atomic_init(&cached.rate, 0);
    atomic_init(&monitor->is_initialized, false);
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_asic_rate(s, 0));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&monitor->lock, NULL));
    unsigned long saved_poll_count = poll_count;
    poll_count = 0;
    update_measurement_history(monitor, 1, 1, true);
    ++poll_count;
    atomic_store_explicit(&monitor->is_initialized, true, memory_order_release);

    cache_reader_t reader = {.state = s};
    atomic_init(&reader.stop, false);
    atomic_init(&reader.done, false);
    atomic_init(&reader.invalid_rate, false);
    atomic_init(&reader.reads, 0);
    TaskHandle_t task = NULL;
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_lock(&monitor->lock));
    BaseType_t created = xTaskCreatePinnedToCore(cache_reader, "cached rates", 4096, &reader,
        uxTaskPriorityGet(NULL), &task, (xPortGetCoreID() + 1) % portNUM_PROCESSORS);
    vTaskDelay(pdMS_TO_TICKS(30));
    unsigned reads_while_locked = atomic_load(&reader.reads);
    pthread_mutex_unlock(&monitor->lock);
    for (unsigned i = 0; i < 100 && !atomic_load(&reader.reads); ++i) vTaskDelay(1);
    for (unsigned i = 0; i < 120 && created == pdPASS; ++i) {
        pthread_mutex_lock(&monitor->lock);
        total.hashrate = i % 2 ? 1 : 16;
        update_measurement_history(monitor, 1, 1, true);
        ++poll_count;
        pthread_mutex_unlock(&monitor->lock);
        if (i % 10 == 0) monitor_reset(s);
        vTaskDelay(1);
    }
    atomic_store(&reader.stop, true);
    for (unsigned i = 0; i < 100 && !atomic_load(&reader.done); ++i) vTaskDelay(1);
    bool done = atomic_load(&reader.done), invalid = atomic_load(&reader.invalid_rate);
    if (task) vTaskDelete(task);
    pthread_mutex_destroy(&monitor->lock);
    poll_count = saved_poll_count;
    free(s);
    TEST_ASSERT_EQUAL_INT(pdPASS, created);
    TEST_ASSERT_GREATER_THAN_UINT32(0, reads_while_locked);
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_FALSE(invalid);
}
