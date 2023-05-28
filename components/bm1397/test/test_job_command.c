#include "unity.h"

#include "bm1397.h"
#include "serial.h"

#include <string.h>

TEST_CASE("Check known working midstate + job command", "[bm1397]")
{
    init_serial();

    init_BM1397();

    //reset the bm1397
    reset_BM1397();

    //send the init command
    send_read_address();

    //read back response
    debug_serial_rx();

    //send the init commands
    send_init();

    uint8_t work1[50] = {
        0xFD, // job id
        0x01, // number of midstates
        0x00, 0x00, 0x00, 0x00, // starting nonce
        0x3A, 0xAE, 0x05, 0x17, // nbits
        0x1F, 0x80, 0x73, 0x64, // ntime
        0x56, 0x96, 0xFC, 0x6D, // merkle 4
        0xF4, 0xF7, 0x09, 0x8A, 0x04, 0x2E, 0x14, 0x2A, 0xC5, 0xC8, 0x15, 0x78, 0xB9, 0x8B, 0xFC, 0x3C, 0xB3, 0xB2, 0xE7, 0x2A, 0x4E, 0x00, 0x6A, 0xF3, 0x96, 0xA9, 0x3B, 0xF2, 0x22, 0x3F, 0xB1, 0xF7};
    struct job_packet test_job;
    memcpy((uint8_t *) &test_job, work1, 50);

    uint8_t buf[1024];
    memset(buf, 0, 1024);

    send_work(&test_job);
    uint16_t received = serial_rx(buf);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(sizeof(struct nonce_response), received);

    struct nonce_response nonce;
    memcpy((void *) &nonce, buf, sizeof(struct nonce_response));
    // expected nonce CD C0 92 7B
    TEST_ASSERT_EQUAL_UINT32(0x7b92c0cd, nonce.nonce);

    //reset the bm1397
    reset_BM1397();
}