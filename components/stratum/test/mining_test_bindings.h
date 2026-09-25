#ifndef MINING_TEST_BINDINGS_H
#define MINING_TEST_BINDINGS_H

#include <stdlib.h>
#include "mining_allocator_fault_injector.h"

/* Keep injected allocation failures isolated from other tasks. */
#define mining_build_asic_job mining_test_build_asic_job
#define calculate_coinbase_tx_hash_bin mining_test_calculate_coinbase_tx_hash_bin
#define calculate_merkle_root_hash mining_test_calculate_merkle_root_hash
#define hash_to_pdiff mining_test_hash_to_pdiff
#define mining_nonce_difficulty mining_test_nonce_difficulty
#define increment_bitmask mining_test_increment_bitmask

#define malloc(size) mining_allocator_fault_injector_malloc(size)

#endif
