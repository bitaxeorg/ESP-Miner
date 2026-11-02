#ifndef MINING_TEST_BINDINGS_H
#define MINING_TEST_BINDINGS_H

#include <stdlib.h>
#include "mining_allocator_fault_injector.h"
#include "mining_hash_fault_injector.h"

/* Keep injected allocation failures isolated from other tasks. */
#define mining_build_asic_job mining_test_build_asic_job
#define calculate_coinbase_tx_hash_bin mining_test_calculate_coinbase_tx_hash_bin
#define calculate_merkle_root_hash mining_test_calculate_merkle_root_hash
#define mining_nonce_hash mining_test_nonce_hash
#define increment_bitmask mining_test_increment_bitmask

#define malloc(size) mining_allocator_fault_injector_malloc(size)

#endif
