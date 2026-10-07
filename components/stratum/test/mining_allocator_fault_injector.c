#include "mining_allocator_fault_injector.h"

#include <stdlib.h>

static size_t allocation_count;
static size_t failure_index;
static size_t last_size;

void mining_allocator_fault_injector_reset(size_t failure_at)
{
    allocation_count = 0;
    failure_index = failure_at;
    last_size = 0;
}

void *mining_allocator_fault_injector_malloc(size_t size)
{
    allocation_count++;
    last_size = size;
    if (allocation_count == failure_index) {
        return NULL;
    }
    return malloc(size);
}

size_t mining_allocator_fault_injector_calls(void)
{
    return allocation_count;
}

size_t mining_allocator_fault_injector_last_size(void)
{
    return last_size;
}
