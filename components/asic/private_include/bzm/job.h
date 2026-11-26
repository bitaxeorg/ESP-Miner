#ifndef BZM_JOB_H
#define BZM_JOB_H

#include "bzm/protocol.h"
#include "bzm/work_ref.h"

bool bzm_work_build(const bzm_work_ref_t *source, uint16_t engine_id,
                    uint8_t logical_sequence, uint8_t timestamp_count,
                    uint8_t lead_zeros,
                    bzm_work_t *work);

#endif
