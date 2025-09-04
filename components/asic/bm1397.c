#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_transition_bmXX.h"
#include "esp_log.h"

#include "serial.h"
#include "bm1397.h"
#include "utils.h"
#include "crc.h"
#include "mining.h"
#include "global_state.h"
#include "pll.h"

#define BM1397_CHIP_ID 0x1397
#define BM1397_CHIP_ID_RESPONSE_LENGTH 9

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define SLEEP_TIME 20
#define FREQ_MULT 25.0

#define CLOCK_ORDER_CONTROL_0 0x80
#define CLOCK_ORDER_CONTROL_1 0x84
#define ORDERED_CLOCK_ENABLE 0x20
#define CORE_REGISTER_CONTROL 0x3C
#define PLL3_PARAMETER 0x68
#define FAST_UART_CONFIGURATION 0x28
#define MISC_CONTROL 0x18

typedef struct __attribute__((__packed__))
{
    uint16_t preamble;
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t job_id;
    uint8_t crc;
} bm1397_asic_result_t;

static const char * TAG = "bm1397";

static uint32_t prev_nonce = 0;
static task_result result;

/// @brief
/// @param ftdi
/// @param header
/// @param data
/// @param len
static void _send_BM1397(uint8_t header, uint8_t *data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

    // allocate memory for buffer
    unsigned char *buf = malloc(total_length);

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
    if (packet_type == JOB_PACKET)
    {
        uint16_t crc16_total = crc16_false(buf + 2, data_len + 2);
        buf[4 + data_len] = (crc16_total >> 8) & 0xFF;
        buf[5 + data_len] = crc16_total & 0xFF;
    }
    else
    {
        buf[4 + data_len] = crc5(buf + 2, data_len + 2);
    }

    // send serial data
    SERIAL_send(buf, total_length, debug);

    free(buf);
}

static void _send_read_address(void)
{
    unsigned char read_address[2] = {0x00, 0x00};
    // send serial data
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_READ), read_address, 2, BM1397_SERIALTX_DEBUG);
}

static void _send_chain_inactive(void)
{

    unsigned char read_address[2] = {0x00, 0x00};
    // send serial data
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2, BM1397_SERIALTX_DEBUG);
}

static void _set_chip_address(uint8_t chipAddr)
{

    unsigned char read_address[2] = {chipAddr, 0x00};
    // send serial data
    _send_BM1397((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2, BM1397_SERIALTX_DEBUG);
}

void BM1397_set_version_mask(uint32_t version_mask) {
    // placeholder
}

void BM1397_send_hash_frequency(float target_freq)
{
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float frequency;

    pll_get_parameters(target_freq, 60, 200, &fb_divider, &refdiv, &postdiv1, &postdiv2, &frequency);

    uint8_t vdo_scale = 0x40;
    uint8_t postdiv = ((postdiv1 & 0x7) << 4) + (postdiv2 & 0x7);
    uint8_t freqbuf[6] = {0x00, 0x08, vdo_scale, fb_divider, refdiv, postdiv};  // freqbuf - pll0_parameter
    uint8_t prefreq1[6] = {0x00, 0x70, 0x0F, 0x0F, 0x0F, 0x00}; // prefreq - pll0_divider

    for (int i = 0; i < 2; i++)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), prefreq1, 6, BM1397_SERIALTX_DEBUG);
    }
    for (int i = 0; i < 2; i++)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), freqbuf, 6, BM1397_SERIALTX_DEBUG);
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Setting Frequency to %g MHz (%g)", target_freq, frequency);
}

uint8_t BM1397_init(float frequency, uint16_t asic_count, uint16_t difficulty)
{
    // send the init command
    _send_read_address();

    int chip_counter = count_asic_chips(asic_count, BM1397_CHIP_ID, BM1397_CHIP_ID_RESPONSE_LENGTH);

    if (chip_counter == 0) {
        return 0;
    }

    // send serial data
    vTaskDelay(SLEEP_TIME / portTICK_PERIOD_MS);
    _send_chain_inactive();

    // split the chip address space evenly
    for (uint8_t i = 0; i < asic_count; i++) {
        _set_chip_address(i * (256 / asic_count));
    }

    unsigned char init[6] = {0x00, CLOCK_ORDER_CONTROL_0, 0x00, 0x00, 0x00, 0x00}; // init1 - clock_order_control0
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init, 6, BM1397_SERIALTX_DEBUG);

    unsigned char init2[6] = {0x00, CLOCK_ORDER_CONTROL_1, 0x00, 0x00, 0x00, 0x00}; // init2 - clock_order_control1
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init2, 6, BM1397_SERIALTX_DEBUG);

    unsigned char init3[9] = {0x00, ORDERED_CLOCK_ENABLE, 0x00, 0x00, 0x00, 0x01}; // init3 - ordered_clock_enable
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init3, 6, BM1397_SERIALTX_DEBUG);

    unsigned char init4[9] = {0x00, CORE_REGISTER_CONTROL, 0x80, 0x00, 0x80, 0x74}; // init4 - init_4_?
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init4, 6, BM1397_SERIALTX_DEBUG);

    //set difficulty mask
    uint8_t difficulty_mask[6];
    get_difficulty_mask(difficulty, difficulty_mask);
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), difficulty_mask, 6, BM1397_SERIALTX_DEBUG);

    unsigned char init5[9] = {0x00, PLL3_PARAMETER, 0xC0, 0x70, 0x01, 0x11}; // init5 - pll3_parameter
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init5, 6, BM1397_SERIALTX_DEBUG);

    unsigned char init6[9] = {0x00, FAST_UART_CONFIGURATION, 0x06, 0x00, 0x00, 0x0F}; // init6 - fast_uart_configuration
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init6, 6, BM1397_SERIALTX_DEBUG);

    BM1397_set_default_baud();

    //ramp up the hash frequency
    do_frequency_transition(frequency, BM1397_send_hash_frequency);

    return chip_counter;
}

// Baud formula = 25M/((denominator+1)*8)
// The denominator is 5 bits found in the misc_control (bits 9-13)
int BM1397_set_default_baud(void)
{
    // default divider of 26 (11010) for 115,749
    unsigned char baudrate[9] = {0x00, MISC_CONTROL, 0x00, 0x00, 0b01111010, 0b00110001}; // baudrate - misc_control
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), baudrate, 6, BM1397_SERIALTX_DEBUG);
    return 115749;
}

int BM1397_set_max_baud(void)
{
    // divider of 0 for 3,125,000
    ESP_LOGI(TAG, "Setting max baud of 3125000");
    unsigned char baudrate[9] = {0x00, MISC_CONTROL, 0x00, 0x00, 0b01100000, 0b00110001};
    ; // baudrate - misc_control
    _send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), baudrate, 6, BM1397_SERIALTX_DEBUG);
    return 3125000;
}

static uint8_t id = 0;

void BM1397_send_work(void *pvParameters, bm_job *next_bm_job)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    job_packet job;
    // max job number is 128
    // there is still some really weird logic with the job id bits for the asic to sort out
    // so we have it limited to 128 and it has to increment by 4
    id = (id + 4) % 128;

    job.job_id = id;
    job.num_midstates = next_bm_job->num_midstates;
    memcpy(&job.starting_nonce, &next_bm_job->starting_nonce, 4);
    memcpy(&job.nbits, &next_bm_job->target, 4);
    memcpy(&job.ntime, &next_bm_job->ntime, 4);
    memcpy(&job.merkle4, next_bm_job->merkle_root + 28, 4);
    memcpy(job.midstate, next_bm_job->midstate, 32);

    if (job.num_midstates == 4)
    {
        memcpy(job.midstate1, next_bm_job->midstate1, 32);
        memcpy(job.midstate2, next_bm_job->midstate2, 32);
        memcpy(job.midstate3, next_bm_job->midstate3, 32);
    }

    if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] != NULL)
    {
        free_bm_job(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id]);
    }

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] = next_bm_job;

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    GLOBAL_STATE->valid_jobs[job.job_id] = 1;
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    #if BM1397_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: %02X", job.job_id);
    #endif

    _send_BM1397((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t *)&job, sizeof(job_packet), BM1397_DEBUG_WORK);
}

task_result *BM1397_process_work(void *pvParameters)
{
    bm1397_asic_result_t asic_result = {0};

    if (receive_work((uint8_t *)&asic_result, sizeof(asic_result)) == ESP_FAIL) {
        return NULL;
    }

    uint8_t nonce_found = 0;
    uint32_t first_nonce = 0;

    uint8_t rx_job_id = asic_result.job_id & 0xfc;
    uint8_t rx_midstate_index = asic_result.job_id & 0x03;

    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    if (GLOBAL_STATE->valid_jobs[rx_job_id] == 0)
    {
        ESP_LOGW(TAG, "Invalid job nonce found, id=%d", rx_job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->version;
    for (int i = 0; i < rx_midstate_index; i++)
    {
        rolled_version = increment_bitmask(rolled_version, GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->version_mask);
    }

    // ASIC may return the same nonce multiple times
    // or one that was already found
    // most of the time it behavies however
    if (nonce_found == 0)
    {
        first_nonce = asic_result.nonce;
        nonce_found = 1;
    }
    else if (asic_result.nonce == first_nonce)
    {
        // stop if we've already seen this nonce
        return NULL;
    }

    if (asic_result.nonce == prev_nonce)
    {
        return NULL;
    }
    else
    {
        prev_nonce = asic_result.nonce;
    }

    result.job_id = rx_job_id;
    result.nonce = asic_result.nonce;
    result.rolled_version = rolled_version;

    return &result;
}
