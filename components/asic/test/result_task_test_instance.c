#include "result_task_test_bindings.h"
#include "../../../main/tasks/asic_result_task.c"

/* SatsForFreedom hooks are outside the result-task behavior under test. */
void sff_result(GlobalState *state, bool received)
{
    (void)state;
    (void)received;
}

void sff_response(bool job_response)
{
    (void)job_response;
}
