#ifndef MAIN_H_
#define MAIN_H_

#include "connect.h"

void MINER_set_wifi_status(wifi_status_t status, int retry_count, int reason);
void MINER_set_ap_status(bool enabled);
void self_test(void * pvParameters);

#endif /* MAIN_H_ */