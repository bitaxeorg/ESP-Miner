#include "bm1373.h"

#include "crc.h"
#include "global_state.h"
#include "mining.h"
#include "serial.h"
#include "utils.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_transition_bmXX.h"
#include "pll.h"

#include <arpa/inet.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BM1372_CHIP_ID 0x1372
#define BM1373_CHIP_ID_ALIAS 0x1373
#define BM1372_CHIP_ID_RESPONSE_LENGTH 9

#define BM1372_REGISTER_PLL0_PARAMETER 0x08
#define BM1372_REGISTER_HASH_COUNTING_NUMBER 0x10
#define BM1372_REGISTER_TICKET_MASK 0x14
#define BM1372_REGISTER_MISC_CONTROL 0x18
#define BM1372_REGISTER_MISC_CONTROL_ADD 0x19
#define BM1372_REGISTER_FAST_UART_CONFIGURATION 0x28
#define BM1372_REGISTER_CORE_COMMAND 0x3C
#define BM1372_REGISTER_ANALOG_MUX_CONTROL 0x54
#define BM1372_REGISTER_IO_DRIVER_STRENGTH 0x58
#define BM1372_REGISTER_ROSC_PAD_DISABLE 0x68
#define BM1372_REGISTER_SOFT_RESET_CONTROL 0xA8
#define BM1372_REGISTER_VERSION_ROLLING 0xA4
#define BM1372_REGISTER_AUTO_WORK_CONFIGURATION 0xC4

// The generic BM13xx nonce-space calculation makes BM1372/BM1373 chips search
// overlapping work. This is the known-good S21 Pro stock register value.
#define BM1372_HASH_COUNTING_NUMBER_S21_PRO 0x00001EB5

#define BM1372_CORE_REGISTER_CLOCK_DELAY_CTRL 0x00
#define BM1372_CORE_REGISTER_CORE_ENABLE 0x02
#define BM1372_CORE_REGISTER_CLOCK_SELECT_CTRL 0x0B
#define BM1372_CORE_REGISTER_NONCE_BIN_OVERFLOW_CTRL 0x0D

#define BM1372_SOFT_RESET_FAST 0xF00701F0
#define BM1372_RNO_ENABLE 0xFF00C100
#define BM1372_CRR_DISABLE 0x00000000
#define BM1372_CLOCK_SELECT 0x00
#define BM1372_CLOCK_DELAY_PWTH6_CCDLY2 0x5A
#define BM1372_CORE_ENABLE 0xAA
#define BM1372_NONCE_BIN_OVERFLOW_DISABLE 0x04
#define BM1372_IO_DRIVER_STRENGTH_DEFAULT 0x00011111
#define BM1372_IO_DRIVER_STRENGTH_DOMAIN_END 0x0001F111
#define BM1372_ROSC_PAD_DISABLE 0x5AA55AA5
#define BM1372_ANALOG_MUX_TEMPERATURE_DIODE 0x00000002
#define BM1372_AUTO_WORK_CONFIGURATION 0x00000000
#define BM1372_FAST_UART_3M 0x80000000

#define BM1372_ASIC_BAUD 3000000
#define BM1372_WRITE_RETRIES 3
#define BM1372_WRITE_RETRY_DELAY_MS 50
#define BM1372_INIT_STEP_DELAY_MS 10
#define BM1372_FAST_RESET_DELAY_MS 20
#define BM1372_SET_ADDRESS_STRIDE 0x10
#define BM1372_COMMAND_ADDRESS_SHIFT 2

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

static const register_type_t REGISTER_MAP[] = {
    [0x4C] = REGISTER_ERROR_COUNT,
    [0x88] = REGISTER_DOMAIN_0_COUNT,
    [0x89] = REGISTER_DOMAIN_1_COUNT,
    [0x8A] = REGISTER_DOMAIN_2_COUNT,
    [0x8B] = REGISTER_DOMAIN_3_COUNT,
    [0x8C] = REGISTER_TOTAL_COUNT
};

typedef struct __attribute__((__packed__))
{
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t id;
    uint16_t version;
} bm1373_asic_result_job_t;

typedef struct __attribute__((__packed__))
{
    uint32_t value;
    uint8_t asic_address;
    uint8_t register_address;
    uint16_t : 16;
} bm1373_asic_result_cmd_t;

typedef struct __attribute__((__packed__))
{
    uint16_t preamble;
    union {
        bm1373_asic_result_job_t job;
        bm1373_asic_result_cmd_t cmd;
    };
    uint8_t crc             : 5;
    uint8_t                 : 2;
    uint8_t is_job_response : 1;
} bm1373_asic_result_t;

static const char * TAG = "bm1372/73";

static task_result result;

static uint8_t chip_command_address_interval;
static uint8_t chip_response_address_interval;
static uint8_t chip_nonce_address_interval;
static uint8_t detected_chip_count;
static uint8_t detected_voltage_domains;
static bool frequency_write_failed;

/// @brief
/// @param ftdi
/// @param header
/// @param data
/// @param len
static bool _send_BM1373(uint8_t header, const uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);
    uint8_t buf[total_length];

    // add the preamble
    buf[0] = 0x55;
    buf[1] = 0xAA;

    // add the header field
    buf[2] = header;

    // add the length field
    buf[3] = (packet_type == JOB_PACKET) ? (data_len + 4) : (data_len + 3);

    // add the data
    memcpy(buf + 4, data, data_len);

    // add the correct crc type
    if (packet_type == JOB_PACKET) {
        uint16_t crc16_total = crc16_false(buf + 2, data_len + 2);
        buf[4 + data_len] = (crc16_total >> 8) & 0xFF;
        buf[5 + data_len] = crc16_total & 0xFF;
    } else {
        buf[4 + data_len] = crc5(buf + 2, data_len + 2);
    }

    for (uint8_t attempt = 1; attempt <= BM1372_WRITE_RETRIES; attempt++) {
        int bytes_written = SERIAL_send(buf, total_length, debug);
        if (bytes_written == total_length) {
            return true;
        }

        ESP_LOGW(TAG, "ASIC write failed (%d/%u bytes), attempt %u/%u",
                 bytes_written, total_length, attempt, BM1372_WRITE_RETRIES);
        if (attempt < BM1372_WRITE_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(BM1372_WRITE_RETRY_DELAY_MS));
        }
    }

    ESP_LOGE(TAG, "ASIC write failed after %u attempts", BM1372_WRITE_RETRIES);
    return false;
}

static bool _write_register(uint8_t group, uint8_t chip_address, uint8_t register_address, uint32_t value)
{
    uint8_t command[6] = {
        chip_address,
        register_address,
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };

    return _send_BM1373(TYPE_CMD | group | CMD_WRITE, command, sizeof(command), BM1373_SERIALTX_DEBUG);
}

static bool _write_broadcast(uint8_t register_address, uint32_t value)
{
    return _write_register(GROUP_ALL, 0, register_address, value);
}

static bool _write_chip(uint8_t chip_address, uint8_t register_address, uint32_t value)
{
    return _write_register(GROUP_SINGLE, chip_address, register_address, value);
}

static bool _write_core_register(uint8_t core_register, uint8_t value)
{
    uint32_t command = 0x80008000 | ((uint32_t)core_register << 8) | value;
    return _write_broadcast(BM1372_REGISTER_CORE_COMMAND, command);
}

static bool _send_chain_inactive(void)
{
    const uint8_t command[2] = {0x00, 0x00};
    return _send_BM1373(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, command, sizeof(command), BM1373_SERIALTX_DEBUG);
}

static bool _set_chip_address(uint8_t chip_address)
{
    const uint8_t command[2] = {chip_address, 0x00};
    return _send_BM1373(TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS, command, sizeof(command), BM1373_SERIALTX_DEBUG);
}

void BM1373_set_version_mask(uint32_t version_mask)
{
    uint32_t versions_to_roll = version_mask >> 13;
    uint32_t value = 0x90000000 | (versions_to_roll & 0xFFFF);
    if (!_write_broadcast(BM1372_REGISTER_VERSION_ROLLING, value)) {
        ESP_LOGE(TAG, "Failed to set version mask");
    }
}

void BM1373_set_hash_counting_number(uint32_t hcn)
{
    if (!_write_broadcast(BM1372_REGISTER_HASH_COUNTING_NUMBER, hcn)) {
        ESP_LOGE(TAG, "Failed to set hash counting number");
    }
}

void BM1373_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores)
{
    (void)nonce_percent;
    (void)frequency;
    (void)asic_count;
    (void)cores;

    BM1373_set_hash_counting_number(BM1372_HASH_COUNTING_NUMBER_S21_PRO);
}

float BM1373_send_hash_frequency(float target_freq)
{
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float frequency;

    pll_get_parameters(target_freq, 160, 239, &fb_divider, &refdiv, &postdiv1, &postdiv2, &frequency);

    uint8_t vdo_scale = (fb_divider * FREQ_MULT / refdiv >= 2400) ? 0x50 : 0x40;
    uint8_t postdiv = (((postdiv1 - 1) & 0xf) << 4) | ((postdiv2 - 1) & 0xf);
    uint32_t pll_value = ((uint32_t)vdo_scale << 24) |
                         ((uint32_t)fb_divider << 16) |
                         ((uint32_t)refdiv << 8) |
                         postdiv;

    // Every chip in the Bitaxe chain runs at the same target frequency. A
    // broadcast also avoids the BM1372's distinct assigned/command address
    // encodings during the frequency ramp.
    bool success = _write_broadcast(BM1372_REGISTER_PLL0_PARAMETER, pll_value);

    if (!success) {
        frequency_write_failed = true;
        ESP_LOGE(TAG, "Failed to program one or more ASIC PLLs");
    }

    ESP_LOGI(TAG, "Setting Frequency to %g MHz (%g)", target_freq, frequency);

    return frequency;
}

uint8_t BM1373_init(GlobalState * GLOBAL_STATE)
{
    uint8_t expected_chip_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;

    chip_command_address_interval = 0;
    chip_response_address_interval = 0;
    chip_nonce_address_interval = 0;
    detected_chip_count = 0;
    detected_voltage_domains = 0;
    frequency_write_failed = false;

    // Discover the chain at reset baud before changing any ASIC configuration.
    const uint8_t chip_id_request[2] = {0x00, 0x00};
    if (!_send_BM1373(TYPE_CMD | GROUP_ALL | CMD_READ, chip_id_request,
                      sizeof(chip_id_request), BM1373_SERIALTX_DEBUG)) {
        return 0;
    }

    int chip_counter = count_asic_chips_with_id_alias(expected_chip_count,
                                                      BM1372_CHIP_ID,
                                                      BM1373_CHIP_ID_ALIAS,
                                                      BM1372_CHIP_ID_RESPONSE_LENGTH);
    if (chip_counter != expected_chip_count) {
        ESP_LOGE(TAG, "Expected %u BM1372/BM1373 ASICs, detected %d",
                 expected_chip_count, chip_counter);
        return 0;
    }

    detected_chip_count = chip_counter;
    detected_voltage_domains = GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains;
    chip_command_address_interval = BM1372_SET_ADDRESS_STRIDE >> BM1372_COMMAND_ADDRESS_SHIFT;
    chip_response_address_interval = BM1372_SET_ADDRESS_STRIDE;
    chip_nonce_address_interval = 256 / detected_chip_count;
    if (chip_nonce_address_interval == 0) {
        ESP_LOGE(TAG, "Invalid nonce address interval for %u ASICs", detected_chip_count);
        return 0;
    }

    if (!_send_chain_inactive()) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    for (uint8_t chip = 0; chip < detected_chip_count; chip++) {
        if (!_set_chip_address(chip * BM1372_SET_ADDRESS_STRIDE)) {
            return 0;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    // Match the stock BM1372 setup order: nonce overflow, ticket mask, IO,
    // ring-oscillator pads, then UART and the staged core initialization.
    if (!_write_core_register(BM1372_CORE_REGISTER_NONCE_BIN_OVERFLOW_CTRL,
                              BM1372_NONCE_BIN_OVERFLOW_DISABLE)) {
        return 0;
    }

    uint8_t difficulty_mask[6];
    get_difficulty_mask(GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty, difficulty_mask);
    if (!_send_BM1373(TYPE_CMD | GROUP_ALL | CMD_WRITE, difficulty_mask,
                      sizeof(difficulty_mask), BM1373_SERIALTX_DEBUG)) {
        return 0;
    }

    if (!_write_broadcast(BM1372_REGISTER_IO_DRIVER_STRENGTH,
                          BM1372_IO_DRIVER_STRENGTH_DEFAULT)) {
        return 0;
    }

    uint8_t domains = detected_voltage_domains == 0 ? 1 : detected_voltage_domains;
    for (uint8_t domain = 0; domain < domains; domain++) {
        uint8_t domain_first_chip = (domain * detected_chip_count) / domains;
        uint8_t domain_end_exclusive = ((domain + 1) * detected_chip_count) / domains;
        if (domain_end_exclusive == domain_first_chip) {
            continue;
        }
        uint8_t domain_end_chip = domain_end_exclusive - 1;
        if (!_write_chip(domain_end_chip * chip_command_address_interval,
                         BM1372_REGISTER_IO_DRIVER_STRENGTH,
                         BM1372_IO_DRIVER_STRENGTH_DOMAIN_END)) {
            return 0;
        }
    }

    if (!_write_broadcast(BM1372_REGISTER_ROSC_PAD_DISABLE, BM1372_ROSC_PAD_DISABLE)) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    int asic_baud = BM1373_set_max_baud();
    if (asic_baud == 0 || SERIAL_set_baud(asic_baud) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch BM1372/BM1373 UART to %d baud", BM1372_ASIC_BAUD);
        return 0;
    }
    SERIAL_clear_buffer();
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    if (!_write_broadcast(BM1372_REGISTER_SOFT_RESET_CONTROL, BM1372_SOFT_RESET_FAST)) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_FAST_RESET_DELAY_MS));

    if (!_write_broadcast(BM1372_REGISTER_MISC_CONTROL, BM1372_RNO_ENABLE)) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    if (!_write_broadcast(BM1372_REGISTER_MISC_CONTROL_ADD, BM1372_CRR_DISABLE)) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    if (!_write_core_register(BM1372_CORE_REGISTER_CLOCK_SELECT_CTRL, BM1372_CLOCK_SELECT)) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    if (!_write_core_register(BM1372_CORE_REGISTER_CLOCK_DELAY_CTRL,
                              BM1372_CLOCK_DELAY_PWTH6_CCDLY2)) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    if (!_write_core_register(BM1372_CORE_REGISTER_CORE_ENABLE, BM1372_CORE_ENABLE)) {
        return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(BM1372_INIT_STEP_DELAY_MS));

    if (!_write_broadcast(BM1372_REGISTER_ANALOG_MUX_CONTROL,
                          BM1372_ANALOG_MUX_TEMPERATURE_DIODE)) {
        return 0;
    }

    // A hardware reset restores the PLL baseline even during a live recovery.
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = 50.0f;
    do_frequency_transition(GLOBAL_STATE, BM1373_send_hash_frequency);
    if (frequency_write_failed) {
        return 0;
    }

    if (!_write_broadcast(BM1372_REGISTER_HASH_COUNTING_NUMBER,
                          BM1372_HASH_COUNTING_NUMBER_S21_PRO)) {
        ESP_LOGE(TAG, "Failed to configure nonce space");
        return 0;
    }

    uint32_t versions_to_roll = STRATUM_DEFAULT_VERSION_MASK >> 13;
    if (!_write_broadcast(BM1372_REGISTER_VERSION_ROLLING,
                          0x90000000 | (versions_to_roll & 0xFFFF))) {
        ESP_LOGE(TAG, "Failed to configure version rolling");
        return 0;
    }

    return detected_chip_count;
}

int BM1373_set_default_baud(void)
{
    // BM1372/BM1373 returns to 115200 only through a hardware reset.
    return UART_FREQ;
}

int BM1373_set_max_baud(void)
{
    ESP_LOGI(TAG, "Setting ASIC UART to %d baud", BM1372_ASIC_BAUD);

    if (!_write_broadcast(BM1372_REGISTER_AUTO_WORK_CONFIGURATION,
                          BM1372_AUTO_WORK_CONFIGURATION) ||
        !_write_broadcast(BM1372_REGISTER_FAST_UART_CONFIGURATION,
                          BM1372_FAST_UART_3M)) {
        return 0;
    }

    return BM1372_ASIC_BAUD;
}

static uint8_t id = 0;

void BM1373_send_work(GlobalState * GLOBAL_STATE, bm_job * next_bm_job)
{
    BM1373_job job;
    id = (id + 24) % 128;
    job.job_id = id;
    job.num_midstates = 0x01;
    memcpy(&job.starting_nonce, &next_bm_job->starting_nonce, 4);
    memcpy(&job.nbits, &next_bm_job->target, 4);
    memcpy(&job.ntime, &next_bm_job->ntime, 4);
    memcpy(job.merkle_root, next_bm_job->merkle_root, 32);
    memcpy(job.prev_block_hash, next_bm_job->prev_block_hash, 32);
    memcpy(&job.version, &next_bm_job->version, 4);

    if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] != NULL) {
        free_bm_job(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id]);
    }

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] = next_bm_job;

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    GLOBAL_STATE->valid_jobs[job.job_id] = 1;
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    //debug sent jobs - this can get crazy if the interval is short
    #if BM1373_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: %02X", job.job_id);
    #endif

    _send_BM1373((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t *)&job, sizeof(BM1373_job), BM1373_DEBUG_WORK);
}

task_result * BM1373_process_work(GlobalState * GLOBAL_STATE)
{
    bm1373_asic_result_t asic_result = {0};

    memset(&result, 0, sizeof(task_result));

    if (receive_work((uint8_t *)&asic_result, sizeof(asic_result), &result.timestamp_us) == ESP_FAIL) {
        return NULL;
    }

    if (!asic_result.is_job_response) {
        result.register_type = REGISTER_MAP[asic_result.cmd.register_address];
        if (result.register_type == REGISTER_INVALID) {
            ESP_LOGW(TAG, "Unknown register read: %02x", asic_result.cmd.register_address);
            return NULL;
        }
        result.asic_nr = asic_result.cmd.asic_address / chip_response_address_interval;
        result.value = ntohl(asic_result.cmd.value);

        return &result;
    }

    // uint8_t job_id = asic_result.job_id;
    // uint8_t rx_job_id = ((int8_t)job_id & 0xf0) >> 1;
    // ESP_LOGI(TAG, "Job ID: %02X, RX: %02X", job_id, rx_job_id);

    // uint8_t job_id = asic_result.job_id & 0xf8;
    // ESP_LOGI(TAG, "Job ID: %02X, Core: %01X", job_id, asic_result.job_id & 0x07);

    uint8_t job_id = (asic_result.job.id & 0xf0) >> 1;
    uint32_t nonce_h = ntohl(asic_result.job.nonce);
    uint8_t asic_nr = (uint8_t)((nonce_h >> 17) & 0xff) / chip_nonce_address_interval;
    uint8_t core_id = (uint8_t)((nonce_h >> 25) & 0x7f);
    uint8_t small_core_id = asic_result.job.id & 0x0f;
    uint32_t version_bits = (ntohs(asic_result.job.version) << 13);

    if (GLOBAL_STATE->valid_jobs[job_id] == 0) {
        ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version | version_bits;

    result.job_id = job_id;
    result.nonce = asic_result.job.nonce;
    result.rolled_version = rolled_version;
    result.asic_nr = asic_nr;
    result.core_id = core_id;
    result.small_core_id = small_core_id;

    return &result;
}

void BM1373_read_registers(void)
{
    int size = sizeof(REGISTER_MAP) / sizeof(REGISTER_MAP[0]);
    for (int reg = 0; reg < size; reg++) {
        if (REGISTER_MAP[reg] != REGISTER_INVALID) {
            _send_BM1373((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, reg}, 2, BM1373_SERIALTX_DEBUG);
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }
}
