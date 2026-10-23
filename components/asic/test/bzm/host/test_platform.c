#include "check.h"
#include "bzm/posix.h"
#include "bzm/hash.h"
#include "bzm/controls.h"
#include "bzm/runtime.h"
#include "bzm/job_store.h"
#include "bzm/platform_serial.h"
#include <string.h>

static struct {
    int64_t time;
    size_t writes, inits, clears;
    bool ready, fail_alloc;
    int baud;
    uint32_t wait_ms;
    uint16_t read_size, read_ms;
    int16_t read_result;
    bzm_status_t init_status, baud_status, clear_status;
} fixture;
static int64_t now(void *ctx) { CHECK(ctx == &fixture); return fixture.time; }
static void delay(void *ctx, uint32_t us) { CHECK(ctx == &fixture); fixture.time += us; }
static void *allocate(void *ctx, size_t count, size_t size, bool large)
{ CHECK(ctx == &fixture); (void)large; return fixture.fail_alloc ? NULL : calloc(count, size); }
static void release(void *ctx, void *ptr) { CHECK(ctx == &fixture); free(ptr); }
static bzm_status_t init(void *ctx)
{ CHECK(ctx == &fixture); ++fixture.inits; fixture.ready = fixture.init_status == BZM_OK; return fixture.init_status; }
static bool ready(void *ctx) { CHECK(ctx == &fixture); return fixture.ready; }
static bzm_status_t baud(void *ctx, int rate)
{ CHECK(ctx == &fixture); fixture.baud = rate; return rate > 0 ? fixture.baud_status : BZM_ERR_INVALID_ARG; }
static bzm_status_t wait_tx(void *ctx, uint32_t ms) { CHECK(ctx == &fixture); fixture.wait_ms = ms; return BZM_OK; }
static bzm_status_t clear(void *ctx)
{ CHECK(ctx == &fixture); ++fixture.clears; return fixture.clear_status; }
static int write_bytes(void *ctx, const uint8_t *bytes, size_t size)
{ CHECK(ctx == &fixture); CHECK(bytes && size); ++fixture.writes; return (int)size; }
static int16_t read_bytes(void *ctx, uint8_t *bytes, uint16_t size, uint16_t ms)
{
    CHECK(ctx == &fixture);
    CHECK(bytes != NULL);
    fixture.read_size = size;
    fixture.read_ms = ms;
    fixture.time += (int64_t)ms * 1000;
    if (fixture.read_result > 0) {
        CHECK(fixture.read_result <= size);
        memset(bytes, 0xa5, (size_t)fixture.read_result);
    }
    return fixture.read_result;
}
static bzm_status_t hash_failure(void *ctx, const uint8_t *data, size_t size, uint8_t digest[32])
{ CHECK(ctx == &fixture); (void)data; (void)size; memset(digest, 0, 32); return BZM_ERR_IO; }
int main(void)
{
    CHECK(bzm_port_serial_init() != BZM_OK);
    bzm_control_status_t status = {.valid = true};
    CHECK(bzm_control_get_safety_status(&status) != BZM_OK && !status.valid);
    CHECK(bzm_control_set_core_enabled(true) != BZM_OK);
    bzm_platform_ops_t ops;
    bzm_platform_posix_defaults(&ops);
    CHECK(ops.now_us(NULL) > 0);
    ops = (bzm_platform_ops_t){.now_us = now, .sleep_us = delay, .calloc = allocate, .free = release,
        .serial_init = init, .serial_is_initialized = ready, .serial_set_baud = baud,
        .serial_wait_tx = wait_tx, .serial_clear_rx = clear, .serial_write = write_bytes, .serial_read = read_bytes};
    CHECK(bzm_platform_bind(&ops, &fixture));
    ops.free = NULL;
    CHECK(!bzm_platform_bind(&ops, &fixture));
    ops.free = release;
    ops.sha256 = hash_failure;
    CHECK(bzm_platform_bind(&ops, &fixture));
    uint8_t digest[32];
    CHECK(!bzm_double_sha256((const uint8_t *)"abc", 3, digest));
    ops.sha256 = NULL;
    CHECK(bzm_platform_bind(&ops, &fixture));
    CHECK(bzm_port_serial_init() == BZM_OK && fixture.ready);
    bzm_sleep_ms(20);
    CHECK(bzm_now_us() == 20000);
    // Exercise the production transport adapter, including the portable session setup.
    CHECK(bzm_port_serial_prepare_session(2000000) == BZM_OK);
    CHECK(fixture.inits == 1 && fixture.clears == 1 && fixture.baud == 2000000);
    fixture.ready = false;
    fixture.init_status = BZM_ERR_IO;
    CHECK(bzm_port_serial_prepare_session(115200) == BZM_ERR_IO);
    CHECK(fixture.inits == 2 && fixture.clears == 1 && fixture.baud == 2000000);
    fixture.init_status = BZM_OK;
    fixture.baud_status = BZM_ERR_IO;
    CHECK(bzm_port_serial_prepare_session(115200) == BZM_ERR_IO);
    CHECK(fixture.inits == 3 && fixture.clears == 1);
    fixture.baud_status = BZM_OK;
    fixture.clear_status = BZM_ERR_IO;
    CHECK(bzm_port_serial_prepare_session(115200) == BZM_ERR_IO);
    CHECK(fixture.inits == 3 && fixture.clears == 2);
    fixture.clear_status = BZM_OK;
    uint8_t bytes[4] = {1, 2, 3, 4};
    const bzm_serial_ops_t *serial = &bzm_platform_serial_ops;
    serial->lock(NULL);
    CHECK(serial->write(NULL, bytes, sizeof(bytes)) == sizeof(bytes));
    CHECK(fixture.writes == 1);
    fixture.read_result = 3;
    CHECK(serial->read(NULL, bytes, sizeof(bytes), 17) == 3);
    CHECK(bytes[0] == 0xa5 && bytes[2] == 0xa5 && bytes[3] == 4);
    CHECK(fixture.read_size == sizeof(bytes) && fixture.read_ms == 17);
    CHECK(serial->now_us(NULL) == 37000);
    fixture.read_result = -1;
    CHECK(serial->read(NULL, bytes, sizeof(bytes), 0) == -1);
    serial->clear_rx(NULL);
    CHECK(fixture.clears == 3);
    serial->unlock(NULL);
    CHECK(bzm_port_serial_wait_tx_done(23) == BZM_OK && fixture.wait_ms == 23);
    asic_job_t job = {.version = 0x20000000, .version_mask = 0x6000, .ntime = 1234};
    bzm_job_store_t store;
    CHECK(bzm_job_store_init(&store));
    bzm_work_handle_t handle;
    CHECK(bzm_job_store_store_generated(&store, &job, &handle));
    asic_job_t snapshot;
    CHECK(bzm_job_store_snapshot(&store, handle, &snapshot));
    bzm_job_store_invalidate_all(&store);
    CHECK(!bzm_job_store_snapshot(&store, handle, &snapshot));
    bzm_job_store_destroy(&store);
    fixture.fail_alloc = true;
    CHECK(!bzm_job_store_init(&store));
    bzm_device_t bzm = {.asic_count = 4, .core_count = 236};
    CHECK(!bzm_driver_state_init(&bzm));
    puts("BZM portable callbacks, stale jobs and allocation failure passed");
    return 0;
}
