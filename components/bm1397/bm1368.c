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

/// @brief
/// @param ftdi
/// @param header
/// @param data
/// @param len
static void _send_BM1368(uint8_t header, uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

    // allocate memory for buffer
    unsigned char * buf = malloc(total_length);

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

    // send serial data
    SERIAL_send(buf, total_length, debug);

    free(buf);
}

static void _send_simple(uint8_t * data, uint8_t total_length)
{
    unsigned char * buf = malloc(total_length);
    memcpy(buf, data, total_length);
    SERIAL_send(buf, total_length, false);

    free(buf);
}

static void _send_chain_inactive(void)
{

    unsigned char read_address[2] = {0x00, 0x00};
    // send serial data
    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2, false);
}

static void _set_chip_address(uint8_t chipAddr)
{

    unsigned char read_address[2] = {chipAddr, 0x00};
    // send serial data
    _send_BM1368((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2, false);
}

void BM1368_send_hash_frequency(float target_freq)
{
    // default 200Mhz if it fails
    unsigned char freqbuf[9] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x41}; // freqbuf - pll0_parameter
    float newf = 200.0;

    uint8_t fb_divider = 0;
    uint8_t post_divider1 = 0, post_divider2 = 0;
    uint8_t ref_divider = 0;
    float min_difference = 10;

    // refdiver is 2 or 1
    // postdivider 2 is 1 to 7
    // postdivider 1 is 1 to 7 and less than postdivider 2
    // fbdiv is 144 to 235
    for (uint8_t refdiv_loop = 2; refdiv_loop > 0 && fb_divider == 0; refdiv_loop--) {
        for (uint8_t postdiv1_loop = 7; postdiv1_loop > 0 && fb_divider == 0; postdiv1_loop--) {
            for (uint8_t postdiv2_loop = 1; postdiv2_loop < postdiv1_loop && fb_divider == 0; postdiv2_loop++) {
                int temp_fb_divider = round(((float) (postdiv1_loop * postdiv2_loop * target_freq * refdiv_loop) / 25.0));

                if (temp_fb_divider >= 144 && temp_fb_divider <= 235) {
                    float temp_freq = 25.0 * (float) temp_fb_divider / (float) (refdiv_loop * postdiv2_loop * postdiv1_loop);
                    float freq_diff = fabs(target_freq - temp_freq);

                    if (freq_diff < min_difference) {
                        fb_divider = temp_fb_divider;
                        post_divider1 = postdiv1_loop;
                        post_divider2 = postdiv2_loop;
                        ref_divider = refdiv_loop;
                        min_difference = freq_diff;
                        break;
                    }
                }
            }
        }
    }

    if (fb_divider == 0) {
        puts("Finding dividers failed, using default value (200Mhz)");
    } else {
        newf = 25.0 / (float) (ref_divider * fb_divider) / (float) (post_divider1 * post_divider2);
        printf("final refdiv: %d, fbdiv: %d, postdiv1: %d, postdiv2: %d, min diff value: %f\n", ref_divider, fb_divider,
               post_divider1, post_divider2, min_difference);

        freqbuf[3] = fb_divider;
        freqbuf[4] = ref_divider;
        freqbuf[5] = (((post_divider1 - 1) & 0xf) << 4) + ((post_divider2 - 1) & 0xf);

        if (fb_divider * 25 / (float) ref_divider >= 2400) {
            freqbuf[2] = 0x50;
        }
    }

    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_WRITE), freqbuf, 6, false);

    ESP_LOGI(TAG, "Setting Frequency to %.2fMHz (%.2f)", target_freq, newf);
}

static void do_frequency_ramp_up() {

    //PLLO settings taken from a S21 dump.
    //todo: do this right.
    uint8_t freq_list[65][4] = {{0x40, 0xA2, 0x02, 0x55},
        {0x40, 0xAF, 0x02, 0x64},
        {0x40, 0xA5, 0x02, 0x54},
        {0x40, 0xA8, 0x02, 0x63},
        {0x40, 0xB6, 0x02, 0x63},
        {0x40, 0xA8, 0x02, 0x53},
        {0x40, 0xB4, 0x02, 0x53},
        {0x40, 0xA8, 0x02, 0x62},
        {0x40, 0xAA, 0x02, 0x43},
        {0x40, 0xA2, 0x02, 0x52},
        {0x40, 0xAB, 0x02, 0x52},
        {0x40, 0xB4, 0x02, 0x52},
        {0x40, 0xBD, 0x02, 0x52},
        {0x40, 0xA5, 0x02, 0x42},
        {0x40, 0xA1, 0x02, 0x61},
        {0x40, 0xA8, 0x02, 0x61},
        {0x40, 0xAF, 0x02, 0x61},
        {0x40, 0xB6, 0x02, 0x61},
        {0x40, 0xA2, 0x02, 0x51},
        {0x40, 0xA8, 0x02, 0x51},
        {0x40, 0xAE, 0x02, 0x51},
        {0x40, 0xB4, 0x02, 0x51},
        {0x40, 0xBA, 0x02, 0x51},
        {0x40, 0xA0, 0x02, 0x41},
        {0x40, 0xA5, 0x02, 0x41},
        {0x40, 0xAA, 0x02, 0x41},
        {0x40, 0xAF, 0x02, 0x41},
        {0x40, 0xB4, 0x02, 0x41},
        {0x40, 0xB9, 0x02, 0x41},
        {0x40, 0xBE, 0x02, 0x41},
        {0x40, 0xA0, 0x02, 0x31},
        {0x40, 0xA4, 0x02, 0x31},
        {0x40, 0xA8, 0x02, 0x31},
        {0x40, 0xAC, 0x02, 0x31},
        {0x40, 0xB0, 0x02, 0x31},
        {0x40, 0xB4, 0x02, 0x31},
        {0x40, 0xA1, 0x02, 0x60},
        {0x40, 0xBC, 0x02, 0x31},
        {0x40, 0xA8, 0x02, 0x60},
        {0x40, 0xAF, 0x02, 0x60},
        {0x50, 0xCC, 0x02, 0x31},
        {0x40, 0xB6, 0x02, 0x60},
        {0x50, 0xD4, 0x02, 0x31},
        {0x40, 0xA2, 0x02, 0x50},
        {0x40, 0xA5, 0x02, 0x50},
        {0x40, 0xA8, 0x02, 0x50},
        {0x40, 0xAB, 0x02, 0x50},
        {0x40, 0xAE, 0x02, 0x50},
        {0x40, 0xB1, 0x02, 0x50},
        {0x40, 0xB4, 0x02, 0x50},
        {0x40, 0xB7, 0x02, 0x50},
        {0x40, 0xBA, 0x02, 0x50},
        {0x40, 0xBD, 0x02, 0x50},
        {0x40, 0xA0, 0x02, 0x40},
        {0x50, 0xC3, 0x02, 0x50},
        {0x40, 0xA5, 0x02, 0x40},
        {0x50, 0xC9, 0x02, 0x50},
        {0x40, 0xAA, 0x02, 0x40},
        {0x50, 0xCF, 0x02, 0x50},
        {0x40, 0xAF, 0x02, 0x40},
        {0x50, 0xD5, 0x02, 0x50},
        {0x40, 0xB4, 0x02, 0x40},
        {0x50, 0xDB, 0x02, 0x50},
        {0x40, 0xB9, 0x02, 0x40},
        {0x50, 0xE0, 0x02, 0x50}};

    uint8_t freq_cmd[6] = {0x00, 0x08, 0x40, 0xB4, 0x02, 0x40};

    for (int i = 0; i < 65; i++) {
        freq_cmd[2] = freq_list[i][0];
        freq_cmd[3] = freq_list[i][1];
        freq_cmd[4] = freq_list[i][2];
        freq_cmd[5] = freq_list[i][3];
        _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_WRITE), freq_cmd, 6, false);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

static void _send_init(uint64_t frequency) {

    //enable and set version rolling mask to 0xFFFF
    unsigned char init0[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF, 0x1C};
    _send_simple(init0, 11);

    //enable and set version rolling mask to 0xFFFF (again)
    unsigned char init1[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF, 0x1C};
    _send_simple(init1, 11);

    //enable and set version rolling mask to 0xFFFF (again)
    unsigned char init2[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF, 0x1C};
    _send_simple(init2, 11);

    //read register 00 on all chips (should respond AA 55 13 68 00 00 00 00 00 00 0F)
    unsigned char init3[7] = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};
    _send_simple(init3, 7);

    //enable and set version rolling mask to 0xFFFF (again)
    unsigned char init4[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF, 0x1C};
    _send_simple(init4, 11);

    //Reg_A8
    unsigned char init5[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x00, 0x00, 0x03};
    _send_simple(init5, 11);

    //Misc Control
    unsigned char init6[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00, 0x00};
    _send_simple(init6, 11);

    //chain inactive
    unsigned char init7[7] = {0x55, 0xAA, 0x53, 0x05, 0x00, 0x00, 0x03};
    _send_simple(init7, 7);

    //assign address 0x00 to the first chip
    unsigned char init8[7] = {0x55, 0xAA, 0x40, 0x05, 0x00, 0x00, 0x1C};
    _send_simple(init8, 7);

    //Core Register Control
    unsigned char init9[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x8B, 0x00, 0x12};
    _send_simple(init9, 11);

    //Core Register Control
    unsigned char init10[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x80, 0x18, 0x1F};
    _send_simple(init10, 11);

    //set ticket mask
    unsigned char init11[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x14, 0x00, 0x00, 0x00, 0xFF, 0x08};
    _send_simple(init11, 11);

    //Analog Mux Control
    unsigned char init12[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x54, 0x00, 0x00, 0x00, 0x03, 0x1D};
    _send_simple(init12, 11);

    //Set the IO Driver Strength on chip 00
    unsigned char init13[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x58, 0x02, 0x11, 0x11, 0x11, 0x06};
    _send_simple(init13, 11);

    //Reg_A8
    unsigned char init14[11] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x01, 0xF0, 0x15};
    _send_simple(init14, 11);

    //Misc Control
    unsigned char init15[11] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x18, 0xF0, 0x00, 0xC1, 0x00, 0x0C};
    _send_simple(init15, 11);

    //Core Register Control
    unsigned char init16[11] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x8B, 0x00, 0x1A};
    _send_simple(init16, 11);

    //Core Register Control
    unsigned char init17[11] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x80, 0x18, 0x17};
    _send_simple(init17, 11);

    //Core Register Control
    unsigned char init18[11] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x82, 0xAA, 0x05};
    _send_simple(init18, 11);

    do_frequency_ramp_up();

    BM1368_send_hash_frequency(frequency);

}

// reset the BM1368 via the RTS line
static void _reset(void)
{
    gpio_set_level(BM1368_RST_PIN, 0);

    // delay for 100ms
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // set the gpio pin high
    gpio_set_level(BM1368_RST_PIN, 1);

    // delay for 100ms
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

static void _send_read_address(void)
{

    unsigned char read_address[2] = {0x00, 0x00};
    // send serial data
    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_READ), read_address, 2, false);
}

void BM1368_init(uint64_t frequency)
{
    ESP_LOGI(TAG, "Initializing BM1368");

    memset(asic_response_buffer, 0, 1024);

    esp_rom_gpio_pad_select_gpio(BM1368_RST_PIN);
    gpio_set_direction(BM1368_RST_PIN, GPIO_MODE_OUTPUT);

    // reset the bm1368
    _reset();

    // send the init command
    //_send_read_address();

    _send_init(frequency);
}

// Baud formula = 25M/((denominator+1)*8)
// The denominator is 5 bits found in the misc_control (bits 9-13)
int BM1368_set_default_baud(void)
{
    // default divider of 26 (11010) for 115,749
    unsigned char baudrate[9] = {0x00, MISC_CONTROL, 0x00, 0x00, 0b01111010, 0b00110001}; // baudrate - misc_control
    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_WRITE), baudrate, 6, false);
    return 115749;
}

int BM1368_set_max_baud(void)
{

    /// return 115749;

    // divider of 0 for 3,125,000
    ESP_LOGI(TAG, "Setting max baud of 1000000 ");

    unsigned char init8[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x28, 0x11, 0x30, 0x02, 0x00, 0x03};
    _send_simple(init8, 11);
    return 1000000;
}

void BM1368_set_job_difficulty_mask(int difficulty)
{

    return;

    // Default mask of 256 diff
    unsigned char job_difficulty_mask[9] = {0x00, TICKET_MASK, 0b00000000, 0b00000000, 0b00000000, 0b11111111};

    // The mask must be a power of 2 so there are no holes
    // Correct:  {0b00000000, 0b00000000, 0b11111111, 0b11111111}
    // Incorrect: {0b00000000, 0b00000000, 0b11100111, 0b11111111}
    // (difficulty - 1) if it is a pow 2 then step down to second largest for more hashrate sampling
    difficulty = _largest_power_of_two(difficulty) - 1;

    // convert difficulty into char array
    // Ex: 256 = {0b00000000, 0b00000000, 0b00000000, 0b11111111}, {0x00, 0x00, 0x00, 0xff}
    // Ex: 512 = {0b00000000, 0b00000000, 0b00000001, 0b11111111}, {0x00, 0x00, 0x01, 0xff}
    for (int i = 0; i < 4; i++) {
        char value = (difficulty >> (8 * i)) & 0xFF;
        // The char is read in backwards to the register so we need to reverse them
        // So a mask of 512 looks like 0b00000000 00000000 00000001 1111111
        // and not 0b00000000 00000000 10000000 1111111

        job_difficulty_mask[5 - i] = _reverse_bits(value);
    }

    ESP_LOGI(TAG, "Setting job ASIC mask to %d", difficulty);

    _send_BM1368((TYPE_CMD | GROUP_ALL | CMD_WRITE), job_difficulty_mask, 6, false);
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
    // ESP_LOGI(TAG, "Added Job: %i", job.job_id);
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    _send_BM1368((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), &job, sizeof(BM1368_job), false);
}

asic_result * BM1368_receive_work(void)
{
    // wait for a response, wait time is pretty arbitrary
    int received = SERIAL_rx(asic_response_buffer, 11, 60000);

    if (received < 0) {
        ESP_LOGI(TAG, "Error in serial RX");
        return NULL;
    } else if (received == 0) {
        // Didn't find a solution, restart and try again
        return NULL;
    }

    if (received != 11 || asic_response_buffer[0] != 0xAA || asic_response_buffer[1] != 0x55) {
        ESP_LOGI(TAG, "Serial RX invalid %i", received);
        ESP_LOG_BUFFER_HEX(TAG, asic_response_buffer, received);
        return NULL;
    }

    return (asic_result *) asic_response_buffer;
}

static uint16_t reverse_uint16(uint16_t num)
{
    return (num >> 8) | (num << 8);
}

task_result * BM1368_proccess_work(void * pvParameters)
{

    asic_result * asic_result = BM1368_receive_work();

    if (asic_result == NULL) {
        return NULL;
    }

    uint8_t job_id = asic_result->job_id;
    uint8_t rx_job_id = ((int8_t)job_id & 0xf0) >> 1;
    ESP_LOGI(TAG, "RX Job ID: %02X", rx_job_id);

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    if (GLOBAL_STATE->valid_jobs[rx_job_id] == 0) {
        ESP_LOGE(TAG, "Invalid job nonce found, 0x%02X", rx_job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->version;

    // // // shift the 16 bit value left 13
    rolled_version = (reverse_uint16(asic_result->version) << 13) | rolled_version;

    result.job_id = rx_job_id;
    result.nonce = asic_result->nonce;
    result.rolled_version = rolled_version;

    return &result;
}
