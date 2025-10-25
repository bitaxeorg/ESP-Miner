#include "bm1368.h"

#include "crc.h"
#include "global_state.h"
#include "serial.h"
#include "utils.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_transition_bmXX.h"
#include "pll.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define BM1368_CHIP_ID 0x1368
#define BM1368_CHIP_ID_RESPONSE_LENGTH 11

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define MISC_CONTROL 0x18
#define FAST_UART_CONFIGURATION 0x28

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
    uint32_t nonce;                   // 2-5
    uint8_t midstate_num;             // 6
    uint8_t id;                       // 7
    uint16_t version;                 // 8-9
} bm1368_asic_result_job_t;

typedef struct __attribute__((__packed__))
{
    uint32_t value;                   // 2-5
    uint8_t asic_address;             // 6
    uint8_t register_address;         // 7
    uint16_t                  : 16;   // 8-9
} bm1368_asic_result_cmd_t;

typedef struct __attribute__((__packed__))
{
    uint16_t preamble;                // 0-1
    union {
        bm1368_asic_result_job_t job; // 2-9
        bm1368_asic_result_cmd_t cmd; // 2-9
    };
    uint8_t crc             : 5;      // 10:0-5
    uint8_t                 : 2;      // 10:6-7
    uint8_t is_job_response : 1;      // 10:8
} bm1368_asic_result_t;

static const char * TAG = "bm1368";

static task_result result;

static void _send_BM1368(uint8_t header, uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

    uint8_t buf[total_length];

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

void BM1368_send_hash_frequency(float target_freq) 
{
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float new_freq;
    
    pll_get_parameters(target_freq, 144, 235, &fb_divider, &refdiv, &postdiv1, &postdiv2, &new_freq);

    uint8_t vdo_scale = (fb_divider * FREQ_MULT / refdiv >= 2400) ? 0x50 : 0x40;
    uint8_t postdiv = (((postdiv1 - 1) & 0xf) << 4) | ((postdiv2 - 1) & 0xf);
    uint8_t freqbuf[6] = {0x00, 0x08, vdo_scale, fb_divider, refdiv, postdiv};

    _send_BM1368(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, sizeof(freqbuf), BM1368_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Setting Frequency to %g MHz (%g)", target_freq, new_freq);
}

uint8_t BM1368_init(float frequency, uint16_t asic_count, uint16_t difficulty)
{
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

    uint8_t difficulty_mask[6];
    get_difficulty_mask(difficulty, difficulty_mask);
    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_WRITE), difficulty_mask, 6, BM1368_SERIALTX_DEBUG);    

    do_frequency_transition(frequency, BM1368_send_hash_frequency);

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

    unsigned char fast_uart[] = {0x00, FAST_UART_CONFIGURATION, 0x11, 0x30, 0x02, 0x00};
    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_WRITE), fast_uart, 6, BM1368_SERIALTX_DEBUG);

    return 1000000;
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

    memset(&result, 0, sizeof(task_result));

    if (receive_work((uint8_t *)&asic_result, sizeof(asic_result)) == ESP_FAIL) {
        return NULL;
    }

    if (!asic_result.is_job_response) {
        result.register_type = REGISTER_MAP[asic_result.cmd.register_address];
        if (result.register_type == REGISTER_INVALID) {
            ESP_LOGW(TAG, "Unknown register read: %02x", asic_result.cmd.register_address);
            return NULL;
        }
        result.asic_nr = asic_result.cmd.asic_address;
        result.value = ntohl(asic_result.cmd.value);
        
        return &result;
    }

    uint8_t job_id = (asic_result.job.id & 0xf0) >> 1;
    uint8_t core_id = (uint8_t)((ntohl(asic_result.job.nonce) >> 25) & 0x7f);
    uint8_t small_core_id = asic_result.job.id & 0x0f;
    uint32_t version_bits = (ntohs(asic_result.job.version) << 13);
    ESP_LOGI(TAG, "Job ID: %02X, Core: %d/%d, Ver: %08" PRIX32, job_id, core_id, small_core_id, version_bits);

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    if (GLOBAL_STATE->valid_jobs[job_id] == 0) {
        ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version | version_bits;

    result.job_id = job_id;
    result.nonce = asic_result.job.nonce;
    result.rolled_version = rolled_version;

    return &result;
}

void BM1368_read_registers(void)
{
    int size = sizeof(REGISTER_MAP) / sizeof(REGISTER_MAP[0]);
    for (int reg = 0; reg < size; reg++) {
        if (REGISTER_MAP[reg] != REGISTER_INVALID) {
            _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, reg}, 2, BM1368_SERIALTX_DEBUG);
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }
}
