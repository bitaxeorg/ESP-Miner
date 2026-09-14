#include "bitmain_job_allocator_fault_injector.h"
#include <stdlib.h>

static size_t allocation_count;
static size_t failure_at;

void bitmain_job_allocator_fault_injector_reset(size_t failing_allocation)
{
    allocation_count = 0;
    failure_at = failing_allocation;
}

size_t bitmain_job_allocator_fault_injector_calls(void)
{
    return allocation_count;
}

void *bitmain_job_allocator_fault_injector_malloc(size_t size)
{
    if (++allocation_count == failure_at) {
        return NULL;
    }
    return malloc(size);
}
