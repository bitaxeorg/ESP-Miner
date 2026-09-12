#ifndef BITMAIN_JOB_ALLOCATOR_FIXTURE_H_
#define BITMAIN_JOB_ALLOCATOR_FIXTURE_H_

#include <stddef.h>

void bitmain_job_allocator_fault_injector_reset(size_t failure_at);
size_t bitmain_job_allocator_fault_injector_calls(void);
void *bitmain_job_allocator_fault_injector_malloc(size_t size);
char *bitmain_job_allocator_fault_injector_strdup(const char *text);

#endif
