#ifndef SYSTEM_H_
#define SYSTEM_H_

typedef struct {
    double duration_start;
} SystemModule;

void SYSTEM_task(SystemModule* module);


void SYSTEM_notify_accepted_share(void);
void SYSTEM_notify_rejected_share(void);
void SYSTEM_notify_found_nonce(SystemModule* module, double nonce_diff);
void SYSTEM_notify_mining_started(SystemModule* module);

#endif /* SYSTEM_H_ */
