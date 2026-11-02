#ifndef MINING_HASH_FAULT_INJECTOR_H
#define MINING_HASH_FAULT_INJECTOR_H

#include <stddef.h>

/* Fail one PSA hash call in the mining test instance; zero disables injection. */
void mining_hash_fault_injector_reset(size_t failure_at);
size_t mining_hash_fault_injector_calls(void);
size_t mining_hash_fault_injector_abort_calls(void);

#endif
