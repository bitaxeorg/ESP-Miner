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

TEST_CASE("coinbase streaming hash keeps large inputs byte exact",
          "[mining][job-building]")
{
    static uint8_t bytes[1025];
    uint8_t hash[32];
    memset(bytes, 0xa5, sizeof(bytes));

    mining_allocator_fault_injector_reset(0);
    TEST_ASSERT_TRUE(calculate_coinbase_tx_hash_bin(bytes, 1024, NULL, 0, NULL, 0, NULL, 0, hash));
    TEST_ASSERT_EQUAL_UINT32(0, mining_allocator_fault_injector_calls());
    assert_hash("68340791fcd0a423a34a6bbf7794ccf983f0290d46c2c74990cde7e14c1b2dea", hash);

    TEST_ASSERT_TRUE(calculate_coinbase_tx_hash_bin(bytes, 1025, NULL, 0, NULL, 0, NULL, 0, hash));
    TEST_ASSERT_EQUAL_UINT32(0, mining_allocator_fault_injector_calls());
    assert_hash("6456e426406ca41a386e3ff3fa13b02fb4ef606512de6deea88a42333dc105c4", hash);

    // These counters cover the coinbase buffer, not allocations inside PSA.
    TEST_ASSERT_TRUE(calculate_coinbase_tx_hash_bin(bytes, 1000, bytes, 8, bytes, 8, bytes, 9, hash));
    TEST_ASSERT_EQUAL_UINT32(0, mining_allocator_fault_injector_calls());
    assert_hash("6456e426406ca41a386e3ff3fa13b02fb4ef606512de6deea88a42333dc105c4", hash);
}

TEST_CASE("coinbase hash rejects null destination",
          "[mining][job-building]")
{
    static uint8_t bytes[1025];
    memset(bytes, 0xa5, sizeof(bytes));

    mining_allocator_fault_injector_reset(0);
    mining_hash_fault_injector_reset(1);
    TEST_ASSERT_FALSE(calculate_coinbase_tx_hash_bin(bytes, sizeof(bytes), NULL, 0, NULL, 0, NULL, 0, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, mining_allocator_fault_injector_calls());
    TEST_ASSERT_EQUAL_UINT32(0, mining_hash_fault_injector_calls());
    TEST_ASSERT_EQUAL_UINT32(0, mining_hash_fault_injector_abort_calls());
    mining_hash_fault_injector_reset(0);
}

TEST_CASE("coinbase streaming hash preserves segment order across SHA blocks",
          "[mining][job-building]")
{
    uint8_t bytes[129], hash[32];
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (uint8_t)i;
    }
    TEST_ASSERT_TRUE(calculate_coinbase_tx_hash_bin(bytes, 63, bytes + 63, 1,
        bytes + 64, 3, bytes + 67, 62, hash));
    assert_hash("53d1a95cf285eb4e0e35f20d38b724bc70ea4d857e785fbdc2b636177328a902", hash);
    TEST_ASSERT_TRUE(calculate_coinbase_tx_hash_bin(NULL, 0, bytes, 0,
        NULL, 0, NULL, 0, hash));
    assert_hash("5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456", hash);
}

TEST_CASE("coinbase hash failures clear output and permit next-call recovery",
          "[mining][job-building]")
{
    const uint8_t bytes[] = {1, 2, 3, 4, 5, 6, 7};
    uint8_t hash[32], zero[32] = {0};
    // Fail setup, each of the four updates, finish, and the second hash in turn.
    for (size_t failure_at = 1; failure_at <= 7; ++failure_at) {
        memset(hash, 0xa5, sizeof(hash));
        mining_hash_fault_injector_reset(failure_at);
        TEST_ASSERT_FALSE(calculate_coinbase_tx_hash_bin(bytes, 2, bytes + 2, 1,
            bytes + 3, 2, bytes + 5, 2, hash));
        TEST_ASSERT_EQUAL_MEMORY(zero, hash, sizeof(hash));
        TEST_ASSERT_EQUAL_UINT32(failure_at <= 6 ? 1 : 0, mining_hash_fault_injector_abort_calls());
        TEST_ASSERT_TRUE(calculate_coinbase_tx_hash_bin(bytes, 2, bytes + 2, 1,
            bytes + 3, 2, bytes + 5, 2, hash));
        assert_hash("a669a4141f56b33d635a76c538dabafb72d136de59cc6373260376376dcba307", hash);
    }
    mining_hash_fault_injector_reset(0);
}
