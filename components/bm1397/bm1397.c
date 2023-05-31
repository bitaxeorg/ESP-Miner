#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "serial.h"
#include "bm1397.h"
#include "utils.h"
#include "crc.h"

#define SLEEP_TIME 20
#define FREQ_MULT 25.0

#define BM1397_FREQUENCY CONFIG_BM1397_FREQUENCY

static const char *TAG = "bm1397";

static void send_hash_frequency(float frequency);

//reset the BM1397 via the RTS line
void reset_BM1397(void) {
    gpio_set_level(BM1397_RST_PIN, 0);

    //delay for 100ms
    vTaskDelay(100 / portTICK_RATE_MS);

    //set the gpio pin high
    gpio_set_level(BM1397_RST_PIN, 1);

    //delay for 100ms
    vTaskDelay(100 / portTICK_RATE_MS);

}

void init_BM1397(void) {
    ESP_LOGI(TAG, "Initializing BM1397");

    gpio_pad_select_gpio(BM1397_RST_PIN);
    gpio_set_direction(BM1397_RST_PIN, GPIO_MODE_OUTPUT);

}

/// @brief 
/// @param ftdi 
/// @param header 
/// @param data 
/// @param len 
void send_BM1397(uint8_t header, uint8_t * data, uint8_t data_len, bool debug) {
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    uint8_t total_length = (packet_type == JOB_PACKET) ? (data_len+6) : (data_len+5);

    //allocate memory for buffer
    unsigned char *buf = malloc(total_length);

    //add the preamble
    buf[0] = 0x55;
    buf[1] = 0xAA;

    //add the header field
    buf[2] = header;

    //add the length field
    buf[3] = (packet_type == JOB_PACKET) ? (data_len+4) : (data_len+3);

    //add the data
    memcpy(buf+4, data, data_len);

    //add the correct crc type
    if (packet_type == JOB_PACKET) {
        uint16_t crc16_total = crc16_false(buf+2, data_len+2);
        buf[4+data_len] = (crc16_total >> 8) & 0xFF;
        buf[5+data_len] = crc16_total & 0xFF;
    } else {
        buf[4+data_len] = crc5(buf+2, data_len+2);
    }

    //send serial data
    send_serial(buf, total_length, debug);

    free(buf);
}

void send_read_address(void) {

    unsigned char read_address[2] = {0x00, 0x00};
    //send serial data
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_READ), read_address, 2, false);
}

void send_chain_inactive(void) {

    unsigned char read_address[2] = {0x00, 0x00};
    //send serial data
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2, false);
}

void set_chip_address(uint8_t chipAddr) {

    unsigned char read_address[2] = {chipAddr, 0x00};
    //send serial data
    send_BM1397((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2, false);
}

void send_init(void) {

    //send serial data
    vTaskDelay(SLEEP_TIME / portTICK_RATE_MS);
    send_chain_inactive();

    set_chip_address(0x00);

    unsigned char init[6] = {0x00, 0x80, 0x00, 0x00, 0x00, 0x00}; //init1 - clock_order_control0
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init, 6, false);

    unsigned char init2[6] = {0x00, 0x84, 0x00, 0x00, 0x00, 0x00}; //init2 - clock_order_control1
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init2, 6, false);

    unsigned char init3[9] = {0x00, 0x20, 0x00, 0x00, 0x00, 0x01}; //init3 - ordered_clock_enable
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init3, 6, false);

    unsigned char init4[9] = {0x00, 0x3C, 0x80, 0x00, 0x80, 0x74}; //init4 - init_4_?
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init4, 6, false);

    unsigned char set_ticket[9] = {0x00, 0x14, 0x00, 0x00, 0x00, 0xff}; //set_ticket - ticket_mask
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), set_ticket, 6, false);

    unsigned char init5[9] = {0x00, 0x68, 0xC0, 0x70, 0x01, 0x11}; //init5 - pll3_parameter
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init5, 6, false);

    unsigned char init5_2[9] = {0x00, 0x68, 0xC0, 0x70, 0x01, 0x11}; //init5_2 - pll3_parameter
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init5_2, 6, false);

    unsigned char init6[9] = {0x00, 0x28, 0x06, 0x00, 0x00, 0x0F}; //init6 - fast_uart_configuration
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), init6, 6, false);

    unsigned char baudrate[9] = {0x00, 0x18, 0x00, 0x00, 0x7A, 0x31}; //baudrate - misc_control
    send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), baudrate, 6, false);

    send_hash_frequency(BM1397_FREQUENCY);

    // unsigned char prefreq1[9] = {0x00, 0x70, 0x0F, 0x0F, 0x0F, 0x00}; //prefreq - pll0_divider
    // send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), prefreq1, 6, false);

    // unsigned char freqbuf[9] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x25}; //freqbuf - pll0_parameter
    // send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), freqbuf, 6, false);
}

void send_work(struct job_packet *job) {
    send_BM1397((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t*)job, sizeof(struct job_packet), false);
}


// borrowed from cgminer driver-gekko.c calc_gsf_freq()
static void send_hash_frequency(float frequency) {

	//unsigned char prefreq[] = {0x51, 0x09, 0x00, 0x70, 0x0F, 0x0F, 0x0F, 0x00, 0x00};
    unsigned char prefreq1[9] = {0x00, 0x70, 0x0F, 0x0F, 0x0F, 0x00}; //prefreq - pll0_divider
    // 

	// default 200Mhz if it fails
    unsigned char freqbuf[9] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x25}; //freqbuf - pll0_parameter
	//unsigned char freqbuf[] = {0x51, 0x09, 0x00, 0x08, 0x40, -0xF0, -0x02, -0x65, 0x00};

	float deffreq = 200.0;

	float fa, fb, fc1, fc2, newf;
	float f1, basef, famax = 0xf0, famin = 0x10;
	int i;

    //bound the frequency setting
    if (frequency < 100) {
        f1 = 100;
    } else if (frequency > 800) {
        f1 = 800;
    } else {
        f1 = frequency;
    }


	fb = 2; fc1 = 1; fc2 = 5; // initial multiplier of 10
	if (f1 >= 500) {
		// halve down to '250-400'
		fb = 1;
	} else if (f1 <= 150) {
		// triple up to '300-450'
		fc1 = 3;
	} else if (f1 <= 250) {
		// double up to '300-500'
		fc1 = 2;
	}
	// else f1 is 250-500

	// f1 * fb * fc1 * fc2 is between 2500 and 5000
	// - so round up to the next 25 (freq_mult)
	basef = FREQ_MULT * ceil(f1 * fb * fc1 * fc2 / FREQ_MULT);

	// fa should be between 100 (0x64) and 200 (0xC8)
	fa = basef / FREQ_MULT;

	// code failure ... basef isn't 400 to 6000
	if (fa < famin || fa > famax) {
		newf = deffreq;
	} else {
		freqbuf[3] = (int)fa;
		freqbuf[4] = (int)fb;
		// fc1, fc2 'should' already be 1..15
		freqbuf[5] = (((int)fc1 & 0xf) << 4) + ((int)fc2 & 0xf);

		newf = basef / ((float)fb * (float)fc1 * (float)fc2);
	}

	for (i = 0; i < 2; i++) {
		//cgsleep_ms(10);
        vTaskDelay(10 / portTICK_RATE_MS);
		//compac_send2(compac, prefreq, sizeof(prefreq), 8 * sizeof(prefreq) - 8, "prefreq");
        send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), prefreq1, 6, true);
	}
	for (i = 0; i < 2; i++) {
		//cgsleep_ms(10);
        vTaskDelay(10 / portTICK_RATE_MS);
		//compac_send2(compac, freqbuf, sizeof(freqbuf), 8 * sizeof(freqbuf) - 8, "freq");
        send_BM1397((TYPE_CMD | GROUP_ALL | CMD_WRITE), freqbuf, 6, true);
	}
	//cgsleep_ms(10);
    vTaskDelay(10 / portTICK_RATE_MS);

	// the freq wanted, which 'should' be the same
	// info->asics[0].frequency = frequency;
	// info->frequency = frequency;

	// applog(LOG_INFO, "%d: %s %d - setting frequency to %.2fMHz (%.2f)" " (%.0f/%.0f/%.0f/%.0f)",
	// 	compac->cgminer_id, compac->drv->name, compac->device_id, frequency, newf, fa, fb, fc1, fc2);
    ESP_LOGI(TAG, "Setting Frequency to %.2fMHz (%.2f)", frequency, newf);

}
