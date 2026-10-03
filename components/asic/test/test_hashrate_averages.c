#include "unity.h"
#include "global_state.h"
#include "system.h"
#include "asic.h"
#include "esp_heap_caps.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf monitor_exit;
static GlobalState monitor_state;
static unsigned sample_index;
static unsigned hashing_samples;
static unsigned total_samples;
static bool stop_asic;
static void *allocations[4];
static unsigned allocation_count;

static void *monitor_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    TEST_ASSERT_LESS_THAN_UINT32(4, allocation_count);
    void *result = malloc(size);
    TEST_ASSERT_NOT_NULL(result);
    allocations[allocation_count++] = result;
    return result;
}

static void monitor_read_registers(GlobalState *state)
{
    state->HASHRATE_MONITOR_MODULE.total_measurement[0].hashrate =
        sample_index < hashing_samples ? 1000.0f : 0.0f;
    state->HASHRATE_MONITOR_MODULE.error_measurement[0].hashrate =
        sample_index < hashing_samples ? 10.0f : 0.0f;
}

static void monitor_delay(TickType_t ticks)
{
    (void)ticks;
}

static void monitor_noinit_update(SystemModule *system)
{
    (void)system;
}

static void monitor_delay_until(TickType_t *wake_time, TickType_t ticks)
{
    (void)wake_time;
    (void)ticks;
    if (++sample_index >= total_samples) {
        longjmp(monitor_exit, 1);
    }
    monitor_state.ASIC_initalized = !stop_asic || sample_index < hashing_samples;
    // Let the idle task run during the long-window regression, without
    // waiting a real second for each simulated sample.
    if (sample_index % 100 == 0) vTaskDelay(1);
}

// Run the real task, including its stopped/zero-rate branches. Keep this
// instance's symbols private to avoid affecting result-task test fixtures.
#define hashrate_monitor_task test_monitor_task
#define hashrate_monitor_reset_measurements test_monitor_reset
#define hashrate_monitor_register_read test_monitor_register_read
#define update_hashrate test_monitor_update_hashrate
#define update_hash_counter test_monitor_update_hash_counter
#define heap_caps_malloc monitor_malloc
#define ASIC_read_registers monitor_read_registers
#define SYSTEM_noinit_update monitor_noinit_update
#define vTaskDelay monitor_delay
#ifdef vTaskDelayUntil
#undef vTaskDelayUntil
#endif
#define vTaskDelayUntil monitor_delay_until
#include "../../../main/tasks/hashrate_monitor_task.c"
#undef vTaskDelayUntil
#undef vTaskDelay
#undef SYSTEM_noinit_update
#undef ASIC_read_registers
#undef heap_caps_malloc
#undef update_hash_counter
#undef update_hashrate
#undef hashrate_monitor_register_read
#undef hashrate_monitor_reset_measurements
#undef hashrate_monitor_task

static void run_monitor(unsigned active_seconds, unsigned seconds, bool stopped)
{
    memset(&monitor_state, 0, sizeof(monitor_state));
    monitor_state.DEVICE_CONFIG.family.asic_count = 1;
    monitor_state.DEVICE_CONFIG.family.asic.hash_domains = 1;
    monitor_state.ASIC_initalized = true;
    sample_index = 0;
    hashing_samples = active_seconds;
    total_samples = seconds;
    stop_asic = stopped;
    allocation_count = 0;
    poll_count = 0;
    hashrate_10m_prev = hashrate_1h_prev = 0;
    if (setjmp(monitor_exit) == 0) {
        test_monitor_task(&monitor_state);
    }
    pthread_mutex_destroy(&monitor_state.HASHRATE_MONITOR_MODULE.lock);
    for (unsigned i = 0; i < allocation_count; ++i) free(allocations[i]);
}

TEST_CASE("hashrate averages include stopped time", "[asic][hashrate]")
{
    run_monitor(30, 60, true);
    TEST_ASSERT_EQUAL_UINT32(60, poll_count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, monitor_state.SYSTEM_MODULE.hashrate_1m);
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_state.SYSTEM_MODULE.current_hashrate);
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_state.SYSTEM_MODULE.error_percentage);
}

TEST_CASE("hashrate averages include zero-rate initialized ASICs", "[asic][hashrate]")
{
    run_monitor(30, 60, false);
    TEST_ASSERT_EQUAL_UINT32(60, poll_count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, monitor_state.SYSTEM_MODULE.hashrate_1m);
}

TEST_CASE("all hashrate windows decay after a full stopped hour", "[asic][hashrate]")
{
    run_monitor(3600, 7200, true);
    TEST_ASSERT_EQUAL_UINT32(7200, poll_count);
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_state.SYSTEM_MODULE.hashrate_1m);
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_state.SYSTEM_MODULE.hashrate_10m);
    TEST_ASSERT_EQUAL_FLOAT(0, monitor_state.SYSTEM_MODULE.hashrate_1h);
}
