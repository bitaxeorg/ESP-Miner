#ifndef STRATUM_TASK_H_
#define STRATUM_TASK_H_

void stratum_task(void *pvParameters);
void stratum_close_connection();
int stratum_submit_share(char * jobid, char * extranonce2, uint32_t ntime,uint32_t nonce,uint32_t version);

#endif