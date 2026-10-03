#include "unity.h"
#include "asic_common.h"

TEST_CASE("Check calculate_effective_asic_difficulty", "[asic][difficulty]")
{
    // --- 1. Target difficulty calculation (unconstrained pool difficulty <= 0.0) ---
    // Normal mining mode (default 5.0s interval)
    // 500 GH/s -> raw_diff ~582.1 -> nearest power of 2 is 512
    TEST_ASSERT_EQUAL_DOUBLE(512.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 0.0));
    // 8446 GH/s -> raw_diff ~9832.4 -> nearest power of 2 is 8192
    TEST_ASSERT_EQUAL_DOUBLE(8192.0, calculate_effective_asic_difficulty(8446.0, DEFAULT_SHARE_INTERVAL_S, 0.0));
    // 285 GH/s -> raw_diff ~331.8 -> nearest power of 2 is 256
    TEST_ASSERT_EQUAL_DOUBLE(256.0, calculate_effective_asic_difficulty(285.0, DEFAULT_SHARE_INTERVAL_S, 0.0));

    // Self-test interval (0.125s interval)
    // 285 GH/s -> raw_diff ~8.29 -> nearest power of 2 is 8
    TEST_ASSERT_EQUAL_DOUBLE(8.0, calculate_effective_asic_difficulty(285.0, SELF_TEST_SHARE_INTERVAL_S, 0.0));
    // 500 GH/s -> raw_diff ~14.55 -> nearest power of 2 is 16
    TEST_ASSERT_EQUAL_DOUBLE(16.0, calculate_effective_asic_difficulty(500.0, SELF_TEST_SHARE_INTERVAL_S, 0.0));
    // 8446 GH/s -> raw_diff ~245.8 -> nearest power of 2 is 256
    TEST_ASSERT_EQUAL_DOUBLE(256.0, calculate_effective_asic_difficulty(8446.0, SELF_TEST_SHARE_INTERVAL_S, 0.0));

    // --- 2. Boundary and edge-case handling for hashrate and interval ---
    // Floor clamping at MIN_ASIC_DIFFICULTY (8.0) for low/invalid hashrate
    TEST_ASSERT_EQUAL_DOUBLE(8.0, calculate_effective_asic_difficulty(10.0, DEFAULT_SHARE_INTERVAL_S, 0.0));
    TEST_ASSERT_EQUAL_DOUBLE(8.0, calculate_effective_asic_difficulty(100.0, SELF_TEST_SHARE_INTERVAL_S, 0.0));
    TEST_ASSERT_EQUAL_DOUBLE(8.0, calculate_effective_asic_difficulty(0.0, DEFAULT_SHARE_INTERVAL_S, 0.0));
    TEST_ASSERT_EQUAL_DOUBLE(8.0, calculate_effective_asic_difficulty(-100.0, DEFAULT_SHARE_INTERVAL_S, 0.0));

    // Fallback when interval_s <= 0.0 (defaults to DEFAULT_SHARE_INTERVAL_S = 5.0s)
    TEST_ASSERT_EQUAL_DOUBLE(512.0, calculate_effective_asic_difficulty(500.0, 0.0, 0.0));
    TEST_ASSERT_EQUAL_DOUBLE(512.0, calculate_effective_asic_difficulty(500.0, -1.0, 0.0));

    // --- 3. Pool difficulty reconciliation (500 GH/s @ 5.0s yields target diff 512) ---
    // Case 1: Pool diff is higher than target -> caps at target diff (512)
    TEST_ASSERT_EQUAL_DOUBLE(512.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 2048.0));
    TEST_ASSERT_EQUAL_DOUBLE(512.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 1024.0));

    // Case 2: Pool diff matches target diff (512)
    TEST_ASSERT_EQUAL_DOUBLE(512.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 512.0));

    // Case 3: Pool diff is lower power of 2 -> scales down to pool diff
    TEST_ASSERT_EQUAL_DOUBLE(256.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 256.0));
    TEST_ASSERT_EQUAL_DOUBLE(128.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 128.0));

    // Case 4: Pool diff is non-power-of-2 -> floors to largest power of 2 <= pool diff
    TEST_ASSERT_EQUAL_DOUBLE(256.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 300.0));
    TEST_ASSERT_EQUAL_DOUBLE(128.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 200.0));
    TEST_ASSERT_EQUAL_DOUBLE(64.0,  calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 100.0));

    // Case 5: Pool diff below hardware floor (MIN_ASIC_DIFFICULTY = 8.0) -> clamps to 8.0
    TEST_ASSERT_EQUAL_DOUBLE(8.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 4.0));
    TEST_ASSERT_EQUAL_DOUBLE(8.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 1.0));
    TEST_ASSERT_EQUAL_DOUBLE(8.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 7.9));

    // Case 6: Pool diff negative -> defaults to target diff (512)
    TEST_ASSERT_EQUAL_DOUBLE(512.0, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, -1.0));
}

TEST_CASE("Check get_difficulty_mask bit patterns", "[asic][difficulty]")
{
    uint8_t mask[6];

    // Difficulty 8: register 0x14, 3 zero bits
    get_difficulty_mask(8.0, mask);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, mask[1]); // TICKET_MASK register
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[4]);
    TEST_ASSERT_EQUAL_HEX8(0xE0, mask[5]); // 0b11100000 (reverse of 7 = 0b00000111)

    // Difficulty 256: register 0x14, 8 zero bits
    get_difficulty_mask(256.0, mask);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, mask[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[4]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mask[5]); // 0b11111111 (reverse of 0xFF)

    // Difficulty 8192 (2^13): register 0x14, 13 zero bits (8 bits in byte 5, 5 bits in byte 4)
    get_difficulty_mask(8192.0, mask);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, mask[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, mask[3]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, mask[4]); // 0b11111000 (reverse of 0x1F = 0b00011111)
    TEST_ASSERT_EQUAL_HEX8(0xFF, mask[5]);
}
