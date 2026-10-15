#include "unity.h"
#include "asic_common.h"
#include "bm13xx_test_harness.h"
#include "bm1397_test_harness.h"

TEST_CASE("Check calculate_effective_asic_difficulty", "[asic][difficulty]")
{
    // --- 1. Target difficulty calculation across device hashrates (1 share/s) ---
    // 500 GH/s -> raw_diff ~116.4 -> nearest power of 2 is 128 (power 7)
    TEST_ASSERT_EQUAL_UINT8(7, calculate_effective_asic_difficulty(500.0));
    // 8446 GH/s -> raw_diff ~1966.5 -> nearest power of 2 is 2048 (power 11)
    TEST_ASSERT_EQUAL_UINT8(11, calculate_effective_asic_difficulty(8446.0));
    // 285 GH/s -> raw_diff ~66.4 -> nearest power of 2 is 64 (power 6)
    TEST_ASSERT_EQUAL_UINT8(6, calculate_effective_asic_difficulty(285.0));

    // --- 2. Boundary and edge-case handling for hashrate ---
    // Scaled difficulty for lower hashrates
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(100.0));
    TEST_ASSERT_EQUAL_UINT8(3, calculate_effective_asic_difficulty(50.0));
    TEST_ASSERT_EQUAL_UINT8(2, calculate_effective_asic_difficulty(25.0));
    TEST_ASSERT_EQUAL_UINT8(1, calculate_effective_asic_difficulty(10.0));
    // Floor clamping at 0 (difficulty 1) for low or invalid hashrate
    TEST_ASSERT_EQUAL_UINT8(0, calculate_effective_asic_difficulty(5.0));
    TEST_ASSERT_EQUAL_UINT8(0, calculate_effective_asic_difficulty(0.0));
    TEST_ASSERT_EQUAL_UINT8(0, calculate_effective_asic_difficulty(-100.0));
}

TEST_CASE("Check get_difficulty_mask bit patterns", "[asic][difficulty]")
{
    uint8_t mask[6];

    // Difficulty 8 (power 3): register 0x14, 3 zero bits
    get_difficulty_mask(3, mask);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, mask[1]); // TICKET_MASK register
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[4]);
    TEST_ASSERT_EQUAL_HEX8(0xE0, mask[5]); // 0b11100000 (reverse of 7 = 0b00000111)

    // Difficulty 16 (power 4): register 0x14, 4 zero bits
    get_difficulty_mask(4, mask);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, mask[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[4]);
    TEST_ASSERT_EQUAL_HEX8(0xF0, mask[5]); // 0b11110000 (reverse of 15 = 0b00001111)

    // Difficulty 256 (power 8): register 0x14, 8 zero bits
    get_difficulty_mask(8, mask);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, mask[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[4]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mask[5]); // 0b11111111 (reverse of 0xFF)

    // Difficulty 8192 (power 13): register 0x14, 13 zero bits (8 bits in byte 5, 5 bits in byte 4)
    get_difficulty_mask(13, mask);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, mask[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[3]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, mask[4]); // 0b11111000 (reverse of 0x1F = 0b00011111)
    TEST_ASSERT_EQUAL_HEX8(0xFF, mask[5]);
}

TEST_CASE("Check BM13xx and BM1397 set_difficulty serial packet generation", "[asic][difficulty]")
{
    // Wire command packet for difficulty 256 (power 8):
    // Header 0x55 0xAA, TYPE_CMD | GROUP_ALL | CMD_WRITE (0x51), length 0x09,
    // register 0x14, 4-byte mask 0x00 0x00 0x00 0xFF, CRC5 0x08.
    static const uint8_t expected_packet_power8[11] = {
        0x55, 0xAA, 0x51, 0x09, 0x00, 0x14, 0x00, 0x00, 0x00, 0xFF, 0x08
    };
    // Wire command packet for difficulty 16 (power 4):
    // Header 0x55 0xAA, TYPE_CMD | GROUP_ALL | CMD_WRITE (0x51), length 0x09,
    // register 0x14, 4-byte mask 0x00 0x00 0x00 0xF0, CRC5 0x1E.
    static const uint8_t expected_packet_power4[11] = {
        0x55, 0xAA, 0x51, 0x09, 0x00, 0x14, 0x00, 0x00, 0x00, 0xF0, 0x1E
    };

    for (size_t i = 0; i < BM13XX_HARNESS_DRIVER_COUNT; ++i) {
        bm13xx_harness_begin();
        bm13xx_harness_clear_packets();

        bm13xx_harness_drivers[i].set_difficulty(8);

        TEST_ASSERT_EQUAL_UINT(1, bm13xx_harness_packet_count());
        const bm13xx_harness_packet_t *packet = bm13xx_harness_packet(0);
        TEST_ASSERT_EQUAL_UINT(11, packet->length);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_packet_power8, packet->bytes, 11);

        bm13xx_harness_clear_packets();
        bm13xx_harness_drivers[i].set_difficulty(4);

        TEST_ASSERT_EQUAL_UINT(1, bm13xx_harness_packet_count());
        packet = bm13xx_harness_packet(0);
        TEST_ASSERT_EQUAL_UINT(11, packet->length);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_packet_power4, packet->bytes, 11);

        bm13xx_harness_end();
    }

    {
        bm1397_harness_begin();
        bm1397_harness_clear_packets();

        bm1397_harness_driver.set_difficulty(8);

        TEST_ASSERT_EQUAL_UINT(1, bm1397_harness_packet_count());
        const bm1397_harness_packet_t *packet = bm1397_harness_packet(0);
        TEST_ASSERT_EQUAL_UINT(11, packet->length);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_packet_power8, packet->bytes, 11);

        bm1397_harness_clear_packets();
        bm1397_harness_driver.set_difficulty(4);

        TEST_ASSERT_EQUAL_UINT(1, bm1397_harness_packet_count());
        packet = bm1397_harness_packet(0);
        TEST_ASSERT_EQUAL_UINT(11, packet->length);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_packet_power4, packet->bytes, 11);

        bm1397_harness_end();
    }
}
