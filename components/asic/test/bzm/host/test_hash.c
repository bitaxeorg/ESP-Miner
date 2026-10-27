#include "check.h"
#include "bzm/hash.h"
#include "bzm/job.h"
#include "bzm/result_dedup.h"
#include <string.h>
#include "hash_vectors.h"

static void check_hex(const uint8_t digest[32], const char *expected)
{
    char hex[65];
    for (size_t i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    CHECK(strcmp(hex, expected) == 0);
}
int main(void)
{
    uint8_t data[1024], hash[32];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)i;
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        CHECK(bzm_sha256(data, vectors[i].size, hash));
        check_hex(hash, vectors[i].single);
        CHECK(bzm_double_sha256(data, vectors[i].size, hash));
        check_hex(hash, vectors[i].double_hash);
    }
    CHECK(!bzm_sha256(NULL, 1, hash));
    CHECK(!bzm_sha256(data, 1, NULL));
    CHECK(bzm_sha256(NULL, 0, hash));
    uint8_t boundary[32] = {[25] = 0xf0, [26] = 0xff, [27] = 0x0f};
    CHECK(bzm_hash_meets_difficulty(boundary, 4));
    boundary[0] = 1;
    CHECK(!bzm_hash_meets_difficulty(boundary, 4));
    boundary[0] = 0;
    --boundary[25];
    CHECK(bzm_hash_meets_difficulty(boundary, 4));
    asic_job_t job = {.version = 0x20000000, .version_mask = 0x6000, .ntime = 1234};
    job.merkle_root[28] = 0x12; job.merkle_root[29] = 0x34;
    job.merkle_root[30] = 0x56; job.merkle_root[31] = 0x78;
    bzm_work_ref_t ref = {.template = &job, .handle = 1};
    bzm_work_t work;
    CHECK(bzm_work_build(&ref, 1, 0, 16, 36, &work));
    CHECK(work.merkle_residue == 0x78563412);
    CHECK(work.versions[0] == 0x20000000 && work.versions[1] == 0x20002000);
    CHECK(work.versions[2] == 0x20004000 && work.versions[3] == 0x20006000);
    const char *midstates[4] = {
        "fab2449298967ae732d8dc6a0bb20789de33b02d9c20b25ba28cb0cc540c047b",
        "cd0bcf702436ce9340a5b98e93328502911f646e84cef0417543c5bad9a5b9b6",
        "576a91fb7f649fee9dc546ee5ac87bdb4f9e260f8b85b1fdf9d96374c705f1cd",
        "5f7561bb0aaa75a26771f8bbcf71c5d0ca9b2e5accb7579fa484307fb14fb4d1",
    };
    for (size_t i = 0; i < 4; ++i) check_hex(work.midstates[i], midstates[i]);
    job.version_mask = 0;
    CHECK(bzm_work_build(&ref, 1, 0, 16, 36, &work));
    for (size_t i = 0; i < 4; ++i) check_hex(work.midstates[i], midstates[0]);
    bzm_result_dedup_t cache = {0};
    bzm_result_t result = {.nonce = 123, .final_ntime = 1234, .final_version = job.version};
    CHECK(!bzm_result_is_duplicate(&cache, &job, &result));
    CHECK(bzm_result_is_duplicate(&cache, &job, &result));
    ++result.nonce;
    CHECK(!bzm_result_is_duplicate(&cache, &job, &result));
    puts("SHA-256 padding vectors, difficulty boundary, BZM version encoding and dedup passed");
    return 0;
}
