#include "bm13xx_fixture_bindings.h"
#include "bm13xx_fixture.h"

#include <pthread.h>
#include <string.h>
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "bm1373.h"
#include "frequency_transition_bmXX.h"
#include "global_state.h"
#include "mining.h"
#include "serial.h"
#include "unity.h"

const bm13xx_fixture_driver_t
    bm13xx_fixture_drivers[BM13XX_FIXTURE_DRIVER_COUNT] = {
        {"BM1366", BM1366_init, BM1366_set_version_mask, BM1366_process_work, 0x13, 4},
        {"BM1368", BM1368_init, BM1368_set_version_mask, BM1368_process_work, 0x23, 5},
        {"BM1370", BM1370_init, BM1370_set_version_mask, BM1370_process_work, 0x23, 4},
        {"BM1373", BM1373_init, BM1373_set_version_mask, BM1373_process_work, 0x23, 1},
    };

enum { MAX_PACKETS = 128, JOB_SLOT = 0x10, JOB_SLOTS = 128 };
static GlobalState fixture_state;
static bm_job saved_job;
static bm_job *active_jobs[JOB_SLOTS];
static uint8_t valid_jobs[JOB_SLOTS];
static bm13xx_fixture_packet_t packets[MAX_PACKETS];
static size_t packet_count;
static uint8_t queued_response[BM13XX_FIXTURE_RESPONSE_SIZE];
static bool response_ready;
static unsigned failed_writes;
static unsigned delay_count;

GlobalState *bm13xx_fixture_begin(void)
{
    memset(active_jobs, 0, sizeof(active_jobs));
    memset(valid_jobs, 0, sizeof(valid_jobs));
    fixture_state = (GlobalState) {
        .DEVICE_CONFIG.family.asic_count = 2,
        .DEVICE_CONFIG.family.voltage_domains = 1,
        .DEVICE_CONFIG.family.asic.difficulty = 256,
        .DEVICE_CONFIG.family.asic.core_count = 112,
        .POWER_MANAGEMENT_MODULE.frequency_value = 200.0f,
        .POWER_MANAGEMENT_MODULE.actual_frequency = 200.0f,
        .ASIC_TASK_MODULE.active_jobs = active_jobs,
        .ASIC_TASK_MODULE.valid_jobs = valid_jobs,
    };
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(
        &fixture_state.ASIC_TASK_MODULE.valid_jobs_lock, NULL));
    packet_count = 0;
    response_ready = false;
    failed_writes = 0;
    delay_count = 0;
    return &fixture_state;
}

void bm13xx_fixture_end(void)
{
    /* Also catches a driver returning without releasing the job lock. */
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_trylock(
        &fixture_state.ASIC_TASK_MODULE.valid_jobs_lock));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(
        &fixture_state.ASIC_TASK_MODULE.valid_jobs_lock));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_destroy(
        &fixture_state.ASIC_TASK_MODULE.valid_jobs_lock));
}

void bm13xx_fixture_clear_packets(void)
{
    packet_count = 0;
    delay_count = 0;
}

size_t bm13xx_fixture_packet_count(void)
{
    return packet_count;
}

const bm13xx_fixture_packet_t *bm13xx_fixture_packet(size_t index)
{
    TEST_ASSERT_TRUE(index < packet_count);
    return &packets[index];
}

void bm13xx_fixture_fail_writes(unsigned count)
{
    failed_writes = count;
}

unsigned bm13xx_fixture_delay_count(void)
{
    return delay_count;
}

void bm13xx_fixture_set_job(uint32_t version, bool valid, bool present)
{
    saved_job = (bm_job) {.version = version};
    active_jobs[JOB_SLOT] = present ? &saved_job : NULL;
    valid_jobs[JOB_SLOT] = valid;
}

void bm13xx_fixture_queue_response(
    const uint8_t response[BM13XX_FIXTURE_RESPONSE_SIZE])
{
    response_ready = response != NULL;
    if (response_ready) {
        memcpy(queued_response, response, sizeof(queued_response));
    }
}

int SERIAL_send(uint8_t *bytes, int length, bool debug)
{
    (void)debug;
    TEST_ASSERT_TRUE(packet_count < MAX_PACKETS);
    TEST_ASSERT_TRUE(length > 0 && (size_t)length <= sizeof(packets[0].bytes));
    bm13xx_fixture_packet_t *packet = &packets[packet_count++];
    packet->length = (size_t)length;
    memcpy(packet->bytes, bytes, packet->length);
    if (failed_writes > 0) {
        failed_writes--;
        return length - 1;
    }
    return length;
}

esp_err_t SERIAL_set_baud(int baud)
{
    TEST_ASSERT_EQUAL_INT(3000000, baud);
    return ESP_OK;
}

void SERIAL_clear_buffer(void)
{
}

esp_err_t receive_work(uint8_t *buffer, int size, uint64_t *timestamp_us)
{
    TEST_ASSERT_EQUAL_INT(sizeof(queued_response), size);
    if (!response_ready) {
        return ESP_FAIL;
    }
    memcpy(buffer, queued_response, sizeof(queued_response));
    response_ready = false;
    *timestamp_us = 123456789;
    return ESP_OK;
}

int count_asic_chips(uint16_t count, uint16_t chip_id, int response_length)
{
    (void)chip_id;
    TEST_ASSERT_EQUAL_INT(11, response_length);
    return count;
}

int count_asic_chips_with_id_alias(
    uint16_t count, uint16_t chip_id, uint16_t alias, int response_length)
{
    TEST_ASSERT_EQUAL_HEX16(0x1372, chip_id);
    TEST_ASSERT_EQUAL_HEX16(0x1373, alias);
    TEST_ASSERT_EQUAL_INT(9, response_length);
    return count;
}

void do_frequency_transition(GlobalState *state, set_hash_frequency_fn set_frequency)
{
    /* Clock ramps are outside the version-rolling contract. */
    (void)state;
    (void)set_frequency;
}

void bm13xx_fixture_delay(TickType_t ticks)
{
    (void)ticks;
    delay_count++;
}
