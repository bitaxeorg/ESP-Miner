#ifndef MINING_ALLOCATOR_FAULT_INJECTOR_H
#define MINING_ALLOCATOR_FAULT_INJECTOR_H

#include <stddef.h>

/* Fail one allocation in the selected mining translation units. Zero disables
 * failure injection. Pool storage, libc strdup and ESP services are unaffected. */
void mining_allocator_fault_injector_reset(size_t failure_at);
void *mining_allocator_fault_injector_malloc(size_t size);
size_t mining_allocator_fault_injector_calls(void);
size_t mining_allocator_fault_injector_last_size(void);

#endif
