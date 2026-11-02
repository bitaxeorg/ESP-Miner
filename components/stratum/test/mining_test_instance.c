#include "psa/crypto.h"
#include "mining_test_bindings.h"

static size_t hash_failure_at;
static size_t hash_calls;
static size_t hash_abort_calls;

void mining_hash_fault_injector_reset(size_t failure_at)
{
    hash_failure_at = failure_at;
    hash_calls = 0;
    hash_abort_calls = 0;
}

size_t mining_hash_fault_injector_calls(void)
{
    return hash_calls;
}

size_t mining_hash_fault_injector_abort_calls(void)
{
    return hash_abort_calls;
}

static psa_status_t injected_hash_abort(psa_hash_operation_t *operation)
{
    hash_abort_calls++;
    return psa_hash_abort(operation);
}

static psa_status_t injected_hash_setup(psa_hash_operation_t *operation, psa_algorithm_t algorithm)
{
    if (++hash_calls == hash_failure_at) {
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    return psa_hash_setup(operation, algorithm);
}

static psa_status_t injected_hash_update(psa_hash_operation_t *operation,
                                        const uint8_t *input, size_t input_length)
{
    if (++hash_calls == hash_failure_at) {
        psa_hash_abort(operation);
        return PSA_ERROR_HARDWARE_FAILURE;
    }
    return psa_hash_update(operation, input, input_length);
}

static psa_status_t injected_hash_finish(psa_hash_operation_t *operation,
                                        uint8_t *hash, size_t hash_size, size_t *hash_length)
{
    if (++hash_calls == hash_failure_at) {
        psa_hash_abort(operation);
        return PSA_ERROR_HARDWARE_FAILURE;
    }
    return psa_hash_finish(operation, hash, hash_size, hash_length);
}

static psa_status_t injected_hash_compute(psa_algorithm_t algorithm,
                                         const uint8_t *input, size_t input_length,
                                         uint8_t *hash, size_t hash_size, size_t *hash_length)
{
    if (++hash_calls == hash_failure_at) {
        return PSA_ERROR_HARDWARE_FAILURE;
    }
    return psa_hash_compute(algorithm, input, input_length, hash, hash_size, hash_length);
}

#define psa_hash_setup injected_hash_setup
#define psa_hash_update injected_hash_update
#define psa_hash_finish injected_hash_finish
#define psa_hash_compute injected_hash_compute
#define psa_hash_abort injected_hash_abort
#include "../mining.c"
#include "../mining_job.c"
