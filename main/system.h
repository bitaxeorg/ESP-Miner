#ifndef SYSTEM_H_
#define SYSTEM_H_

#define HISTORY_LENGTH 100
#define HISTORY_WINDOW_SIZE 5
typedef struct {
    double duration_start;
    int historical_hashrate_rolling_index;
    double historical_hashrate_time_stamps[HISTORY_LENGTH];
    double historical_hashrate[HISTORY_LENGTH];
    int historical_hashrate_init;
    double current_hashrate;
    time_t start_time;
    uint16_t shares_accepted;
    uint16_t shares_rejected;
    int screen_page;
    char oled_buf[20];
} SystemModule;

void SYSTEM_task(void *parameters);


void SYSTEM_notify_accepted_share(SystemModule* module);
void SYSTEM_notify_rejected_share(SystemModule* module);
void SYSTEM_notify_found_nonce(SystemModule* module, double nonce_diff);
void SYSTEM_notify_mining_started(SystemModule* module);

#endif /* SYSTEM_H_ */
