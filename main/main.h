#ifndef MAIN_H_
#define MAIN_H_

#include "connect.h"

void MINER_set_wifi_status(wifi_status_t status, uint16_t retry_count);
void self_test(void * pvParameters);

#endif /* MAIN_H_ */