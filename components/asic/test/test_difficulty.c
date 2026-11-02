#include "unity.h"
#include "asic_common.h"

TEST_CASE("Check calculate_effective_asic_difficulty", "[asic][difficulty]")
{
    // --- 1. Target difficulty calculation (unconstrained pool difficulty <= 0.0) ---
    // Normal mining mode (default 2.0s interval)
    // 500 GH/s -> raw_diff ~232.8 -> nearest power of 2 is 256 (power 8)
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 0.0));
    // 8446 GH/s -> raw_diff ~3933.0 -> nearest power of 2 is 4096 (power 12)
    TEST_ASSERT_EQUAL_UINT8(12, calculate_effective_asic_difficulty(8446.0, DEFAULT_SHARE_INTERVAL_S, 0.0));
    // 285 GH/s -> raw_diff ~132.7 -> nearest power of 2 is 128 (power 7)
    TEST_ASSERT_EQUAL_UINT8(7, calculate_effective_asic_difficulty(285.0, DEFAULT_SHARE_INTERVAL_S, 0.0));

    // Self-test interval (0.125s interval)
    // 285 GH/s -> raw_diff ~8.29 <= MIN_ASIC_DIFFICULTY (16) -> clamps to power 4 (16)
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(285.0, SELF_TEST_SHARE_INTERVAL_S, 0.0));
    // 500 GH/s -> raw_diff ~14.55 <= MIN_ASIC_DIFFICULTY (16) -> clamps to power 4 (16)
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(500.0, SELF_TEST_SHARE_INTERVAL_S, 0.0));
    // 8446 GH/s -> raw_diff ~245.8 -> nearest power of 2 is 256 (power 8)
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(8446.0, SELF_TEST_SHARE_INTERVAL_S, 0.0));

    // --- 2. Boundary and edge-case handling for hashrate and interval ---
    // Floor clamping at MIN_ASIC_DIFFICULTY_POWER (4 -> 16) for low/invalid hashrate
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(10.0, DEFAULT_SHARE_INTERVAL_S, 0.0));
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(100.0, SELF_TEST_SHARE_INTERVAL_S, 0.0));
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(0.0, DEFAULT_SHARE_INTERVAL_S, 0.0));
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(-100.0, DEFAULT_SHARE_INTERVAL_S, 0.0));

    // Fallback when interval_s <= 0.0 (defaults to DEFAULT_SHARE_INTERVAL_S = 2.0s -> power 8)
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(500.0, 0.0, 0.0));
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(500.0, -1.0, 0.0));

    // --- 3. Pool difficulty reconciliation (500 GH/s @ 2.0s yields target power 8 / diff 256) ---
    // Case 1: Pool diff is higher than target -> caps at target power 8 (256)
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 2048.0));
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 512.0));

    // Case 2: Pool diff matches target diff (256 -> power 8)
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 256.0));

    // Case 3: Pool diff is lower power of 2 -> scales down to pool diff power
    TEST_ASSERT_EQUAL_UINT8(7, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 128.0));
    TEST_ASSERT_EQUAL_UINT8(6, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 64.0));
    TEST_ASSERT_EQUAL_UINT8(5, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 32.0));

    // Case 4: Pool diff is non-power-of-2 -> floors to largest power of 2 <= pool diff
    TEST_ASSERT_EQUAL_UINT8(7, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 200.0));
    TEST_ASSERT_EQUAL_UINT8(6, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 100.0));
    TEST_ASSERT_EQUAL_UINT8(5, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 50.0));

    // Case 5: Pool diff below hardware floor (MIN_ASIC_DIFFICULTY_POWER = 4) -> clamps to 4
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 8.0));
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 4.0));
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 1.0));
    TEST_ASSERT_EQUAL_UINT8(4, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 15.9));

    // Case 6: Pool diff negative -> defaults to target diff power (8)
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, -1.0));

    // Case 7: Floating-point precision near power of 2
    TEST_ASSERT_EQUAL_UINT8(8, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 255.9999999));
    TEST_ASSERT_EQUAL_UINT8(7, calculate_effective_asic_difficulty(500.0, DEFAULT_SHARE_INTERVAL_S, 127.9999999));
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
