#include "bm1370.h"

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

#define BM1370_CHIP_ID 0x1370
#define BM1370_CHIP_ID_RESPONSE_LENGTH 11

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define BM_CHIP_ID 0x00
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
} bm1370_asic_result_job_t;

typedef struct __attribute__((__packed__))
{
    uint32_t value;                   // 2-5
    uint8_t asic_address;             // 6
    uint8_t register_address;         // 7
    uint16_t                  : 16;   // 8-9
} bm1370_asic_result_cmd_t;

typedef struct __attribute__((__packed__))
{
    uint16_t preamble;                // 0-1
    union {
        bm1370_asic_result_job_t job; // 2-9
        bm1370_asic_result_cmd_t cmd; // 2-9
    };
    uint8_t crc             : 5;      // 10:0-5
    uint8_t                 : 2;      // 10:6-7
    uint8_t is_job_response : 1;      // 10:8
} bm1370_asic_result_t;

static const char * TAG = "bm1370";

static task_result result;

static int address_interval;

/// @brief
/// @param ftdi
/// @param header
/// @param data
/// @param len
static void _send_BM1370(uint8_t header, const uint8_t * data, uint8_t data_len, bool debug)
{
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    const uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len + 6) : (data_len + 5);

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

    // send serial data
    if (SERIAL_send(buf, total_length, debug) == 0) {
        ESP_LOGE(TAG, "Failed to send data to BM1370");
    }
}

static void _send_chain_inactive(void)
{
    unsigned char read_address[] = {0x00, 0x00};
    // send serial data
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2, BM1370_SERIALTX_DEBUG);
}

static void _set_chip_address(uint8_t chipAddr)
{
    unsigned char read_address[] = {chipAddr, 0x00};
    // send serial data
    _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2, BM1370_SERIALTX_DEBUG);
}

void BM1370_set_version_mask(uint32_t version_mask) 
{
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF); 
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    _send_BM1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6, BM1370_SERIALTX_DEBUG);
}

void BM1370_send_hash_frequency(float target_freq) 
{
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float frequency;

    pll_get_parameters(target_freq, 160, 239, &fb_divider, &refdiv, &postdiv1, &postdiv2, &frequency);
    
    uint8_t vdo_scale = (fb_divider * FREQ_MULT / refdiv >= 2400) ? 0x50 : 0x40;
    uint8_t postdiv = (((postdiv1 - 1) & 0xf) << 4) | ((postdiv2 - 1) & 0xf);
    uint8_t freqbuf[6] = {0x00, 0x08, vdo_scale, fb_divider, refdiv, postdiv};

    _send_BM1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6, BM1370_SERIALTX_DEBUG);

    ESP_LOGI(TAG, "Setting Frequency to %g MHz (%g)", target_freq, frequency);
}

uint8_t BM1370_init(float frequency, uint16_t asic_count, uint16_t difficulty)
{
    // set version mask
    for (int i = 0; i < 3; i++) {
        BM1370_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);
    }

    //read register 00 on all chips (should respond AA 55 13 68 00 00 00 00 00 00 0F)
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, BM_CHIP_ID}, 2, BM1370_SERIALTX_DEBUG);

    int chip_counter = count_asic_chips(asic_count, BM1370_CHIP_ID, BM1370_CHIP_ID_RESPONSE_LENGTH);

    if (chip_counter == 0) {
        return 0;
    }


    // set version mask
    BM1370_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);

    //Reg_A8
    //unsigned char init5[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x00, 0x00, 0x03};
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xA8, 0x00, 0x07, 0x00, 0x00}, 6, BM1370_SERIALTX_DEBUG);

    //Misc Control
    //TX: 55 AA 51 09 [00 18 F0 00 C1 00] 04 //command all chips, write chip address 00, register 18, data F0 00 C1 00 - Misc Control
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x18, 0xF0, 0x00, 0xC1, 0x00}, 6, BM1370_SERIALTX_DEBUG); //from S21Pro dump
    //_send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00}, 6, BM1370_SERIALTX_DEBUG); //from S21 dump

    //chain inactive
    _send_chain_inactive();
    // unsigned char init7[7] = {0x55, 0xAA, 0x53, 0x05, 0x00, 0x00, 0x03};
    // _send_simple(init7, 7);

    // split the chip address space evenly
    address_interval = 256 / chip_counter;
    for (uint8_t i = 0; i < chip_counter; i++) {
        _set_chip_address(i * address_interval);
        // unsigned char init8[7] = {0x55, 0xAA, 0x40, 0x05, 0x00, 0x00, 0x1C};
        // _send_simple(init8, 7);
    }

    //Core Register Control
    //unsigned char init9[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x8B, 0x00, 0x12};
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x3C, 0x80, 0x00, 0x8B, 0x00}, 6, BM1370_SERIALTX_DEBUG);

    //Core Register Control
    //TX: 55 AA 51 09 [00 3C 80 00 80 0C] 11  //command all chips, write chip address 00, register 3C, data 80 00 80 0C - Core Register Control
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x3C, 0x80, 0x00, 0x80, 0x0C}, 6, BM1370_SERIALTX_DEBUG); //from S21Pro dump
    //_send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x3C, 0x80, 0x00, 0x80, 0x18}, 6, BM1370_SERIALTX_DEBUG); //from S21 dump

    //set difficulty mask
    uint8_t difficulty_mask[6];
    get_difficulty_mask(difficulty, difficulty_mask);
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), difficulty_mask, 6, BM1370_SERIALTX_DEBUG);    

    //Analog Mux Control -- not sent on S21 Pro?
    // unsigned char init12[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x54, 0x00, 0x00, 0x00, 0x03, 0x1D};
    // _send_simple(init12, 11);

    //Set the IO Driver Strength on chip 00
    //TX: 55 AA 51 09 [00 58 00 01 11 11] 0D  //command all chips, write chip address 00, register 58, data 01 11 11 11 - Set the IO Driver Strength on chip 00
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x58, 0x00, 0x01, 0x11, 0x11}, 6, BM1370_SERIALTX_DEBUG); //from S21Pro dump
    //_send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x58, 0x02, 0x11, 0x11, 0x11}, 6, BM1370_SERIALTX_DEBUG); //from S21Pro dump
    

    for (uint8_t i = 0; i < chip_counter; i++) {
        //TX: 55 AA 41 09 00 [A8 00 07 01 F0] 15    // Reg_A8
        unsigned char set_a8_register[6] = {i * address_interval, 0xA8, 0x00, 0x07, 0x01, 0xF0};
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_a8_register, 6, BM1370_SERIALTX_DEBUG);
        //TX: 55 AA 41 09 00 [18 F0 00 C1 00] 0C    // Misc Control
        unsigned char set_18_register[6] = {i * address_interval, 0x18, 0xF0, 0x00, 0xC1, 0x00};
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_18_register, 6, BM1370_SERIALTX_DEBUG);
        //TX: 55 AA 41 09 00 [3C 80 00 8B 00] 1A    // Core Register Control
        unsigned char set_3c_register_first[6] = {i * address_interval, 0x3C, 0x80, 0x00, 0x8B, 0x00};
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_first, 6, BM1370_SERIALTX_DEBUG);
        //TX: 55 AA 41 09 00 [3C 80 00 80 0C] 19    // Core Register Control
        unsigned char set_3c_register_second[6] = {i * address_interval, 0x3C, 0x80, 0x00, 0x80, 0x0C};
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_second, 6, BM1370_SERIALTX_DEBUG);
        //TX: 55 AA 41 09 00 [3C 80 00 82 AA] 05    // Core Register Control
        unsigned char set_3c_register_third[6] = {i * address_interval, 0x3C, 0x80, 0x00, 0x82, 0xAA};
        _send_BM1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_third, 6, BM1370_SERIALTX_DEBUG);
    }

    //Some misc settings?
    // TX: 55 AA 51 09 [00 B9 00 00 44 80] 0D    //command all chips, write chip address 00, register B9, data 00 00 44 80
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xB9, 0x00, 0x00, 0x44, 0x80}, 6, BM1370_SERIALTX_DEBUG);
    // TX: 55 AA 51 09 [00 54 00 00 00 02] 18    //command all chips, write chip address 00, register 54, data 00 00 00 02 - Analog Mux Control - rumored to control the temp diode
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x54, 0x00, 0x00, 0x00, 0x02}, 6, BM1370_SERIALTX_DEBUG);
    // TX: 55 AA 51 09 [00 B9 00 00 44 80] 0D    //command all chips, write chip address 00, register B9, data 00 00 44 80 -- duplicate of first command in series
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0xB9, 0x00, 0x00, 0x44, 0x80}, 6, BM1370_SERIALTX_DEBUG);
    // TX: 55 AA 51 09 [00 3C 80 00 8D EE] 1B    //command all chips, write chip address 00, register 3C, data 80 00 8D EE
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), (uint8_t[]){0x00, 0x3C, 0x80, 0x00, 0x8D, 0xEE}, 6, BM1370_SERIALTX_DEBUG);

    //ramp up the hash frequency
    do_frequency_transition(frequency, BM1370_send_hash_frequency);

    //register 10 is still a bit of a mystery. discussion: https://github.com/bitaxeorg/ESP-Miner/pull/167

    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x11, 0x5A}; //S19k Pro Default
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x14, 0x46}; //S19XP-Luxos Default
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x15, 0x1C}; //S19XP-Stock Default
    //unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x15, 0xA4}; //S21-Stock Default
    unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x1E, 0xB5}; //S21 Pro-Stock Default
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x0F, 0x00, 0x00}; //supposedly the "full" 32bit nonce range
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), set_10_hash_counting, 6, BM1370_SERIALTX_DEBUG);

    return chip_counter;
}

// static void _send_read_address(void)
// {

//     unsigned char read_address[2] = {0x00, 0x00};
//     // send serial data
//     _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_READ), read_address, 2, BM1370_SERIALTX_DEBUG);
// }

// Baud formula = 25M/((denominator+1)*8)
// The denominator is 5 bits found in the misc_control (bits 9-13)
int BM1370_set_default_baud(void)
{
    // default divider of 26 (11010) for 115,749
    unsigned char baudrate[] = {0x00, MISC_CONTROL, 0x00, 0x00, 0b01111010, 0b00110001}; // baudrate - misc_control
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), baudrate, 6, BM1370_SERIALTX_DEBUG);
    return 115749;
}

int BM1370_set_max_baud(void)
{
    // divider of 0 for 3,125,000
    ESP_LOGI(TAG, "Setting max baud of 1000000 ");

    unsigned char fast_uart[] = {0x00, FAST_UART_CONFIGURATION, 0x11, 0x30, 0x02, 0x00};
    _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), fast_uart, 6, BM1370_SERIALTX_DEBUG);
    return 1000000;
}

static uint8_t id = 0;

void BM1370_send_work(void * pvParameters, bm_job * next_bm_job)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    BM1370_job job;
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
    #if BM1370_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: %02X", job.job_id);
    #endif

    _send_BM1370((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t *)&job, sizeof(BM1370_job), BM1370_DEBUG_WORK);
}

task_result * BM1370_process_work(void * pvParameters)
{
    bm1370_asic_result_t asic_result = {0};

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
        result.asic_nr = asic_result.cmd.asic_address / address_interval;
        result.value = ntohl(asic_result.cmd.value);
        
        return &result;
    }

    uint8_t job_id = (asic_result.job.id & 0xf0) >> 1;
    uint32_t nonce_h = ntohl(asic_result.job.nonce);
    uint8_t asic_nr = (uint8_t)((nonce_h >> 17) & 0xff) / address_interval; // Asic address is encoded in the next 8 bits
    uint8_t core_id = (uint8_t)((nonce_h >> 25) & 0x7f); // BM1370 has 80 cores, so it should be coded on 7 bits
    uint8_t small_core_id = asic_result.job.id & 0x0f; // BM1370 has 16 small cores, so it should be coded on 4 bits
    uint32_t version_bits = (ntohs(asic_result.job.version) << 13); // shift the 16 bit value left 13
    ESP_LOGI(TAG, "Job ID: %02X, Asic nr: %d, Core: %d/%d, Ver: %08" PRIX32, job_id, asic_nr, core_id, small_core_id, version_bits);

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    if (GLOBAL_STATE->valid_jobs[job_id] == 0) {
        ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version | version_bits;

    result.job_id = job_id;
    result.nonce = asic_result.job.nonce;
    result.rolled_version = rolled_version;
    result.asic_nr = asic_nr;

    return &result;
}

void BM1370_read_registers(void)
{
    int size = sizeof(REGISTER_MAP) / sizeof(REGISTER_MAP[0]);
    for (int reg = 0; reg < size; reg++) {
        if (REGISTER_MAP[reg] != REGISTER_INVALID) {
            _send_BM1370((TYPE_CMD | GROUP_ALL | CMD_READ), (uint8_t[]){0x00, reg}, 2, BM1370_SERIALTX_DEBUG);
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }
}
