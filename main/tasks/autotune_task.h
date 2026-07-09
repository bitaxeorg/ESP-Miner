#ifndef AUTOTUNE_TASK_H_
#define AUTOTUNE_TASK_H_

typedef enum {
    AUTOTUNE_STATE_IDLE = 0,      // disabled via NVS
    AUTOTUNE_STATE_PAUSED,        // waiting out an overheat/pause/fault condition
    AUTOTUNE_STATE_WARMING,       // settled on a step, collecting samples
    AUTOTUNE_STATE_AT_CEILING,    // sitting at the highest vendor-approved step
} AutotuneState;

typedef struct {
    AutotuneState state;
    int freq_step_index;
    int volt_step_index;
    float max_temp_seen_this_window;
    float last_reject_rate;
    int step_downs_total;
    int step_ups_total;
} AutotuneModule;

void AUTOTUNE_task(void * pvParameters);

#endif 
