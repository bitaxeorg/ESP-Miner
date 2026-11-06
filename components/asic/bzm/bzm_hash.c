#include "bzm/hash.h"
#include "bzm/platform.h"

bool bzm_sha256(const uint8_t *data, size_t size, uint8_t digest[32])
{
    if (digest == NULL || (data == NULL && size != 0)) return false;
#if SIZE_MAX > UINT64_MAX / 8U
    if (size > UINT64_MAX / 8U) return false;
#endif
    return bzm_platform_sha256(data, size, digest) == BZM_OK;
}

bool bzm_double_sha256(const uint8_t *data, size_t size, uint8_t digest[32])
{
    uint8_t first[32];
    return bzm_sha256(data, size, first) && bzm_sha256(first, sizeof(first), digest);
}

bool bzm_nonce_hash(const asic_job_t *job, uint32_t nonce, uint32_t version, uint8_t hash[32])
{
    if (job == NULL) return false;
    uint8_t header[80];
    asic_job_header(job, nonce, version, header);
    return bzm_double_sha256(header, sizeof(header), hash);
}

bool bzm_hash_meets_difficulty(const uint8_t hash[32], uint8_t power)
{
    if (hash == NULL) return false;
    uint8_t target[32] = {[26] = 0xff, [27] = 0xff};
    for (unsigned bit = 0; bit < power; ++bit) {
        unsigned carry = 0;
        for (int i = 31; i >= 0; --i) {
            unsigned next = target[i] & 1U;
            target[i] = (uint8_t)((target[i] >> 1) | (carry << 7));
            carry = next;
        }
    }
    for (int i = 31; i >= 0; --i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;
}
