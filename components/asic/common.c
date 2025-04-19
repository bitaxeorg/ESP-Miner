#include <string.h>
#include <stdbool.h>

#include "common.h"
#include "serial.h"
#include "esp_log.h"
#include "crc.h"

#define PREAMBLE 0xAA55

static const char * TAG = "common";

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

int count_asic_chips(uint16_t asic_count, uint16_t chip_id, int chip_id_response_length)
{
    uint8_t buffer[11] = {0};

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
        if (received_chip_id != chip_id) {
            ESP_LOGW(TAG, "CHIP_ID response mismatch: expected 0x%04x, got 0x%04x", chip_id, received_chip_id);
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
        ESP_LOGW(TAG, "%i chip(s) detected on the chain, expected %i", chip_counter, asic_count);
    }

    return chip_counter;
}

void shift_buffer_left(uint8_t *buffer, size_t len) 
{
    if (len < 1) return;
    memmove(buffer, buffer + 1, len - 1);
}

int find_preamble_offset(uint8_t *buffer, int buffer_size) 
{
    int preamble_offset  = -1;
    for (int i = 0; i < buffer_size; i++) {
        uint16_t received_preamble = (buffer[i] << 8) | buffer[i+1];
        if (received_preamble == PREAMBLE)  {
            preamble_offset = i;
            break;
        }
    }

    return preamble_offset; // -1 not found, >=0 location
}

esp_err_t serial_alignment(uint8_t * buffer, int buffer_size, int preamble_offset) 
{
    uint8_t reserve_buf[11] = {0}; // 11 is the largest result message from bm13xx chips
    int reserve_received = SERIAL_rx(reserve_buf, preamble_offset, 10);
    bool reserve_buf_size_test_ok = reserve_received == preamble_offset;

    if (reserve_buf_size_test_ok) {
        shift_buffer_left(buffer, preamble_offset);
        for (int i=0; i < preamble_offset; i++) buffer[buffer_size-preamble_offset+i] = reserve_buf[i];
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t receive_work(uint8_t * buffer, int buffer_size)
{
    int received = SERIAL_rx(buffer, buffer_size, 10000);

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

    int preamble_offset = find_preamble_offset(buffer, buffer_size);
    if (preamble_offset == -1) {
        ESP_LOGE(TAG, "Preamble not found");
        ESP_LOG_BUFFER_HEX(TAG, buffer, received);
        SERIAL_clear_buffer();
        return ESP_FAIL;
    }

    if (preamble_offset > 0) {
        ESP_LOGW(TAG, "Non zero preamble located at %i", preamble_offset);
        if (serial_alignment(buffer, buffer_size, preamble_offset) == ESP_FAIL) {
            ESP_LOGE(TAG, "Serial alignment recovery failed");
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            SERIAL_clear_buffer();
            return ESP_FAIL;
        }
    }

    if (crc5(buffer + 2, buffer_size - 2) != 0) {
        ESP_LOGE(TAG, "Checksum failed on response");        
        ESP_LOG_BUFFER_HEX(TAG, buffer, received);
        SERIAL_clear_buffer();
        return ESP_FAIL;
    }

    return ESP_OK;
}