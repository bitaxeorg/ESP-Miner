#ifndef BZM_WORK_REF_H
#define BZM_WORK_REF_H

#include "bzm/result.h"
#include "asic_job.h"

typedef struct {
    bzm_work_handle_t handle;
    const asic_job_t *template;
} bzm_work_ref_t;

#endif // BZM_WORK_REF_H
