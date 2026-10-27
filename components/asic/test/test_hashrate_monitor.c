#include "unity.h"
#include "device_config.h"
#include "hashrate_monitor_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <setjmp.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

#define TEST_STUB_GLOBAL_STATE_H
#define GLOBAL_STATE_H_
typedef struct SystemModule {
    float current_hashrate, error_percentage;
    float hashrate_1m, hashrate_10m, hashrate_1h;
} SystemModule;
typedef struct GlobalState {
    DeviceConfig DEVICE_CONFIG;
    HashrateMonitorModule HASHRATE_MONITOR_MODULE;
    SystemModule SYSTEM_MODULE;
    bool ASIC_initalized;
} GlobalState;

// QEMU has no PSRAM; retain the production allocation sizes on its internal heap.
static void *monitor_alloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
static void monitor_noinit(SystemModule *system, float interval_hashrate);
static void monitor_delay(TickType_t ticks);
static void monitor_delay_until(TickType_t *wake, TickType_t ticks);
#define heap_caps_malloc monitor_alloc
#define ASIC_read_registers monitor_read
#define SYSTEM_noinit_update monitor_noinit
#define vTaskDelay monitor_delay
#undef vTaskDelayUntil
#define vTaskDelayUntil monitor_delay_until
#define hashrate_monitor_task monitor_task
#define hashrate_monitor_reset_measurements monitor_reset
#define hashrate_monitor_register_read monitor_register_read
#define update_hashrate monitor_update_hashrate
#define update_hash_counter monitor_update_counter
#include "../../../main/tasks/hashrate_monitor_task.c"

static GlobalState state;
static jmp_buf task_done;
static unsigned sample;
static double minute_sum;
static float samples[60];

void monitor_read(GlobalState *s)
{
    // One D512 result every two seconds, sampled through the production counter path.
    uint32_t counter = 512 * ((sample + 1) / 2);
    uint64_t time_us = (uint64_t)(sample + 1) * 1000000;
    monitor_register_read(s, REGISTER_TOTAL_COUNT, 0, counter, time_us);
    monitor_register_read(s, REGISTER_DOMAIN_0_COUNT, 0, counter, time_us);
}
static void monitor_delay(TickType_t ticks) { (void)ticks; }
static void monitor_noinit(SystemModule *system, float interval_hashrate)
{
    const float spike_ghs = 512 * 4294967296.0 / 1e9;
    float expected = sample < 3600 && sample % 2 ? spike_ghs : 0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected, interval_hashrate);
    minute_sum -= samples[sample % 60];
    samples[sample % 60] = expected;
    minute_sum += expected;
    unsigned count = sample < 60 ? sample + 1 : 60;
    TEST_ASSERT_FLOAT_WITHIN(0.002f, minute_sum / count, system->hashrate_1m);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, minute_sum / count, system->current_hashrate);
    TEST_ASSERT_EQUAL_FLOAT(system->hashrate_1m, system->current_hashrate);
    TEST_ASSERT_EQUAL_UINT32(sample + 1, poll_count);
    if (sample == 59 || sample == 599 || sample == 3599) {
        TEST_ASSERT_FLOAT_WITHIN(0.002f, spike_ghs / 2, system->hashrate_1m);
        TEST_ASSERT_FLOAT_WITHIN(0.002f, spike_ghs / 2, system->hashrate_10m);
        TEST_ASSERT_FLOAT_WITHIN(0.002f, spike_ghs / 2, system->hashrate_1h);
    }
    if (sample == 7199) {
        TEST_ASSERT_EQUAL_FLOAT(0, system->hashrate_1m);
        TEST_ASSERT_EQUAL_FLOAT(0, system->hashrate_10m);
        TEST_ASSERT_EQUAL_FLOAT(0, system->hashrate_1h);
    }
}
static void monitor_delay_until(TickType_t *wake, TickType_t ticks)
{
    (void)wake; (void)ticks;
    ++sample;
    if (sample == 3600) state.ASIC_initalized = false;
    if (sample == 7200) longjmp(task_done, 1);
}

TEST_CASE("Current hashrate is a rolling average while counters retain raw zero-inclusive intervals", "[asic][hashrate]")
{
    state = (GlobalState){.ASIC_initalized = true};
    state.DEVICE_CONFIG.family.asic_count = 1;
    state.DEVICE_CONFIG.family.asic.hash_domains = 1;
    sample = 0;
    minute_sum = 0;
    memset(samples, 0, sizeof(samples));
    poll_count = 0;
    if (setjmp(task_done) == 0) monitor_task(&state);
    HashrateMonitorModule *monitor = &state.HASHRATE_MONITOR_MODULE;
    free(monitor->total_measurement);
    free(monitor->domain_measurements[0]);
    free(monitor->domain_measurements);
    free(monitor->error_measurement);
    pthread_mutex_destroy(&monitor->lock);
}
