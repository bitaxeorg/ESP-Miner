#include "bm1368.h"
#include "crc.h"
#include "global_state.h"
#include "serial.h"
#include "utils.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BM1368_RST_PIN GPIO_NUM_1

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_JOB 0x01

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define RESPONSE_CMD 0x00
#define RESPONSE_JOB 0x80

#define SLEEP_TIME 20
#define FREQ_MULT 25.0

#define CLOCK_ORDER_CONTROL_0 0x80
#define CLOCK_ORDER_CONTROL_1 0x84
#define ORDERED_CLOCK_ENABLE 0x20
#define CORE_REGISTER_CONTROL 0x3C
#define PLL3_PARAMETER 0x68
#define FAST_UART_CONFIGURATION 0x28
#define TICKET_MASK 0x14
#define MISC_CONTROL 0x18

typedef struct __attribute__((__packed__))
{
    uint8_t preamble[2];
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t job_id;
    uint16_t version;
    uint8_t crc;
} asic_result;

static const char * TAG = "bm1368Module";

static uint8_t asic_response_buffer[CHUNK_SIZE];
static task_result result;

static float current_frequency = 56.25;

static void _send_BM1368(uint8_t header, uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

    unsigned char * buf = malloc(total_length);

    buf[0] = 0x55;
    buf[1] = 0xAA;
    buf[2] = header;
    buf[3] = (packet_type == JOB_PACKET) ? (data_len + 4) : (data_len + 3);
    memcpy(buf + 4, data, data_len);

    if (packet_type == JOB_PACKET) {
        uint16_t crc16_total = crc16_false(buf + 2, data_len + 2);
        buf[4 + data_len] = (crc16_total >> 8) & 0xFF;
        buf[5 + data_len] = crc16_total & 0xFF;
    } else {
        buf[4 + data_len] = crc5(buf + 2, data_len + 2);
    }

    SERIAL_send(buf, total_length, debug);

    free(buf);
}

static void _send_simple(uint8_t * data, uint8_t total_length)
{
    unsigned char * buf = malloc(total_length);
    memcpy(buf, data, total_length);
    SERIAL_send(buf, total_length, BM1368_SERIALTX_DEBUG);

    free(buf);
}

static void _send_chain_inactive(void)
{
    unsigned char read_address[2] = {0x00, 0x00};
    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2, BM1368_SERIALTX_DEBUG);
}

static void _set_chip_address(uint8_t chipAddr)
{
    unsigned char read_address[2] = {chipAddr, 0x00};
    _send_BM1368((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2, BM1368_SERIALTX_DEBUG);
}

static void _reset(void)
{
    gpio_set_level(BM1368_RST_PIN, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(BM1368_RST_PIN, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

bool BM1368_send_hash_frequency(float target_freq) {
    float max_diff = 0.001;
    uint8_t freqbuf[6] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x41};
    uint8_t postdiv_min = 255;
    uint8_t postdiv2_min = 255;
    float best_freq = 0;
    uint8_t best_refdiv = 0, best_fbdiv = 0, best_postdiv1 = 0, best_postdiv2 = 0;
    bool found = false;

    for (uint8_t refdiv = 2; refdiv > 0; refdiv--) {
        for (uint8_t postdiv1 = 7; postdiv1 > 0; postdiv1--) {
            for (uint8_t postdiv2 = 7; postdiv2 > 0; postdiv2--) {
                uint16_t fb_divider = round(target_freq / 25.0 * (refdiv * postdiv2 * postdiv1));
                float newf = 25.0 * fb_divider / (refdiv * postdiv2 * postdiv1);

                if (fb_divider >= 144 && fb_divider <= 235 &&
                    fabs(target_freq - newf) < max_diff &&
                    postdiv1 >= postdiv2 &&
                    postdiv1 * postdiv2 < postdiv_min &&
                    postdiv2 <= postdiv2_min) {

                    postdiv2_min = postdiv2;
                    postdiv_min = postdiv1 * postdiv2;
                    best_freq = newf;
                    best_refdiv = refdiv;
                    best_fbdiv = fb_divider;
                    best_postdiv1 = postdiv1;
                    best_postdiv2 = postdiv2;
                    found = true;
                }
            }
        }
    }

    if (!found) {
        ESP_LOGE(TAG, "Didn't find PLL settings for target frequency %.2f", target_freq);
        return false;
    }

    freqbuf[2] = (best_fbdiv * 25 / best_refdiv >= 2400) ? 0x50 : 0x40;
    freqbuf[3] = best_fbdiv;
    freqbuf[4] = best_refdiv;
    freqbuf[5] = (((best_postdiv1 - 1) & 0xf) << 4) | ((best_postdiv2 - 1) & 0xf);

    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, sizeof(freqbuf), BM1368_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Setting Frequency to %.2fMHz (%.2f)", target_freq, best_freq);
    current_frequency = target_freq;
    return true;
}

bool do_frequency_transition(float target_frequency) {
    float step = 6.25;
    float current = current_frequency;
    float target = target_frequency;

    float direction = (target > current) ? step : -step;

    if (fmod(current, step) != 0) {
        float next_dividable;
        if (direction > 0) {
            next_dividable = ceil(current / step) * step;
        } else {
            next_dividable = floor(current / step) * step;
        }
        current = next_dividable;
        BM1368_send_hash_frequency(current);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    while ((direction > 0 && current < target) || (direction < 0 && current > target)) {
        float next_step = fmin(fabs(direction), fabs(target - current));
        current += direction > 0 ? next_step : -next_step;
        BM1368_send_hash_frequency(current);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    BM1368_send_hash_frequency(target);
    return true;
}

bool BM1368_set_frequency(float target_freq) {
    return do_frequency_transition(target_freq);
}

static int count_asic_chips(void) {
    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_READ, (uint8_t[]){0x00, 0x00}, 2, false);

    int chip_counter = 0;
    while (true) {
        if (SERIAL_rx(asic_response_buffer, 11, 5000) <= 0) {
            break;
        }

        if (memcmp(asic_response_buffer, "\xaa\x55\x13\x68\x00\x00", 6) == 0) {
            chip_counter++;
        }
    }

    _send_chain_inactive();
    return chip_counter;
}

static void do_frequency_ramp_up(float target_frequency) {
    ESP_LOGI(TAG, "Ramping up frequency from %.2f MHz to %.2f MHz", current_frequency, target_frequency);
    do_frequency_transition(target_frequency);
}

uint8_t BM1368_init(uint64_t frequency, uint16_t asic_count)
{
    ESP_LOGI(TAG, "Initializing BM1368");

    memset(asic_response_buffer, 0, CHUNK_SIZE);

    esp_rom_gpio_pad_select_gpio(BM1368_RST_PIN);
    gpio_set_direction(BM1368_RST_PIN, GPIO_MODE_OUTPUT);

    _reset();

    uint8_t init_cmd[] = {0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF};
    for (int i = 0; i < 4; i++) {
        _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, init_cmd, 6, false);
    }

    int chip_counter = count_asic_chips();

    if (chip_counter != asic_count) {
        ESP_LOGE(TAG, "Chip count mismatch. Expected: %d, Actual: %d", asic_count, chip_counter);
        return 0;
    }

    uint8_t init_cmds[][6] = {
        {0x00, 0xA8, 0x00, 0x07, 0x00, 0x00},
        {0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00},
        {0x00, 0x3C, 0x80, 0x00, 0x8b, 0x00},
        {0x00, 0x3C, 0x80, 0x00, 0x80, 0x18},
        {0x00, 0x14, 0x00, 0x00, 0x00, 0xFF},
        {0x00, 0x54, 0x00, 0x00, 0x00, 0x03},
        {0x00, 0x58, 0x02, 0x11, 0x11, 0x11}
    };

    for (int i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, init_cmds[i], 6, false);
    }

    uint8_t address_interval = (uint8_t) (256 / chip_counter);
    for (int i = 0; i < chip_counter; i++) {
        _set_chip_address(i * address_interval);
    }

    for (int i = 0; i < chip_counter; i++) {
        uint8_t chip_init_cmds[][6] = {
            {i * address_interval, 0xA8, 0x00, 0x07, 0x01, 0xF0},
            {i * address_interval, 0x18, 0xF0, 0x00, 0xC1, 0x00},
            {i * address_interval, 0x3C, 0x80, 0x00, 0x8b, 0x00},
            {i * address_interval, 0x3C, 0x80, 0x00, 0x80, 0x18},
            {i * address_interval, 0x3C, 0x80, 0x00, 0x82, 0xAA}
        };

        for (int j = 0; j < sizeof(chip_init_cmds) / sizeof(chip_init_cmds[0]); j++) {
            _send_BM1368(TYPE_CMD | GROUP_SINGLE | CMD_WRITE, chip_init_cmds[j], 6, false);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    BM1368_set_job_difficulty_mask(BM1368_INITIAL_DIFFICULTY);

    do_frequency_ramp_up((float)frequency);

    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, (uint8_t[]){0x00, 0x10, 0x00, 0x00, 0x15, 0xa4}, 6, false);
    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, (uint8_t[]){0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF}, 6, false);

    ESP_LOGI(TAG, "%i chip(s) detected on the chain, expected %i", chip_counter, asic_count);
    return chip_counter;
}

int BM1368_set_default_baud(void)
{
    unsigned char baudrate[9] = {0x00, MISC_CONTROL, 0x00, 0x00, 0b01111010, 0b00110001};
    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_WRITE), baudrate, 6, BM1368_SERIALTX_DEBUG);
    return 115749;
}

int BM1368_set_max_baud(void)
{
    ESP_LOGI(TAG, "Setting max baud of 1000000");

    unsigned char init8[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x28, 0x11, 0x30, 0x02, 0x00, 0x03};
    _send_simple(init8, 11);
    return 1000000;
}

void BM1368_set_job_difficulty_mask(int difficulty)
{
    unsigned char job_difficulty_mask[9] = {0x00, TICKET_MASK, 0b00000000, 0b00000000, 0b00000000, 0b11111111};

    difficulty = _largest_power_of_two(difficulty) - 1;

    for (int i = 0; i < 4; i++) {
        char value = (difficulty >> (8 * i)) & 0xFF;
        job_difficulty_mask[5 - i] = _reverse_bits(value);
    }

    ESP_LOGI(TAG, "Setting job ASIC mask to %d", difficulty);

    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_WRITE), job_difficulty_mask, 6, BM1368_SERIALTX_DEBUG);
}

static uint8_t id = 0;

void BM1368_send_work(void * pvParameters, bm_job * next_bm_job)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    BM1368_job job;
    id = (id + 24) % 128;
    job.job_id = id;
    job.num_midstates = 0x01;
    memcpy(&job.starting_nonce, &next_bm_job->starting_nonce, 4);
    memcpy(&job.nbits, &next_bm_job->target, 4);
    memcpy(&job.ntime, &next_bm_job->ntime, 4);
    memcpy(job.merkle_root, next_bm_job->merkle_root_be, 32);
    memcpy(job.prev_block_hash, next_bm_job->prev_block_hash_be, 32);
    memcpy(&job.version, &next_bm_job->version, 4);

    if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] != NULL) {
        free_bm_job(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id]);
    }

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] = next_bm_job;

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    GLOBAL_STATE->valid_jobs[job.job_id] = 1;

    #if BM1368_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: %02X", job.job_id);
    #endif
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    _send_BM1368((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), &job, sizeof(BM1368_job), BM1368_DEBUG_WORK);
}

asic_result * BM1368_receive_work(void)
{
    int received = SERIAL_rx(asic_response_buffer, 11, 60000);

    if (received < 0) {
        ESP_LOGI(TAG, "Error in serial RX");
        return NULL;
    } else if (received == 0) {
        return NULL;
    }

    if (received != 11 || asic_response_buffer[0] != 0xAA || asic_response_buffer[1] != 0x55) {
        ESP_LOGE(TAG, "Serial RX invalid %i", received);
        ESP_LOG_BUFFER_HEX(TAG, asic_response_buffer, received);
        SERIAL_clear_buffer();
        return NULL;
    }

    return (asic_result *) asic_response_buffer;
}

static uint16_t reverse_uint16(uint16_t num)
{
    return (num >> 8) | (num << 8);
}

static uint32_t reverse_uint32(uint32_t val)
{
    return ((val >> 24) & 0xff) |
           ((val << 8) & 0xff0000) |
           ((val >> 8) & 0xff00) |
           ((val << 24) & 0xff000000);
}

task_result * BM1368_proccess_work(void * pvParameters)
{
    asic_result * asic_result = BM1368_receive_work();

    if (asic_result == NULL) {
        return NULL;
    }

    uint8_t job_id = (asic_result->job_id & 0xf0) >> 1;
    uint8_t core_id = (uint8_t)((reverse_uint32(asic_result->nonce) >> 25) & 0x7f);
    uint8_t small_core_id = asic_result->job_id & 0x0f;
    uint32_t version_bits = (reverse_uint16(asic_result->version) << 13);
    ESP_LOGI(TAG, "Job ID: %02X, Core: %d/%d, Ver: %08" PRIX32, job_id, core_id, small_core_id, version_bits);

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    if (GLOBAL_STATE->valid_jobs[job_id] == 0) {
        ESP_LOGE(TAG, "Invalid job found, 0x%02X", job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version | version_bits;

    result.job_id = job_id;
    result.nonce = asic_result->nonce;
    result.rolled_version = rolled_version;

    return &result;
}