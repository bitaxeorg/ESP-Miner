#ifndef SYSTEM_H_
#define SYSTEM_H_

typedef struct {

} SystemModule;

void SYSTEM_task(void *arg);


void SYSTEM_notify_accepted_share(void);
void SYSTEM_notify_rejected_share(void);
void SYSTEM_notify_found_nonce(double nonce_diff);
void SYSTEM_notify_mining_started(void);

#endif /* SYSTEM_H_ */