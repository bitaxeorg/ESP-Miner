#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "asic_init.h"
#include "asic_reset.h"
#include "crc.h"
#include "global_state.h"
#include "nvs_config.h"
#include "register_dump.h"
#include "serial.h"
#include "vcore.h"

#define REGISTER_COUNT 256
#define RX_MAX 128             // bytes kept per read, every chip of the chain answers a broadcast read
#define RX_FIRST_TIMEOUT_MS 30 // wait for the answer of the whole chain
#define RX_GAP_MS 5            // idle time that ends an answer
#define PASSES 2               // each stage is read twice to spot volatile registers
#define MAX_SCANS 4
#define TX_LOG_MAX 1024       // frames sent to the chips that are kept, the reads of the scans are left out
#define TX_DATA_MAX RX_MAX

#define RESET_LOW_MS 100
#define RESET_RELEASE_MS 100
#define UART_SETTLE_MS 20

typedef enum
{
    DUMP_IDLE,
    DUMP_RUNNING,
    DUMP_DONE,
    DUMP_ERROR,
} dump_state_t;

typedef struct
{
    const char * stage;
    uint8_t pass;
    uint32_t baud;
    int16_t core_voltage_mv;
    float frequency_mhz;
    uint8_t frame_length; // 9 or 11 when the register 0x00 answer is made of valid frames, else 0
    uint8_t chip_count;
    uint16_t chip_id;
    uint8_t length[REGISTER_COUNT];
    uint8_t (*rx)[RX_MAX];
} scan_t;

typedef struct
{
    uint32_t time_ms;
    uint32_t baud;
    uint8_t scan; // number of scans done when the frame was sent
    uint8_t length;
    uint8_t data[TX_DATA_MAX];
} tx_t;

typedef struct
{
    httpd_req_t * req;
    char buf[2048];
    size_t len;
    esp_err_t err;
} out_t;

static const char * TAG = "register_dump";

static volatile dump_state_t state = DUMP_IDLE;
static const char * dump_error = "";
static const char * init_status = "";
static uint8_t init_chip_count;
static scan_t scans[MAX_SCANS];
static int scan_count;
static tx_t * tx_log;
static int tx_count;
static int tx_dropped;
static bool scanning;
static TickType_t start_tick;

// Keeps every frame sent to the chips by the stock driver
static void log_tx(const uint8_t * data, int len)
{
    if (scanning || len <= 0) {
        return;
    }
    if (tx_log == NULL || tx_count >= TX_LOG_MAX || len > TX_DATA_MAX) {
        tx_dropped++;
        return;
    }
    tx_t * tx = &tx_log[tx_count];
    tx->time_ms = pdTICKS_TO_MS(xTaskGetTickCount() - start_tick);
    uart_get_baudrate(UART_NUM_1, &tx->baud);
    tx->scan = scan_count;
    tx->length = len;
    memcpy(tx->data, data, len);
    tx_count++;
}

// Read one register on all chips, returns the raw bytes received until the line goes idle
static uint8_t read_register(uint8_t reg, uint8_t * rx, uint16_t expected)
{
    uint8_t cmd[7] = {0x55, 0xAA, 0x52, 0x05, 0x00, reg, 0x00};
    cmd[6] = crc5(cmd + 2, 4);

    SERIAL_clear_buffer();
    SERIAL_send(cmd, sizeof(cmd), false);
    uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(100));

    int received = SERIAL_rx(rx, expected, RX_FIRST_TIMEOUT_MS);
    if (received < 0) {
        received = 0;
    }
    while (received < RX_MAX) {
        int more = SERIAL_rx(rx + received, RX_MAX - received, RX_GAP_MS);
        if (more <= 0) {
            break;
        }
        received += more;
    }
    return received;
}

static uint8_t get_frame_length(const uint8_t * rx, uint8_t received)
{
    static const uint8_t lengths[] = {9, 11};

    for (size_t i = 0; i < sizeof(lengths); i++) {
        uint8_t length = lengths[i];
        if (received < length || received % length != 0) {
            continue;
        }
        bool valid = true;
        for (uint8_t offset = 0; offset < received; offset += length) {
            if (rx[offset] != 0xAA || rx[offset + 1] != 0x55 || crc5((uint8_t *) rx + offset + 2, length - 2) != 0) {
                valid = false;
                break;
            }
        }
        if (valid) {
            return length;
        }
    }
    return 0;
}

static bool scan_registers(GlobalState * GLOBAL_STATE, const char * stage, uint8_t pass)
{
    if (scan_count >= MAX_SCANS) {
        return false;
    }
    scan_t * scan = &scans[scan_count];
    memset(scan, 0, sizeof(*scan));

    scan->rx = heap_caps_calloc(REGISTER_COUNT, RX_MAX, GLOBAL_STATE->psram_is_available ? MALLOC_CAP_SPIRAM : MALLOC_CAP_DEFAULT);
    if (scan->rx == NULL) {
        dump_error = "out of memory";
        return false;
    }

    scan->stage = stage;
    scan->pass = pass;
    uart_get_baudrate(UART_NUM_1, &scan->baud);
    scan->core_voltage_mv = VCORE_get_voltage_mv(GLOBAL_STATE);
    scan->frequency_mhz = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency;

    scanning = true;
    uint16_t expected = RX_MAX;
    int answered = 0;
    for (int reg = 0; reg < REGISTER_COUNT; reg++) {
        scan->length[reg] = read_register(reg, scan->rx[reg], expected);
        if (reg == 0) {
            scan->frame_length = get_frame_length(scan->rx[0], scan->length[0]);
            if (scan->frame_length != 0) {
                scan->chip_count = scan->length[0] / scan->frame_length;
                scan->chip_id = (scan->rx[0][2] << 8) | scan->rx[0][3];
                expected = scan->length[0];
            }
        }
        if (scan->length[reg] > 0) {
            answered++;
        }
    }
    scanning = false;
    scan_count++;

    ESP_LOGI(TAG, "%s pass %u: %lu baud, %u chip(s), chip id %04x, %u byte frames, %d/%d addresses answered", stage, pass,
             (unsigned long) scan->baud, scan->chip_count, scan->chip_id, scan->frame_length, answered, REGISTER_COUNT);
    return true;
}

void register_dump_run(GlobalState * GLOBAL_STATE)
{
    state = DUMP_RUNNING;
    ESP_LOGI(TAG, "Dumping ASIC registers instead of mining");

    start_tick = xTaskGetTickCount();
    tx_log = heap_caps_calloc(TX_LOG_MAX, sizeof(tx_t), GLOBAL_STATE->psram_is_available ? MALLOC_CAP_SPIRAM : MALLOC_CAP_DEFAULT);
    SERIAL_set_tx_hook(log_tx);

    // Reset values, read before anything is written to the chips
    if (asic_reset(RESET_LOW_MS, RESET_RELEASE_MS) != ESP_OK) {
        dump_error = "ASIC reset failed";
        SERIAL_set_tx_hook(NULL);
        state = DUMP_ERROR;
        return;
    }
    if (!SERIAL_is_initialized()) {
        SERIAL_init();
    } else {
        SERIAL_set_baud(UART_FREQ);
    }
    vTaskDelay(pdMS_TO_TICKS(UART_SETTLE_MS));

    bool ok = true;
    for (uint8_t pass = 0; ok && pass < PASSES; pass++) {
        ok = scan_registers(GLOBAL_STATE, "reset", pass);
    }

    // Values once the stock driver has initialized the chain
    if (ok) {
        init_chip_count = asic_initialize(GLOBAL_STATE, ASIC_INIT_RECOVERY, 0);
        if (init_chip_count > 0) {
            init_status = "ok";
            for (uint8_t pass = 0; ok && pass < PASSES; pass++) {
                ok = scan_registers(GLOBAL_STATE, "init", pass);
            }
        } else {
            init_status = GLOBAL_STATE->SYSTEM_MODULE.asic_status != NULL ? GLOBAL_STATE->SYSTEM_MODULE.asic_status : "failed";
        }
    }

    GLOBAL_STATE->ASIC_initalized = false;
    asic_hold_reset_low();
    SERIAL_set_tx_hook(NULL);

    state = ok ? DUMP_DONE : DUMP_ERROR;
    ESP_LOGI(TAG, "Register dump %s, get it at /api/system/asic/registers", ok ? "done" : "failed");
}

static void out_flush(out_t * out)
{
    if (out->err == ESP_OK && out->len > 0) {
        out->err = httpd_resp_send_chunk(out->req, out->buf, out->len);
    }
    out->len = 0;
}

static void out_printf(out_t * out, const char * format, ...)
{
    if (out->len > sizeof(out->buf) / 2) {
        out_flush(out);
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(out->buf + out->len, sizeof(out->buf) - out->len, format, args);
    va_end(args);
    if (written > 0) {
        out->len += (size_t) written < sizeof(out->buf) - out->len ? (size_t) written : sizeof(out->buf) - out->len - 1;
    }
}

static void out_hex(out_t * out, const uint8_t * data, uint8_t length)
{
    static const char digits[] = "0123456789abcdef";

    if (out->len + 2 * RX_MAX + 4 > sizeof(out->buf)) {
        out_flush(out);
    }
    out->buf[out->len++] = '"';
    for (uint8_t i = 0; i < length; i++) {
        out->buf[out->len++] = digits[data[i] >> 4];
        out->buf[out->len++] = digits[data[i] & 0x0F];
    }
    out->buf[out->len++] = '"';
}

esp_err_t GET_system_asic_registers(httpd_req_t * req)
{
    static const char * const state_names[] = {"idle", "running", "done", "error"};

    GlobalState * GLOBAL_STATE = (GlobalState *) httpd_get_global_user_ctx(req->handle);
    dump_state_t current = state;

    out_t * out = calloc(1, sizeof(out_t));
    if (out == NULL) {
        return httpd_resp_send_500(req);
    }
    out->req = req;

    httpd_resp_set_type(req, "application/json");
    if (current == DUMP_DONE) {
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"bitaxe-register-dump.json\"");
    }

    out_printf(out, "{\"format\":1,\"status\":\"%s\",\"error\":\"%s\",\"firmware\":\"%s\"", state_names[current], dump_error,
               esp_app_get_description()->version);
    out_printf(out, ",\"boardVersion\":\"%s\",\"family\":\"%s\",\"asicModel\":\"%s\",\"asicCount\":%u,\"voltageDomains\":%u",
               GLOBAL_STATE->DEVICE_CONFIG.board_version, GLOBAL_STATE->DEVICE_CONFIG.family.name,
               GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic_count,
               GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains);
    out_printf(out, ",\"coreVoltage\":%u,\"frequency\":%g,\"initStatus\":\"%s\",\"initChipCount\":%u,\"scans\":[",
               nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE), nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY), init_status,
               init_chip_count);

    // "rx" holds, for each register address from 0x00 to 0xFF, the raw bytes answered by the chain ("" on timeout)
    for (int i = 0; current != DUMP_RUNNING && i < scan_count; i++) {
        const scan_t * scan = &scans[i];
        out_printf(out, "%s{\"stage\":\"%s\",\"pass\":%u,\"baud\":%lu,\"coreVoltageActual\":%d,\"frequencyActual\":%g", i ? "," : "",
                   scan->stage, scan->pass, (unsigned long) scan->baud, scan->core_voltage_mv, scan->frequency_mhz);
        out_printf(out, ",\"frameLength\":%u,\"chipCount\":%u,\"chipId\":\"%04x\",\"rx\":[", scan->frame_length, scan->chip_count,
                   scan->chip_id);
        for (int reg = 0; reg < REGISTER_COUNT; reg++) {
            if (reg > 0) {
                out_printf(out, ",");
            }
            out_hex(out, scan->rx[reg], scan->length[reg]);
        }
        out_printf(out, "]}");
    }

    // "tx" holds every frame sent to the chips besides the reads of the scans, "scan" is the number of scans done before it
    out_printf(out, "],\"txDropped\":%d,\"tx\":[", current != DUMP_RUNNING ? tx_dropped : 0);
    for (int i = 0; current != DUMP_RUNNING && i < tx_count; i++) {
        const tx_t * tx = &tx_log[i];
        out_printf(out, "%s{\"time\":%lu,\"scan\":%u,\"baud\":%lu,\"data\":", i ? "," : "", (unsigned long) tx->time_ms, tx->scan,
                   (unsigned long) tx->baud);
        out_hex(out, tx->data, tx->length);
        out_printf(out, "}");
    }
    out_printf(out, "]}");
    out_flush(out);

    esp_err_t res = out->err;
    free(out);
    if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, NULL, 0);
    }
    return res;
}
