#ifndef REGISTER_DUMP_H_
#define REGISTER_DUMP_H_

#include <esp_http_server.h>

typedef struct GlobalState GlobalState;

// Reads every ASIC register address (0x00-0xFF) instead of mining, see GET /api/system/asic/registers
void register_dump_run(GlobalState * GLOBAL_STATE);
esp_err_t GET_system_asic_registers(httpd_req_t * req);

#endif /* REGISTER_DUMP_H_ */
