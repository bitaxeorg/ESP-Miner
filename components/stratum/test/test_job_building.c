#include "unity.h"
#include "mining_test_bindings.h"
#include "mining.h"
#include "utils.h"

#include <string.h>

static void assert_hash(const char *hex, const uint8_t actual[32])
{
    uint8_t expected[32];
    TEST_ASSERT_EQUAL_UINT32(32, hex2bin(hex, expected, sizeof(expected)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, sizeof(expected));
}

TEST_CASE("coinbase hash keeps stack and heap boundaries byte exact",
          "[mining][job-building]")
{
    static uint8_t bytes[1025];
    uint8_t hash[32];
    memset(bytes, 0xa5, sizeof(bytes));

    mining_allocator_fault_injector_reset(0);
    calculate_coinbase_tx_hash_bin(bytes, 1024, NULL, 0, NULL, 0, NULL, 0, hash);
    TEST_ASSERT_EQUAL_UINT32(0, mining_allocator_fault_injector_calls());
    assert_hash("68340791fcd0a423a34a6bbf7794ccf983f0290d46c2c74990cde7e14c1b2dea", hash);

    calculate_coinbase_tx_hash_bin(bytes, 1025, NULL, 0, NULL, 0, NULL, 0, hash);
    TEST_ASSERT_EQUAL_UINT32(1, mining_allocator_fault_injector_calls());
    TEST_ASSERT_EQUAL_UINT32(1025, mining_allocator_fault_injector_last_size());
    assert_hash("6456e426406ca41a386e3ff3fa13b02fb4ef606512de6deea88a42333dc105c4", hash);

    // Four non-empty segments produce exactly the same 1,025-byte message.
    calculate_coinbase_tx_hash_bin(bytes, 1000, bytes, 8, bytes, 8, bytes, 9, hash);
    TEST_ASSERT_EQUAL_UINT32(2, mining_allocator_fault_injector_calls());
    assert_hash("6456e426406ca41a386e3ff3fa13b02fb4ef606512de6deea88a42333dc105c4", hash);
}

TEST_CASE("coinbase allocation failure preserves zero output and next-call recovery",
          "[mining][job-building]")
{
    static uint8_t bytes[1025];
    uint8_t hash[32];
    memset(bytes, 0xa5, sizeof(bytes));
    memset(hash, 0xff, sizeof(hash));

    mining_allocator_fault_injector_reset(1);
    calculate_coinbase_tx_hash_bin(bytes, sizeof(bytes), NULL, 0, NULL, 0, NULL, 0, hash);
    TEST_ASSERT_EQUAL_UINT32(1, mining_allocator_fault_injector_calls());
    assert_hash("0000000000000000000000000000000000000000000000000000000000000000", hash);
    calculate_coinbase_tx_hash_bin(bytes, sizeof(bytes), NULL, 0, NULL, 0, NULL, 0, hash);
    TEST_ASSERT_EQUAL_UINT32(2, mining_allocator_fault_injector_calls());
    assert_hash("6456e426406ca41a386e3ff3fa13b02fb4ef606512de6deea88a42333dc105c4", hash);

    // The allocation-failure guard also permits no output buffer.
    mining_allocator_fault_injector_reset(1);
    calculate_coinbase_tx_hash_bin(bytes, sizeof(bytes), NULL, 0, NULL, 0, NULL, 0, NULL);
    TEST_ASSERT_EQUAL_UINT32(1, mining_allocator_fault_injector_calls());
    mining_allocator_fault_injector_reset(0);
}
