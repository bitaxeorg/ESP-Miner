#include "bm1368.h"

#include "crc.h"
#include "global_state.h"
#include "serial.h"
#include "utils.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_transition_bmXX.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define BM1368_CHIP_ID 0x1368
#define BM1368_CHIP_ID_RESPONSE_LENGTH 11

#ifdef CONFIG_GPIO_ASIC_RESET
#define GPIO_ASIC_RESET CONFIG_GPIO_ASIC_RESET
#else
#define GPIO_ASIC_RESET 1
#endif

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
    uint16_t preamble;
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t job_id;
    uint16_t version;
    uint8_t crc;
} bm1368_asic_result_t;

static const char * TAG = "bm1368Module";

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

void BM1368_set_version_mask(uint32_t version_mask) 
{
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF); 
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6, BM1368_SERIALTX_DEBUG);
}

static void _reset(void)
{
    gpio_set_level(GPIO_ASIC_RESET, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_ASIC_RESET, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

void BM1368_send_hash_frequency(float target_freq) {
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
        return;
    }

    freqbuf[2] = (best_fbdiv * 25 / best_refdiv >= 2400) ? 0x50 : 0x40;
    freqbuf[3] = best_fbdiv;
    freqbuf[4] = best_refdiv;
    freqbuf[5] = (((best_postdiv1 - 1) & 0xf) << 4) | ((best_postdiv2 - 1) & 0xf);

    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, sizeof(freqbuf), BM1368_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Setting Frequency to %.2fMHz (%.2f)", target_freq, best_freq);
    current_frequency = target_freq;
}

bool BM1368_set_frequency(float target_freq) {
    return do_frequency_transition(target_freq, BM1368_send_hash_frequency, 1368);
}

static void do_frequency_ramp_up(float target_frequency) {
    ESP_LOGI(TAG, "Ramping up frequency from %.2f MHz to %.2f MHz", current_frequency, target_frequency);
    do_frequency_transition(target_frequency, BM1368_send_hash_frequency, 1368);
}

uint8_t BM1368_init(uint64_t frequency, uint16_t asic_count)
{
    ESP_LOGI(TAG, "Initializing BM1368");

    esp_rom_gpio_pad_select_gpio(GPIO_ASIC_RESET);
    gpio_set_direction(GPIO_ASIC_RESET, GPIO_MODE_OUTPUT);

    _reset();

    // set version mask
    for (int i = 0; i < 4; i++) {
        BM1368_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);
    }

    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_READ, (uint8_t[]){0x00, 0x00}, 2, false);

    int chip_counter = count_asic_chips(asic_count, BM1368_CHIP_ID, BM1368_CHIP_ID_RESPONSE_LENGTH);

    if (chip_counter == 0) {
        return 0;
    }

    _send_chain_inactive();
    
    uint8_t init_cmds[][6] = {
        {0x00, 0xA8, 0x00, 0x07, 0x00, 0x00},
        {0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00},
        {0x00, 0x3C, 0x80, 0x00, 0x8b, 0x00},
        {0x00, 0x3C, 0x80, 0x00, 0x80, 0x18},
        {0x00, 0x14, 0x00, 0x00, 0x00, 0xFF},
        {0x00, 0x54, 0x00, 0x00, 0x00, 0x03}, //Analog Mux
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

    BM1368_set_job_difficulty_mask(BM1368_ASIC_DIFFICULTY);

    do_frequency_ramp_up((float)frequency);

    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, (uint8_t[]){0x00, 0x10, 0x00, 0x00, 0x15, 0xa4}, 6, false);
    BM1368_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);

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
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    #if BM1368_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: %02X", job.job_id);
    #endif

    _send_BM1368((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t *)&job, sizeof(BM1368_job), BM1368_DEBUG_WORK);
}

task_result * BM1368_process_work(void * pvParameters)
{
    bm1368_asic_result_t asic_result = {0};

    if (receive_work((uint8_t *)&asic_result, sizeof(asic_result)) == ESP_FAIL) {
        return NULL;
    }

    uint8_t job_id = (asic_result.job_id & 0xf0) >> 1;
    uint8_t core_id = (uint8_t)((ntohl(asic_result.nonce) >> 25) & 0x7f);
    uint8_t small_core_id = asic_result.job_id & 0x0f;
    uint32_t version_bits = (ntohs(asic_result.version) << 13);
    ESP_LOGI(TAG, "Job ID: %02X, Core: %d/%d, Ver: %08" PRIX32, job_id, core_id, small_core_id, version_bits);

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    if (GLOBAL_STATE->valid_jobs[job_id] == 0) {
        ESP_LOGW(TAG, "Invalid job found, 0x%02X", job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version | version_bits;

    result.job_id = job_id;
    result.nonce = asic_result.nonce;
    result.rolled_version = rolled_version;

    return &result;
}
