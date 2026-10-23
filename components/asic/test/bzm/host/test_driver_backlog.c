#include "check.h"
#include "bzm/serial_buffers.h"

#define bzm_port_serial_prepare_session fixture_serial_prepare
#define bzm_control_set_asic_reset fixture_reset
#define bzm_control_pulse_asic_reset fixture_pulse
#define bzm_bringup_start fixture_chip_start
#include "../../../bzm/bzm_driver.c"

/* Continuous 2 Mbaud 8N1 RX; assignment checkpoints and the idle service drain it. */
/* Parsing is instantaneous; ESP scheduling, ISR, PSRAM and allocator delays are not modeled. */
#define MODEL_BITS_PER_BYTE 10U
#define MODEL_RX_RATE (BZM_BAUD_RATE / MODEL_BITS_PER_BYTE)
#define MODEL_STALL_US 200000U

static struct {
    uint64_t now, tx_done, arrived, queued, highwater;
    unsigned polls;
    bool active;
} model;

static void advance(uint64_t us)
{
    model.now += us;
    if (!model.active) return;
    uint64_t arrived = model.now * MODEL_RX_RATE / 1000000U;
    model.queued += arrived - model.arrived;
    model.arrived = arrived;
    if (model.queued > model.highwater) model.highwater = model.queued;
}

static int64_t now_us(void *context) { (void)context; return (int64_t)model.now; }
static void sleep_us(void *context, uint32_t us) { (void)context; advance(us); }
static bool authorize(void *context) { (void)context; return true; }

static int16_t read_bytes(void *context, uint8_t *data, uint16_t size, uint16_t timeout_ms)
{
    (void)context;
    CHECK(size > 0);
    if (size == 1) ++model.polls;
    if (!model.queued && timeout_ms) advance(1000000U / MODEL_RX_RATE);
    size_t length = size < model.queued ? size : (size_t)model.queued;
    memset(data, 0, length);
    model.queued -= length;
    return (int16_t)length;
}

static bzm_status_t wait_tx(void *context, uint32_t timeout_ms)
{
    (void)context;
    uint64_t wait = model.tx_done > model.now ? model.tx_done - model.now : 0;
    CHECK(wait <= (uint64_t)timeout_ms * 1000);
    advance(wait);
    return BZM_OK;
}

static int write_bytes(void *context, const uint8_t *data, size_t size)
{
    (void)context; (void)data;
    if (model.tx_done < model.now) model.tx_done = model.now;
    model.tx_done += size * MODEL_BITS_PER_BYTE * 1000000U / BZM_BAUD_RATE;
    uint64_t capacity_us = (uint64_t)BZM_SERIAL_TX_BUFFER_BYTES * MODEL_BITS_PER_BYTE * 1000000U / BZM_BAUD_RATE;
    // uart_write_bytes blocks only until the bounded ring has room, not until TX is empty.
    if (model.tx_done - model.now > capacity_us)
        advance(model.tx_done - model.now - capacity_us);
    return (int)size;
}

static bzm_status_t clear_rx(void *context) { (void)context; model.queued = 0; return BZM_OK; }
bzm_status_t fixture_serial_prepare(int baud) { CHECK(baud == BZM_BAUD_RATE); return BZM_OK; }
bzm_status_t fixture_reset(bool released) { (void)released; return BZM_OK; }
bzm_status_t fixture_pulse(void) { return BZM_OK; }
bzm_bringup_outcome_t fixture_chip_start(bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
        void *context, const bzm_bringup_telemetry_policy_t *policy, bzm_bringup_report_t *report)
{
    (void)ops; (void)context; (void)policy;
    *state = (bzm_bringup_state_t){.running = true, .clock_mhz = 800};
    *report = (bzm_bringup_report_t){0};
    return BZM_BRINGUP_GOOD;
}

static void run(bool scheduled, bool stalled)
{
    memset(&model, 0, sizeof(model));
    bzm_device_t device = {
        .asic_count = BZM_BRINGUP_ASIC_COUNT, .core_count = BZM_ENGINES_PER_ASIC, .initialized = true,
    };
    CHECK(bzm_driver_state_init(&device));
    bzm_driver_set_operation_authorizer(authorize, NULL);
    bzm_bringup_telemetry_policy_t policy = {0};
    bzm_bringup_report_t report;
    CHECK(bzm_driver_start(&device, &policy, &report) == BZM_BRINGUP_GOOD);
    bzm_driver_set_dispatch_authorizer(authorize, NULL);
    model.now = model.tx_done = 0;
    model.active = true;
    asic_job_t job = {.version = 0x20000000, .ntime = 1234, .nbits = 0x1702369d, .version_mask = 0x6000};
    for (unsigned i = 0; i < BZM_ENGINE_ROWS * 3; ++i) {
        if (scheduled) {
            uint64_t delay = (uint64_t)(bzm_driver_job_frequency_ms(&device) * 1000);
            // Model the 100 ms service while work runs; no delay within a refill.
            while (delay > 100000) {
                advance(100000);
                CHECK(bzm_serial_poll(&TRANSPORT, 0).status == BZM_OK);
                delay -= 100000;
            }
            advance(delay);
        }
        if (stalled && i == BZM_ENGINE_ROWS) {
            uint64_t fill_us = ((uint64_t)BZM_SERIAL_RX_BUFFER_BYTES + 1U) * 1000000U / MODEL_RX_RATE;
            advance(fill_us > MODEL_STALL_US ? fill_us : MODEL_STALL_US);
        }
        unsigned polls_before = model.polls;
        CHECK(driver_send_work(&device, &job));
        CHECK(model.polls == polls_before + 1);
        if (!stalled) {
            CHECK(model.queued == 0);
            CHECK(model.highwater <= BZM_SERIAL_RX_BUFFER_BYTES);
        } else if (i == BZM_ENGINE_ROWS) {
            CHECK(model.highwater > BZM_SERIAL_RX_BUFFER_BYTES);
            CHECK(model.queued > 0);
        }
    }
    printf("backlog scheduled=%u stalled=%u highwater=%llu bytes\n", scheduled, stalled,
           (unsigned long long)model.highwater);
    CHECK(model.highwater > 0);
    CHECK(model.queued == 0);
    /* The unbounded queue exposes excess capacity demand; it injects no hardware overflow error. */
    CHECK(!bzm_driver_has_io_fault());
}

int main(void)
{
    const bzm_platform_ops_t platform = {
        .now_us = now_us, .sleep_us = sleep_us, .serial_read = read_bytes,
        .serial_wait_tx = wait_tx, .serial_write = write_bytes, .serial_clear_rx = clear_rx,
    };
    CHECK(bzm_platform_bind(&platform, NULL));
    run(false, false);
    run(true, false);
    run(true, true);
    bzm_serial_transport_deinit(&TRANSPORT);
    bzm_job_store_destroy(&BZM_STATE->job_store);
    CHECK(pthread_mutex_destroy(&REACTOR_LOCK) == 0);
    bzm_free(BZM_STATE);
    BZM_STATE = NULL;
    return 0;
}
