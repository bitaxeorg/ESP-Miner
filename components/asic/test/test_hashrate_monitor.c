#include "hashrate_monitor_test_harness.h"
#include "unity.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

static HashrateMonitorTestState fixture;
static measurement_t totals[2], errors[2], domains[2][8];
static measurement_t *domain_rows[2];
static uint8_t stall_counts[2][8], *stall_count_rows[2];
static bool stalled[2][8], *stalled_rows[2];
static int64_t now_us;
static unsigned polls;
static bool send_counters;
static jmp_buf task_done;

int64_t hashrate_monitor_test_time_us(void)
{
    return now_us;
}

TickType_t hashrate_monitor_test_ticks(void)
{
    return pdMS_TO_TICKS(now_us / 1000);
}

void hashrate_monitor_test_delay(TickType_t ticks)
{
    (void)ticks;
}

void hashrate_monitor_test_delay_until(TickType_t *last_wake, TickType_t ticks)
{
    *last_wake += ticks;
    now_us += (uint64_t)ticks * portTICK_PERIOD_MS * 1000;
    if (++polls >= 60) {
        longjmp(task_done, 1);
    }
}

void hashrate_monitor_test_read_registers(HashrateMonitorTestState *state)
{
    for (uint8_t asic = 0; asic < state->DEVICE_CONFIG.family.asic_count; asic++) {
        if (state->DEVICE_CONFIG.family.asic.id == BM1397) {
            hashrate_monitor_test_register_read(state, REGISTER_HASHRATE,
                                               asic, 100000, now_us);
        } else if (send_counters) {
            for (uint8_t domain = 0;
                 domain < state->DEVICE_CONFIG.family.asic.hash_domains; domain++) {
                uint32_t value = domain == 0 ? 100 : 100 + polls;
                hashrate_monitor_test_register_read(
                    state, REGISTER_DOMAIN_0_COUNT + domain, asic, value, now_us);
            }
        }
    }
}

void hashrate_monitor_test_noinit_update(HashrateMonitorTestSystemModule *system)
{
    (void)system;
}

void *hashrate_monitor_test_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    void *allocation = malloc(size);
    TEST_ASSERT_NOT_NULL(allocation);
    return allocation;
}

static void init_fixture(Asic id, unsigned asic_count, unsigned hash_domains)
{
    memset(&fixture, 0, sizeof(fixture));
    fixture.DEVICE_CONFIG.family.asic.id = id;
    fixture.DEVICE_CONFIG.family.asic_count = asic_count;
    fixture.DEVICE_CONFIG.family.asic.hash_domains = hash_domains;
    fixture.ASIC_initalized = true;
    now_us = 60000000;
    polls = 0;
    send_counters = false;
}

static void init_query_fixture(Asic id, unsigned asic_count, unsigned hash_domains)
{
    init_fixture(id, asic_count, hash_domains);
    memset(totals, 0, sizeof(totals));
    memset(errors, 0, sizeof(errors));
    memset(domains, 0, sizeof(domains));
    memset(stall_counts, 0, sizeof(stall_counts));
    memset(stalled, 0, sizeof(stalled));
    for (unsigned asic = 0; asic < 2; asic++) {
        domain_rows[asic] = domains[asic];
        stall_count_rows[asic] = stall_counts[asic];
        stalled_rows[asic] = stalled[asic];
    }
    HashrateMonitorModule *monitor = &fixture.HASHRATE_MONITOR_MODULE;
    monitor->total_measurement = totals;
    monitor->error_measurement = errors;
    monitor->domain_measurements = domain_rows;
    monitor->domain_stall_counts = stall_count_rows;
    monitor->domain_stalled = stalled_rows;
    pthread_mutex_init(&monitor->lock, NULL);
    monitor->is_initialized = true;
}

static void finish_query_fixture(void)
{
    pthread_mutex_destroy(&fixture.HASHRATE_MONITOR_MODULE.lock);
}

static void run_polling_fixture(void)
{
    if (setjmp(task_done) == 0) {
        hashrate_monitor_test_task(&fixture);
    }
}

static void finish_polling_fixture(void)
{
    HashrateMonitorModule *monitor = &fixture.HASHRATE_MONITOR_MODULE;
    pthread_mutex_destroy(&monitor->lock);
    free(monitor->total_measurement);
    free(monitor->error_measurement);
    free(monitor->domain_measurements[0]);
    free(monitor->domain_measurements);
    free(monitor->domain_stall_counts[0]);
    free(monitor->domain_stall_counts);
    free(monitor->domain_stalled[0]);
    free(monitor->domain_stalled);
}

TEST_CASE("BM1397 hashrate-only polling never marks its domain stalled",
          "[asic][hashrate]")
{
    init_fixture(BM1397, 1, 1);
    run_polling_fixture();
    TEST_ASSERT_FALSE(fixture.HASHRATE_MONITOR_MODULE.domain_stalled[0][0]);
    TEST_ASSERT_EQUAL_UINT8(0, fixture.HASHRATE_MONITOR_MODULE.domain_stall_counts[0][0]);
    TEST_ASSERT_TRUE(fixture.HASHRATE_MONITOR_MODULE.domain_measurements[0][0].hashrate > 0);
    TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(&fixture, NULL));
    finish_polling_fixture();
}

TEST_CASE("polling reports missing counters without recovering fully silent ASICs",
          "[asic][hashrate]")
{
    init_fixture(BM1373, 2, 8);
    run_polling_fixture();
    for (unsigned asic = 0; asic < 2; asic++) {
        for (unsigned domain = 0; domain < 8; domain++) {
            TEST_ASSERT_TRUE(fixture.HASHRATE_MONITOR_MODULE.domain_stalled[asic][domain]);
        }
    }
    int count = 0;
    TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(&fixture, &count));
    TEST_ASSERT_EQUAL_INT(16, count);
    finish_polling_fixture();
}

TEST_CASE("polling permits recovery for frozen and advancing counters on one ASIC",
          "[asic][hashrate]")
{
    init_fixture(BM1370, 1, 4);
    send_counters = true;
    run_polling_fixture();
    int count = 0;
    TEST_ASSERT_TRUE(hashrate_monitor_test_has_partial_stall(&fixture, &count));
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_TRUE(fixture.HASHRATE_MONITOR_MODULE.domain_stalled[0][0]);
    finish_polling_fixture();
}

TEST_CASE("partial stall requires recent positive activity on the same ASIC",
          "[asic][hashrate]")
{
    init_query_fixture(BM1373, 2, 8);
    stalled[1][7] = true;
    domains[0][0] = (measurement_t){.time_us = now_us, .hashrate = 100};
    TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(&fixture, NULL));
    domains[1][6] = domains[0][0];
    TEST_ASSERT_TRUE(hashrate_monitor_test_has_partial_stall(&fixture, NULL));

    const measurement_t inactive[] = {
        {.time_us = 0, .hashrate = 100},
        {.time_us = now_us, .hashrate = 0},
        {.time_us = now_us - 30000000, .hashrate = 100},
        {.time_us = now_us + 1, .hashrate = 100},
    };
    for (unsigned index = 0; index < sizeof(inactive) / sizeof(inactive[0]); index++) {
        domains[1][6] = inactive[index];
        TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(&fixture, NULL));
    }
    domains[1][6] = (measurement_t){.time_us = now_us - 29999999, .hashrate = 100};
    TEST_ASSERT_TRUE(hashrate_monitor_test_has_partial_stall(&fixture, NULL));
    stalled[1][6] = true;
    TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(&fixture, NULL));
    finish_query_fixture();
}

TEST_CASE("counter wrap clears a stall and reset removes recovery eligibility",
          "[asic][hashrate]")
{
    init_query_fixture(BM1370, 1, 4);
    stalled[0][0] = true;
    stalled[0][1] = true;
    stall_counts[0][1] = 30;
    domains[0][1] = (measurement_t){.value = UINT32_MAX, .time_us = now_us - 1000000};
    hashrate_monitor_test_register_read(&fixture, REGISTER_DOMAIN_1_COUNT, 0, 0, now_us);
    TEST_ASSERT_FALSE(stalled[0][1]);
    TEST_ASSERT_EQUAL_UINT8(0, stall_counts[0][1]);
    TEST_ASSERT_TRUE(domains[0][1].hashrate > 0);
    TEST_ASSERT_TRUE(hashrate_monitor_test_has_partial_stall(&fixture, NULL));
    hashrate_monitor_test_reset_measurements(&fixture);
    TEST_ASSERT_FALSE(stalled[0][0]);
    TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(&fixture, NULL));
    finish_query_fixture();
}

TEST_CASE("partial stall query rejects unavailable monitor and BM1397",
          "[asic][hashrate]")
{
    int count = -1;
    TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(NULL, &count));
    TEST_ASSERT_EQUAL_INT(0, count);
    init_fixture(BM1370, 1, 4);
    count = -1;
    TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(&fixture, &count));
    TEST_ASSERT_EQUAL_INT(0, count);
    init_query_fixture(BM1397, 1, 1);
    stalled[0][0] = true;
    count = -1;
    TEST_ASSERT_FALSE(hashrate_monitor_test_has_partial_stall(&fixture, &count));
    TEST_ASSERT_EQUAL_INT(0, count);
    finish_query_fixture();
}
