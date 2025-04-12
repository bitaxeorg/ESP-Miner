#include <string.h>
#include <stdbool.h>

#include "common.h"
#include "serial.h"
#include "esp_log.h"
#include "crc.h"

#define PREAMBLE 0xAA55

static const char * TAG = "common";

# define XTAL_OSC_MHZ 25 // related to setting the nonce space

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



float limit_percent(float percent,float max) {
    // Limit percent 
    //
    // uses limit if over limit
    
    if (percent> max) percent = max;
    return percent;
}

float calculate_cno_interval(int chips) {
    // Register CNO Chip Nonce Offset
    //          
    // is a optional nonce offset for bm1370 for chips in a chain
    // it causes the chip to start at a paritcular location byte1
    // It has more precision than address interval due to more bits being available
    // Note: CNO overrides the size set by address interval

    // address interval = 0x100 / _largest_power_of_two(chips) << 8 (8 bit precision)
    // cno              = 0x10000 / _largest_power_of_two(chips)    (16 bit precision)

    int cno_interval_max = 0x10000;

    // a float is kept as it will be multiplied by the chip address when sending the commands to chips
    float cno_interval = (float)(cno_interval_max/(float)chips);
    return cno_interval;
}

float calculate_fully_reserved_space(int big_cores, int address_interval, int cno_interval, int freq, float nonce_percent) {
    // Nonce Size for a particular setting
    // 
    // Calculates the  size of the distrobuted space
    // This function enables the time and the hcn register to be set properly 
    big_cores = _largest_power_of_two(big_cores);

    uint32_t max_nonce_range = 0xffffffff;
    uint32_t chain_reserve = 0x100;
    uint32_t chain_reserve16 = chain_reserve << 8;
    uint32_t address_interval16 = address_interval << 8;
    uint32_t fully_divided_space = max_nonce_range/(uint32_t)big_cores/chain_reserve16;

    // address interval calculation
    float fully_reserved_space = (float)fully_divided_space * (float)address_interval16;

    // cno_interval overrides address interval calc
    if (cno_interval > 0) fully_reserved_space = fully_divided_space * cno_interval;

    return fully_reserved_space;
}

int calculate_version_rolling_hcn(int big_cores, int address_interval, int cno_interval, int freq, float nonce_percent) {
    // Register HCN Hash Counting Number
    //          
    // Calulates the nonce size for version rolling chips
    // It signifies to the chip when generate the next version and restart the nonce range
    // This function ensures HCN does not cause duplicates
    // Warning: HCN can cause duplicates if set too large if you decide not to use this function.

    float fully_reserved_space = calculate_fully_reserved_space(big_cores,address_interval,cno_interval,freq,nonce_percent);

    big_cores = _largest_power_of_two(big_cores);
    
    int hcn = (int)(nonce_percent * fully_reserved_space * ((float)XTAL_OSC_MHZ / (float)freq) / 2.0 );
    ESP_LOGI("common.h", "Chip setting freq=%i addr_interval=%i cores=%i cno=%i noncep=%f size=%f",freq,address_interval,big_cores,cno_interval,nonce_percent,fully_reserved_space);
    
    return hcn;
}

float calculate_timeout_ms(int big_cores,int address_interval, int freq, float cno_interval, float nonce_percent, float timeout_percent,int versions_per_core) {
    // Timeout 
    // 
    // Calculates the timeout based on control measures prodvided
    // Dynamically adjusts time based on nonce size and version size

    float fully_reserved_space = calculate_fully_reserved_space(big_cores,address_interval,cno_interval,freq,nonce_percent);

    // This is the total size in parralell (versions and nonces)
    float total_nonce_version_size_per_core = (float)versions_per_core * fully_reserved_space;

    float timeout_s = total_nonce_version_size_per_core/(float)freq/1000/1000;
    float timeout_ms =  timeout_s * 1000;

    // Finally the timeout percent is applied
    return timeout_ms*timeout_percent;
}
