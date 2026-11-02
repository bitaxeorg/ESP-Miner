#include "unity.h"
#include "asic_job_slot.h"
#include "asic_common.h"
#include "bm_job_packet.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "bm1373.h"
#include "bm1397.h"
#include <arpa/inet.h>
#include <string.h>

static asic_job_slot_entry_t s_test_entries[256];
static asic_job_store_t s_test_store;

TEST_CASE("job slot initialization zeroes active jobs and rejects initial lookups",
          "[asic][job-slot]")
{
    asic_job_store_t *store = &s_test_store;
    asic_job_store_init(store, s_test_entries, BM13XX_JOB_SLOT_COUNT);
    TEST_ASSERT_EQUAL_UINT8(0, store->current_slot);

    asic_job_t out_job;
    for (uint8_t slot = 0; slot < BM13XX_JOB_SLOT_COUNT; ++slot) {
        TEST_ASSERT_FALSE(asic_job_store_lookup(store, slot, &out_job));
    }
    // Out-of-bounds slots must also be rejected
    TEST_ASSERT_FALSE(asic_job_store_lookup(store, BM13XX_JOB_SLOT_COUNT, &out_job));
    TEST_ASSERT_FALSE(asic_job_store_lookup(store, 0xff, &out_job));
    asic_job_store_destroy(store);
}

TEST_CASE("job slot claiming advances ring buffer and preserves job payload",
          "[asic][job-slot]")
{
    asic_job_store_t *store = &s_test_store;
    asic_job_store_init(store, s_test_entries, BM13XX_JOB_SLOT_COUNT);

    asic_job_t job = {
        .version = 0x20000004,
        .nbits = 0x1705dd01,
        .ntime = 0x64658bd8,
        .job_id = "test-job-42",
        .extranonce2 = "010203",
    };

    // Claims advance 1..15, then wrap to 0, then 1...
    for (size_t i = 1; i <= BM13XX_JOB_SLOT_COUNT * 2; ++i) {
        uint8_t expected_slot = (uint8_t)(i % BM13XX_JOB_SLOT_COUNT);
        job.version = 0x20000000 + (uint32_t)i;
        (void)snprintf(job.job_id, sizeof(job.job_id), "job-%zu", i);

        uint8_t claimed = (uint8_t)asic_job_store_claim(store, &job);
        TEST_ASSERT_EQUAL_UINT8(expected_slot, claimed);

        asic_job_t lookup = {0};
        TEST_ASSERT_TRUE(asic_job_store_lookup(store, claimed, &lookup));
        TEST_ASSERT_TRUE(s_test_entries[claimed].is_valid);
        TEST_ASSERT_EQUAL_HEX32(job.version, lookup.version);
        TEST_ASSERT_EQUAL_HEX32(job.nbits, lookup.nbits);
        TEST_ASSERT_EQUAL_STRING(job.job_id, lookup.job_id);

        // Lookup with NULL out_job checks validity without crashing
        TEST_ASSERT_TRUE(asic_job_store_lookup(store, claimed, NULL));
    }
    asic_job_store_destroy(store);
}

TEST_CASE("job slot claiming supports 32 slots for BM1397",
          "[asic][job-slot]")
{
    asic_job_store_t *store = &s_test_store;
    asic_job_store_init(store, s_test_entries, BM1397_JOB_SLOT_COUNT);
    TEST_ASSERT_EQUAL_UINT16(32, store->capacity);

    asic_job_t job = {.version = 0x20000001, .job_id = "bm1397-job"};

    // Claims advance 1..31, wrap to 0, then 1
    for (size_t i = 1; i <= BM1397_JOB_SLOT_COUNT * 2; ++i) {
        uint8_t expected_slot = (uint8_t)(i % BM1397_JOB_SLOT_COUNT);
        uint8_t claimed = (uint8_t)asic_job_store_claim(store, &job);
        TEST_ASSERT_EQUAL_UINT8(expected_slot, claimed);
        TEST_ASSERT_TRUE(asic_job_store_lookup(store, claimed, NULL));
        TEST_ASSERT_TRUE(s_test_entries[claimed].is_valid);
    }
    asic_job_store_destroy(store);
}

TEST_CASE("job slot claiming supports large slot counts (256 slots for BNZ)",
          "[asic][job-slot]")
{
    asic_job_store_t *store = &s_test_store;
    asic_job_store_init(store, s_test_entries, 256);
    TEST_ASSERT_EQUAL_UINT16(256, store->capacity);

    asic_job_t job = {.version = 0x20000001, .job_id = "bnz-job"};
    for (size_t i = 1; i <= 256 * 2; ++i) {
        uint16_t expected_slot = (uint16_t)(i % 256);
        uint16_t claimed = asic_job_store_claim(store, &job);
        TEST_ASSERT_EQUAL_UINT16(expected_slot, claimed);
        TEST_ASSERT_TRUE(asic_job_store_lookup(store, claimed, NULL));
        TEST_ASSERT_TRUE(s_test_entries[claimed].is_valid);
    }
    asic_job_store_destroy(store);
}

TEST_CASE("job slot claiming with NULL clears payload and marks slot valid",
          "[asic][job-slot]")
{
    asic_job_store_t *store = &s_test_store;
    asic_job_store_init(store, s_test_entries, BM13XX_JOB_SLOT_COUNT);

    uint8_t slot = (uint8_t)asic_job_store_claim(store, NULL);
    TEST_ASSERT_EQUAL_UINT8(1, slot);

    asic_job_t lookup;
    memset(&lookup, 0xaa, sizeof(lookup));
    TEST_ASSERT_TRUE(asic_job_store_lookup(store, slot, &lookup));
    TEST_ASSERT_TRUE(s_test_entries[slot].is_valid);
    TEST_ASSERT_EQUAL_HEX32(0, lookup.version);
    TEST_ASSERT_EQUAL_STRING("", lookup.job_id);
    asic_job_store_destroy(store);
}

TEST_CASE("job slot invalidation marks all active slots invalid",
          "[asic][job-slot]")
{
    asic_job_store_t *store = &s_test_store;
    asic_job_store_init(store, s_test_entries, BM13XX_JOB_SLOT_COUNT);

    asic_job_t job = {.version = 0x20000001, .job_id = "valid-job"};
    for (size_t i = 0; i < BM13XX_JOB_SLOT_COUNT; ++i) {
        (void)asic_job_store_claim(store, &job);
    }

    // Verify all slots are currently valid
    for (uint8_t slot = 0; slot < BM13XX_JOB_SLOT_COUNT; ++slot) {
        TEST_ASSERT_TRUE(asic_job_store_lookup(store, slot, NULL));
    }

    asic_job_store_invalidate_all(store);

    // After invalidation, no slot should be active
    for (uint8_t slot = 0; slot < BM13XX_JOB_SLOT_COUNT; ++slot) {
        asic_job_t lookup;
        TEST_ASSERT_FALSE(asic_job_store_lookup(store, slot, &lookup));
    }

    // New claim after invalidation works and becomes valid
    uint8_t next_slot = (uint8_t)asic_job_store_claim(store, &job);
    TEST_ASSERT_TRUE(asic_job_store_lookup(store, next_slot, NULL));
    asic_job_store_destroy(store);
}

TEST_CASE("job slot to chip slot mapping roundtrips across shift amounts and sub-ids",
          "[asic][job-slot]")
{
    const uint8_t shifts[] = {2, 3, 4};

    for (size_t s = 0; s < sizeof(shifts); ++s) {
        uint8_t shift = shifts[s];
        uint8_t max_sub_id = (uint8_t)((1U << shift) - 1);

        for (uint8_t slot = 0; slot < BM13XX_JOB_SLOT_COUNT; ++slot) {
            uint8_t base_chip_slot = bm_encode_wire_job_slot(slot, shift);
            TEST_ASSERT_EQUAL_UINT8(slot, bm_decode_wire_job_slot(base_chip_slot, shift));
            TEST_ASSERT_EQUAL_UINT8(0, bm_decode_wire_sub_id(base_chip_slot, shift));

            for (uint8_t sub = 0; sub <= max_sub_id; ++sub) {
                uint8_t wire_chip_slot = (uint8_t)(base_chip_slot | sub);
                TEST_ASSERT_EQUAL_UINT8(slot, bm_decode_wire_job_slot(wire_chip_slot, shift));
                TEST_ASSERT_EQUAL_UINT8(sub, bm_decode_wire_sub_id(wire_chip_slot, shift));
            }
        }
    }
}

TEST_CASE("job result decoder succeeds for single-chip address interval (256)",
          "[asic][job-slot]")
{
    asic_job_store_t *store = &s_test_store;
    asic_job_store_init(store, s_test_entries, BM13XX_JOB_SLOT_COUNT);

    asic_job_t job = {.version = 0x20000000, .job_id = "test-job"};
    uint8_t slot = (uint8_t)asic_job_store_claim(store, &job);

    task_result result;
    // Single-chip miners (Bitaxe Gamma, Ultra, Supra, Max) have address_interval = 256 / 1 = 256
    uint8_t wire_chip_slot = bm_encode_wire_job_slot(slot, BM1370_SMALL_CORE_BITS);
    uint32_t wire_nonce = 0x12345678;

    // BM13xx decode
    TEST_ASSERT_TRUE(bm13xx_decode_job_result(store, BM1370_SMALL_CORE_BITS, 256, wire_nonce, wire_chip_slot, 0, &result));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, result.nonce);
    TEST_ASSERT_EQUAL_STRING("test-job", result.job.job_id);

    // BM1397 decode
    TEST_ASSERT_TRUE(bm1397_decode_job_result(store, 256, wire_nonce, bm_encode_wire_job_slot(slot, BM1397_MIDSTATE_BITS), &result));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, result.nonce);
    TEST_ASSERT_EQUAL_STRING("test-job", result.job.job_id);

    // Multi-chip (e.g. 2 chips, address_interval = 128)
    TEST_ASSERT_TRUE(bm13xx_decode_job_result(store, BM1370_SMALL_CORE_BITS, 128, wire_nonce, wire_chip_slot, 0, &result));

    // Zero address_interval or NULL result must fail safely
    TEST_ASSERT_FALSE(bm13xx_decode_job_result(store, BM1370_SMALL_CORE_BITS, 0, wire_nonce, wire_chip_slot, 0, &result));
    TEST_ASSERT_FALSE(bm13xx_decode_job_result(store, BM1370_SMALL_CORE_BITS, 256, wire_nonce, wire_chip_slot, 0, NULL));
    TEST_ASSERT_FALSE(bm1397_decode_job_result(store, 0, wire_nonce, wire_chip_slot, &result));
    TEST_ASSERT_FALSE(bm1397_decode_job_result(store, 256, wire_nonce, wire_chip_slot, NULL));
    asic_job_store_destroy(store);
}

TEST_CASE("ASIC hardware job slot wire encoding and decoding roundtrip",
          "[asic][job-slot]")
{
    static const struct {
        const char *name;
        uint8_t tx_shift;
        uint8_t rx_shift;
        bool is_bm1397;
    } asic_types[] = {
        {"BM1366", BM13XX_TX_SLOT_SHIFT, BM1366_SMALL_CORE_BITS, false},
        {"BM1368", BM13XX_TX_SLOT_SHIFT, BM1368_SMALL_CORE_BITS, false},
        {"BM1370", BM13XX_TX_SLOT_SHIFT, BM1370_SMALL_CORE_BITS, false},
        {"BM1373", BM13XX_TX_SLOT_SHIFT, BM1373_SMALL_CORE_BITS, false},
        {"BM1397", BM1397_TX_SLOT_SHIFT, BM1397_MIDSTATE_BITS,   true},
    };

    asic_job_store_t *store = &s_test_store;
    for (size_t a = 0; a < sizeof(asic_types) / sizeof(asic_types[0]); ++a) {
        uint16_t capacity = asic_types[a].is_bm1397 ? BM1397_JOB_SLOT_COUNT : BM13XX_JOB_SLOT_COUNT;
        asic_job_store_init(store, s_test_entries, capacity);

        for (uint8_t s = 0; s < capacity; ++s) {
            asic_job_t job = {.version = 0x20000000 + s};
            snprintf(job.job_id, sizeof(job.job_id), "job-%u", s);
            uint8_t claimed = (uint8_t)asic_job_store_claim(store, &job);
            TEST_ASSERT_EQUAL_UINT8((s + 1) % capacity, claimed);

            // TX wire packet byte: encodes slot using tx_shift
            uint8_t wire_tx = bm_encode_wire_job_slot(claimed, asic_types[a].tx_shift);

            // Simulate hardware: ASIC reads bits 6..tx_shift as the job ID
            uint8_t asic_internal_id = (wire_tx >> asic_types[a].tx_shift);
            TEST_ASSERT_EQUAL_UINT8(claimed, asic_internal_id);

            // ASIC returns result byte: job ID shifted by rx_shift, with small core / sub ID
            uint8_t sub_id = (claimed * 3) & ((1U << asic_types[a].rx_shift) - 1);
            uint8_t wire_rx = (uint8_t)((asic_internal_id << asic_types[a].rx_shift) | sub_id);

            // Software decodes result byte using rx_shift
            task_result result;
            bool ok;
            if (asic_types[a].is_bm1397) {
                ok = bm1397_decode_job_result(store, 256, 0x11223344, wire_rx, &result);
            } else {
                ok = bm13xx_decode_job_result(store, asic_types[a].rx_shift, 256, 0x11223344, wire_rx, 0, &result);
                TEST_ASSERT_EQUAL_UINT8(sub_id, result.small_core_id);
            }
            TEST_ASSERT_TRUE_MESSAGE(ok, asic_types[a].name);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(job.job_id, result.job.job_id, asic_types[a].name);
            TEST_ASSERT_EQUAL_HEX32(job.version, result.job.version);
        }
        asic_job_store_destroy(store);
    }
}
