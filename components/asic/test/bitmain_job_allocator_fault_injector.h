#ifndef BITMAIN_JOB_ALLOCATOR_FAULT_INJECTOR_H_
#define BITMAIN_JOB_ALLOCATOR_FAULT_INJECTOR_H_

#include <stddef.h>

void bitmain_job_allocator_fault_injector_reset(size_t failing_allocation);
size_t bitmain_job_allocator_fault_injector_calls(void);
void *bitmain_job_allocator_fault_injector_malloc(size_t size);

#endif
