// OLED SSD1306 using the I2C interface
// Written by Larry Bank (bitbank@pobox.com)
// Project started 1/15/2017
//
// The I2C writes (through a file handle) can be single or multiple bytes.
// The write mode stays in effect throughout each call to write()
// To write commands to the OLED controller, start a byte sequence with 0x00,
// to write data, start a byte sequence with 0x40,
// The OLED controller is set to "page mode". This divides the display
// into 8 128x8 "pages" or strips. Each data write advances the output
// automatically to the next address. The bytes are arranged such that the LSB
// is the topmost pixel and the MSB is the bottom.
// The font data comes from another source and must be rotated 90 degrees
// (at init time) to match the orientation of the bits on the display memory.
// A copy of the display memory is maintained by this code so that single pixel
// writes can occur without having to read from the display controller.

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_config.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "oled.h"

#define I2C_TIMEOUT 1000

/*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_NUM 0

extern unsigned char ucSmallFont[];
static int iScreenOffset;            // current write offset of screen data
static unsigned char ucScreen[1024]; // local copy of the image buffer
static int oled_type, oled_flip;

static void write_command(unsigned char);
static esp_err_t write(uint8_t *, uint8_t);

static bool oled_active;

// Initialialize the OLED Screen
bool OLED_init(void)
{
    uint8_t oled32_initbuf[] = {0x00,
                                0xae, // cmd: display off
                                0xd5, // cmd: set display clock
                                0x80,
                                0xa8, // cmd: set multiplex ratio
                                0x1f, // HEIGHT - 1 -> 31
                                0xd3, // cmd: set display offset
                                0x00,
                                0x40, // cmd: Set Display Start Line
                                0x8d,
                                0x14, // cmd: Set Higher Column Start Address for Page Addressing Mode
                                0xa1,
                                0xc8, // cmd: Set COM Output Scan Direction C0/C8
                                0xda, // cmd: Set COM Pins Hardware Configuration
                                0x02, //
                                0x81, // cmd: Set Contrast control
                                0x7f,
                                0xd9, // cmd: Set Pre-Charge Period
                                0xf1,
                                0xdb, // comd: Vcom regulator output
                                0x40,
                                0xa4,  // cmd: display on ram contents
                                0xa6,  // cmd: set normal
                                0xaf}; // cmd: display on
    uint8_t uc[4];

    uint8_t bFlip = nvs_config_get_u16(NVS_CONFIG_FLIP_SCREEN, 1);
    uint8_t bInvert = nvs_config_get_u16(NVS_CONFIG_INVERT_SCREEN, 0);
    oled_active = false;

    // //enable the module
    // GPIO_write(Board_OLED_DISP_ENABLE, 0);
    // DELAY_MS(50);
    // GPIO_write(Board_OLED_DISP_ENABLE, 1);
    // DELAY_MS(50);

    oled_type = OLED_128x32;
    oled_flip = bFlip;

    // /* Call driver init functions */
    // I2C_init();

    // /* Create I2C for usage */
    // I2C_Params_init(&oled_i2cParams);
    // oled_i2cParams.bitRate = I2C_100kHz;
    // oled_i2c = I2C_open(Board_I2C_SSD1306, &oled_i2cParams);

    // if (oled_i2c == NULL) {
    //     return false;
    // }
    oled_active = true;

    write(oled32_initbuf, sizeof(oled32_initbuf));

    if (bInvert) {
        uc[0] = 0;    // command
        uc[1] = 0xa7; // invert command
        write(uc, 2);
    }

    if (bFlip) {   // rotate display 180
        uc[0] = 0; // command
        uc[1] = 0xa0;
        write(uc, 2);
        uc[1] = 0xc0;
        write(uc, 2);
    }
    return true;
}

// Sends a command to turn off the OLED display
// Closes the I2C file handle
void OLED_shutdown()
{

    write_command(0xaE); // turn off OLED
    // I2C_close(oled_i2c);
    // GPIO_write(Board_OLED_DISP_ENABLE, 0); //turn off power
    oled_active = false;
}

// Send a single byte command to the OLED controller
static void write_command(uint8_t c)
{
    uint8_t buf[2];

    buf[0] = 0x00; // command introducer
    buf[1] = c;
    write(buf, 2);
}

static void oledWriteCommand2(uint8_t c, uint8_t d)
{
    uint8_t buf[3];

    buf[0] = 0x00;
    buf[1] = c;
    buf[2] = d;
    write(buf, 3);
}

bool OLED_setContrast(uint8_t ucContrast)
{

    oledWriteCommand2(0x81, ucContrast);
    return true;
}

// Send commands to position the "cursor" to the given
// row and column
static void oledSetPosition(int x, int y)
{
    iScreenOffset = (y * 128) + x;
    if (oled_type == OLED_64x32) // visible display starts at column 32, row 4
    {
        x += 32;            // display is centered in VRAM, so this is always true
        if (oled_flip == 0) // non-flipped display starts from line 4
            y += 4;
    } else if (oled_type == OLED_132x64) // SH1106 has 128 pixels centered in 132
    {
        x += 2;
    }

    write_command(0xb0 | y);                // go to page Y
    write_command(0x00 | (x & 0xf));        // // lower col addr
    write_command(0x10 | ((x >> 4) & 0xf)); // upper col addr
}

// Write a block of pixel data to the OLED
// Length can be anything from 1 to 1024 (whole display)
static void oledWriteDataBlock(uint8_t * ucBuf, int iLen)
{
    uint8_t ucTemp[129];

    ucTemp[0] = 0x40; // data command
    memcpy(&ucTemp[1], ucBuf, iLen);
    write(ucTemp, iLen + 1);
    // Keep a copy in local buffer
    memcpy(&ucScreen[iScreenOffset], ucBuf, iLen);
    iScreenOffset += iLen;
}

// Set (or clear) an individual pixel
// The local copy of the frame buffer is used to avoid
// reading data from the display controller
int OLED_setPixel(int x, int y, uint8_t ucColor)
{
    int i;
    uint8_t uc, ucOld;

    // if (oled_i2c == NULL)
    //     return -1;

    i = ((y >> 3) * 128) + x;
    if (i < 0 || i > 1023) // off the screen
        return -1;
    uc = ucOld = ucScreen[i];
    uc &= ~(0x1 << (y & 7));
    if (ucColor) {
        uc |= (0x1 << (y & 7));
    }
    if (uc != ucOld) // pixel changed
    {
        oledSetPosition(x, y >> 3);
        oledWriteDataBlock(&uc, 1);
    }
    return 0;
}

//
// Draw a string of small (8x8), large (16x24), or very small (6x8)  characters
// At the given col+row
// The X position is in character widths (8 or 16)
// The Y position is in memory pages (8 lines each)
//
int OLED_writeString(int x, int y, char * szMsg)
{
    int i, iLen;
    uint8_t * s;

    // if (oled_i2c == NULL) return -1; // not initialized

    iLen = strlen(szMsg);

    oledSetPosition(x * 6, y);
    if (iLen + x > 21)
        iLen = 21 - x;
    if (iLen < 0)
        return -1;
    for (i = 0; i < iLen; i++) {
        s = &ucSmallFont[(unsigned char) szMsg[i] * 6];
        oledWriteDataBlock(s, 6);
    }

    return 0;
}

// Fill the frame buffer with a byte pattern
// e.g. all off (0x00) or all on (0xff)
int OLED_fill(uint8_t ucData)
{
    int y;
    uint8_t temp[128];
    int iLines, iCols;

    // if (oled_i2c == NULL) return -1; // not initialized

    iLines = (oled_type == OLED_128x32 || oled_type == OLED_64x32) ? 4 : 8;
    iCols = (oled_type == OLED_64x32) ? 4 : 8;

    memset(temp, ucData, 128);
    for (y = 0; y < iLines; y++) {
        oledSetPosition(0, y);                // set to (0,Y)
        oledWriteDataBlock(temp, iCols * 16); // fill with data byte
    }                                         // for y
    return 0;
}

int OLED_clearLine(uint8_t line)
{
    uint8_t temp[128];

    // if (oled_i2c == NULL) return -1; // not initialized
    if (line > 4)
        return -1; // line number too big

    memset(temp, 0, 128);
    oledSetPosition(0, line);      // set to (0,Y)
    oledWriteDataBlock(temp, 128); // fill with data byte

    return 0;
}

bool OLED_status(void)
{
    return oled_active;
}

/**
 * @brief Write a byte to a I2C register
 */
static esp_err_t write(uint8_t * data, uint8_t len)
{
    int ret;

    ret = i2c_master_write_to_device(I2C_MASTER_NUM, 0x3C, data, len, 1000 / portTICK_PERIOD_MS);

    return ret;
}