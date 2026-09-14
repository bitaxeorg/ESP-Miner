#ifndef MINING_TEST_BINDINGS_H
#define MINING_TEST_BINDINGS_H

#include <stdlib.h>
#include "mining_allocator_fault_injector.h"

/* Give the private test instance unique symbols so unrelated tests and system
 * tasks cannot consume an injected allocation failure. */
#define free_bm_job mining_test_free_bm_job
#define calculate_coinbase_tx_hash_bin mining_test_calculate_coinbase_tx_hash_bin
#define calculate_merkle_root_hash mining_test_calculate_merkle_root_hash
#define construct_bm_job_from_miner_job mining_test_construct_bm_job_from_miner_job
#define hash_to_pdiff mining_test_hash_to_pdiff
#define test_nonce_value mining_test_nonce_value
#define increment_bitmask mining_test_increment_bitmask

#define malloc(size) mining_allocator_fault_injector_malloc(size)

#endif
