#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "asic_common.h"
#include "bm_job_packet.h"
#include "bm1397.h"
#include "mining.h"
#include "serial.h"
#include "esp_log.h"
#include "crc.h"
#include "esp_timer.h"

#define PREAMBLE 0xAA55

static const char * TAG = "common";
static char asic_chain_error[96];

static void format_asic_indices(char *buffer, size_t buffer_size, int first_index, int end_index)
{
    size_t offset = 0;

    for (int index = first_index; index < end_index; index++) {
        int written = snprintf(buffer + offset, buffer_size - offset, "%s%d", index == first_index ? "" : ",", index);
        if (written < 0 || (size_t) written >= buffer_size - offset) {
            snprintf(buffer, buffer_size, "%d-%d", first_index, end_index - 1);
            return;
        }

        offset += written;
    }
}

void clear_asic_chain_error(void)
{
    asic_chain_error[0] = '\0';
}

const char *get_asic_chain_error(void)
{
    return asic_chain_error[0] == '\0' ? NULL : asic_chain_error;
}

unsigned char _reverse_bits(unsigned char num)
{
    unsigned char reversed = 0;
    int i;

    for (i = 0; i < 8; i++) {
        reversed <<= 1;      // Left shift the reversed variable by 1
        reversed |= num & 1; // Use bitwise OR to set the rightmost bit of reversed to the current bit of num
        num >>= 1;           // Right shift num by 1 to get the next bit
    }

    return reversed;
}

int _largest_power_of_two(int num)
{
    int power = 0;

    while (num > 1) {
        num = num >> 1;
        power++;
    }

    return 1 << power;
}

int _next_power_of_two(int num)
{
    if (num <= 1)
        return 1;

    int power = 1;

    while (power < num) {
        power <<= 1;
    }

    return power;
}

int count_asic_chips_with_id_alias(uint16_t asic_count, uint16_t chip_id, uint16_t chip_id_alias, int chip_id_response_length)
{
    uint8_t buffer[11] = {0};

    clear_asic_chain_error();

    int chip_counter = 0;
    while (true) {
        int received = SERIAL_rx(buffer, chip_id_response_length, 1000);
        if (received == 0) break;

        if (received == -1) {
            ESP_LOGE(TAG, "Error reading CHIP_ID");
            break;
        }

        if (received != chip_id_response_length) {
            ESP_LOGE(TAG, "Invalid CHIP_ID response length: expected %d, got %d", chip_id_response_length, received);
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            break;
        }

        uint16_t received_preamble = (buffer[0] << 8) | buffer[1];
        if (received_preamble != PREAMBLE) {
            ESP_LOGW(TAG, "Preamble mismatch: expected 0x%04x, got 0x%04x", PREAMBLE, received_preamble);
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            continue;
        }

        uint16_t received_chip_id = (buffer[2] << 8) | buffer[3];
        if (received_chip_id != chip_id && received_chip_id != chip_id_alias) {
            if (chip_id == chip_id_alias) {
                ESP_LOGW(TAG, "CHIP_ID response mismatch: expected 0x%04x, got 0x%04x", chip_id, received_chip_id);
            } else {
                ESP_LOGW(TAG, "CHIP_ID response mismatch: expected 0x%04x or 0x%04x, got 0x%04x", chip_id, chip_id_alias, received_chip_id);
            }
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            continue;
        }

        if (crc5(buffer + 2, received - 2) != 0) {
            ESP_LOGW(TAG, "Checksum failed on CHIP_ID response");
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            continue;
        }

        ESP_LOGI(TAG, "Chip %d detected: CORE_NUM: 0x%02x ADDR: 0x%02x", chip_counter, buffer[4], buffer[5]);

        chip_counter++;
    }    
    
    if (chip_counter != asic_count) {
        ESP_LOGE(TAG, "%i chip(s) detected on the chain, expected %i", chip_counter, asic_count);

        char asic_indices[64];
        if (chip_counter < asic_count) {
            format_asic_indices(asic_indices, sizeof(asic_indices), chip_counter, asic_count);
            snprintf(asic_chain_error, sizeof(asic_chain_error), "ASIC %s not found", asic_indices);
        } else {
            format_asic_indices(asic_indices, sizeof(asic_indices), asic_count, chip_counter);
            snprintf(asic_chain_error, sizeof(asic_chain_error), "Unexpected ASIC %s detected", asic_indices);
        }
        ESP_LOGE(TAG, "%s", asic_chain_error);

        return 0;
    }

    return chip_counter;
}

int count_asic_chips(uint16_t asic_count, uint16_t chip_id, int chip_id_response_length)
{
    return count_asic_chips_with_id_alias(asic_count, chip_id, chip_id, chip_id_response_length);
}

esp_err_t receive_work(uint8_t * buffer, int buffer_size, uint64_t *out_timestamp_us)
{
    int received = SERIAL_rx(buffer, buffer_size, 10000);
    if (out_timestamp_us) {
        *out_timestamp_us = esp_timer_get_time();
    }

    if (received < 0) {
        ESP_LOGE(TAG, "UART error in serial RX");
        return ESP_FAIL;
    }

    if (received == 0) {
        ESP_LOGD(TAG, "UART timeout in serial RX");
        return ESP_FAIL;
    }

    if (received != buffer_size) {
        ESP_LOGE(TAG, "Invalid response length %i", received);
        ESP_LOG_BUFFER_HEX(TAG, buffer, received);
        SERIAL_clear_buffer();
        return ESP_FAIL;
    }

    uint16_t received_preamble = (buffer[0] << 8) | buffer[1];
    if (received_preamble != PREAMBLE) {
        ESP_LOGE(TAG, "Preamble mismatch: got 0x%04x, expected 0x%04x", received_preamble, PREAMBLE);
        ESP_LOG_BUFFER_HEX(TAG, buffer, received);
        SERIAL_clear_buffer();
        return ESP_FAIL;
    }

    if (crc5(buffer + 2, buffer_size - 2) != 0) {
        ESP_LOGE(TAG, "Checksum failed on response");        
        ESP_LOG_BUFFER_HEX(TAG, buffer, received);
        SERIAL_clear_buffer();
        return ESP_FAIL;
    }

    return ESP_OK;
}

void get_difficulty_mask(uint8_t difficulty_power, uint8_t *job_difficulty_mask)
{
    // Register 0x14 (TICKET_MASK) filters candidate nonces by requiring `difficulty_power` leading zero bits.
    // The mask is a contiguous bitmask (1 << power) - 1, representing difficulty 2^power.
    uint32_t mask = (difficulty_power >= 32) ? 0xFFFFFFFFU : ((1U << difficulty_power) - 1);

    job_difficulty_mask[0] = 0x00;
    job_difficulty_mask[1] = 0x14; // TICKET_MASK

    job_difficulty_mask[2] = _reverse_bits((mask >> 24) & 0xFF);
    job_difficulty_mask[3] = _reverse_bits((mask >> 16) & 0xFF);
    job_difficulty_mask[4] = _reverse_bits((mask >>  8) & 0xFF);
    job_difficulty_mask[5] = _reverse_bits( mask        & 0xFF);
}

uint8_t calculate_effective_asic_difficulty(float expected_ghs, float interval_s, double pool_difficulty)
{
    if (interval_s <= 0.0f) {
        interval_s = DEFAULT_SHARE_INTERVAL_S;
    }

    uint8_t power = MIN_ASIC_DIFFICULTY_POWER;
    if (expected_ghs > 0.0f) {
        float raw_diff = expected_ghs * 1e9f * interval_s / NONCE_SPACE;
        if (raw_diff > 1U << MIN_ASIC_DIFFICULTY_POWER) {
            int p = MIN_ASIC_DIFFICULTY_POWER;
            while (p < 31 && (1U << (p + 1)) <= raw_diff + 1e-4f) {
                p++;
            }
            float lower = 1U << p;
            float upper = 1U << (p + 1);
            power = (raw_diff - lower > upper - raw_diff) ? p + 1 : p;
        }
    }

    if (pool_difficulty > 0.0) {
        while (power > MIN_ASIC_DIFFICULTY_POWER && (1U << power) > pool_difficulty + 1e-6) {
            power--;
        }
    }

    return power;
}

float calculate_bm_timeout_ms(float frequency_mhz, size_t asic_count, size_t small_cores, size_t cores, size_t version_size, float timeout_percent, float default_time_ms)
{
    if (asic_count <= 0)
        return default_time_ms;

    // Round up to the nearest power of 2 some asic constants
    int cores_up = _next_power_of_two(cores);
    int small_cores_up = _next_power_of_two(small_cores);
    int asic_count_up = _next_power_of_two(asic_count);

    if (small_cores_up < cores_up || frequency_mhz <= 0.0f)
        return default_time_ms;

    // Calulate the time to scan the full nonce * version space
    // effectively how many iterations we have to do
    // First we remove the paralell nonces/versions
    // then we end up with `time = space / frequency`
    float midstates = (float)small_cores_up / cores_up;
    float serial_versions = version_size / midstates;
    float serial_nonces = NONCE_SPACE / cores_up / asic_count_up;
    float fullspace_timeout_ms = serial_versions * serial_nonces / (frequency_mhz * 1000.0f);

    if (fullspace_timeout_ms <= 0.0f)
        return default_time_ms;

    return timeout_percent * fullspace_timeout_ms;
}

bool bm13xx_decode_job_result(asic_job_store_t *store,
                              uint8_t small_core_bits,
                              uint16_t address_interval,
                              uint32_t wire_nonce,
                              uint8_t wire_chip_slot,
                              uint16_t wire_version,
                              task_result *result)
{
    if (address_interval == 0 || result == NULL) {
        return false;
    }

    uint8_t slot = bm_decode_wire_job_slot(wire_chip_slot, small_core_bits);
    uint8_t small_core_id = bm_decode_wire_sub_id(wire_chip_slot, small_core_bits);

    if (!asic_job_store_lookup(store, slot, &result->job)) {
        return false;
    }

    uint32_t nonce_h = ntohl(wire_nonce);
    uint8_t asic_nr = (uint8_t)(((nonce_h >> 17) & 0xff) / address_interval);
    uint8_t core_id = (uint8_t)((nonce_h >> 25) & 0x7f);
    uint32_t version_bits = ((uint32_t)ntohs(wire_version) << 13);

    result->rolled_version = result->job.version | version_bits;
    result->nonce = wire_nonce;
    result->asic_nr = asic_nr;
    result->core_id = core_id;
    result->small_core_id = small_core_id;

    return true;
}

bool bm1397_decode_job_result(asic_job_store_t *store,
                              uint16_t address_interval,
                              uint32_t wire_nonce,
                              uint8_t wire_chip_slot,
                              task_result *result)
{
    if (address_interval == 0 || result == NULL) {
        return false;
    }

    uint8_t slot = bm_decode_wire_job_slot(wire_chip_slot, BM1397_MIDSTATE_BITS);
    uint8_t rx_midstate_index = bm_decode_wire_sub_id(wire_chip_slot, BM1397_MIDSTATE_BITS);

    if (!asic_job_store_lookup(store, slot, &result->job)) {
        return false;
    }

    uint32_t rolled_version = result->job.version;
    for (int i = 0; i < rx_midstate_index; i++) {
        rolled_version = increment_bitmask(rolled_version, result->job.version_mask);
    }

    uint32_t nonce_h = ntohl(wire_nonce);
    uint8_t asic_nr = (uint8_t)(((nonce_h >> 17) & 0xff) / address_interval);
    uint8_t core_id = (uint8_t)((nonce_h >> 25) & 0x7f);

    result->rolled_version = rolled_version;
    result->nonce = wire_nonce;
    result->asic_nr = asic_nr;
    result->core_id = core_id;
    result->small_core_id = (uint8_t)(wire_chip_slot & 0x0f);

    return true;
}

void bm13xx_clear_work(asic_job_store_t *store)
{
    if (store) {
        asic_job_store_invalidate_all(store);
    }
}

void bm13xx_init_job_slots(asic_job_store_t *store, uint16_t capacity)
{
    if (!store || capacity == 0) {
        return;
    }
    if (store->entries != NULL) {
        bm13xx_destroy_job_slots(store);
    }
    asic_job_slot_entry_t *entries = (asic_job_slot_entry_t *)calloc(capacity, sizeof(asic_job_slot_entry_t));
    if (entries == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u Bitmain job slots", capacity);
        return;
    }
    asic_job_store_init(store, entries, capacity);
}

void bm13xx_destroy_job_slots(asic_job_store_t *store)
{
    if (!store) {
        return;
    }
    asic_job_store_destroy(store);
    if (store->entries != NULL) {
        free(store->entries);
        store->entries = NULL;
    }
}
