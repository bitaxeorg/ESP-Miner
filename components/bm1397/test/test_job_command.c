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
        0xA3, // job id
        0x01, // number of midstates
        0x00, 0x00, 0x00, 0x00, // starting nonce
        0x3A, 0xAE, 0x05, 0x17, // nbits
        0xA0, 0x84, 0x73, 0x64, // ntime
        0x50, 0xE3, 0x71, 0x61, // merkle 4
        0x7E, 0x02, 0x70, 0x35, 0xB1, 0xAC, 0xBA, 0xF2, 0x3E, 0xA0, 0x1A, 0x52, 0x73, 0x44, 0xFA, 0xF7, 0x6A, 0xB4, 0x76, 0xD3, 0x28, 0x21, 0x61, 0x18, 0xB7, 0x76, 0x0F, 0x7B, 0x1B, 0x22, 0xD2, 0x29};
    struct job_packet test_job;
    memcpy((uint8_t *) &test_job, work1, 50);

    uint8_t buf[1024];
    memset(buf, 0, 1024);

    send_work(&test_job);
    uint16_t received = serial_rx(buf);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(sizeof(struct nonce_response), received);

    struct nonce_response nonce;
    memcpy((void *) &nonce, buf, sizeof(struct nonce_response));
    // expected nonce 9B 04 4C 0A
    TEST_ASSERT_EQUAL_UINT32(0x0a4c049b, nonce.nonce);

    //reset the bm1397
    reset_BM1397();
}