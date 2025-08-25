#include "unity.h"

#include "serial.h"
#include "bm1397.h"

#include <string.h>

/*
static uint8_t uart_initialized = 0;

TEST_CASE("Check known working midstate + job command", "[bm1397]")
{
    static GlobalState GLOBAL_STATE;

    if (!uart_initialized)
    {
        SERIAL_init();
        uart_initialized = 1;

        BM1397_init(425, 1, 256);

        // read back response
        SERIAL_debug_rx();
    }

    uint8_t work1[146] = {
        0x18, // job id
        0x04, // number of midstates
        0x9B,
        0x04,
        0x4C,
        0x0A, // starting nonce
        0x3A,
        0xAE,
        0x05,
        0x17, // nbits
        0xA0,
        0x84,
        0x73,
        0x64, // ntime
        0x50,
        0xE3,
        0x71,
        0x61, // merkle 4
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x7E,
        0x02,
        0x70,
        0x35,
        0xB1,
        0xAC,
        0xBA,
        0xF2,
        0x3E,
        0xA0,
        0x1A,
        0x52,
        0x73,
        0x44,
        0xFA,
        0xF7,
        0x6A,
        0xB4,
        0x76,
        0xD3,
        0x28,
        0x21,
        0x61,
        0x18,
        0xB7,
        0x76,
        0x0F,
        0x7B,
        0x1B,
        0x22,
        0xD2,
        0x29,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
    };
    job_packet test_job;
    memcpy((uint8_t *)&test_job, work1, 146);

    uint8_t buf[1024];
    memset(buf, 0, 1024);

    BM1397_send_work(&GLOBAL_STATE, &test_job);
    uint16_t received = SERIAL_rx(buf, 9, 20);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(sizeof(struct asic_result), received);

    int i;
    for (i = 0; i < received - 1; i++)
    {
        if (buf[i] == 0xAA && buf[i + 1] == 0x55)
        {
            break;
        }
    }

    struct asic_result nonce;
    memcpy((void *)&nonce, buf + i, sizeof(struct asic_result));
    // expected nonce 9B 04 4C 0A
    TEST_ASSERT_EQUAL_UINT32(0x0a4c049b, nonce.nonce);
    TEST_ASSERT_EQUAL_UINT8(0x18, nonce.job_id & 0xfc);
    TEST_ASSERT_EQUAL_UINT8(2, nonce.job_id & 0x03);
}

*/