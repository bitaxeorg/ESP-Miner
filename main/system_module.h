#ifndef SYSTEM_MODULE_H_
#define SYSTEM_MODULE_H_

#define HISTORY_LENGTH 100
#define DIFF_STRING_SIZE 10

typedef struct
{
    char message[64];
    uint32_t count;
} RejectedReasonStat;



typedef struct
{
    // The starting time for a certain period of mining activity.
    double duration_start;

    // The index to keep track of the rolling historical hashrate data.
    int historical_hashrate_rolling_index;

    // An array to store timestamps corresponding to historical hashrate values.
    double historical_hashrate_time_stamps[HISTORY_LENGTH];

    // An array to store historical hashrate values over a defined period.
    double historical_hashrate[HISTORY_LENGTH];

    // A flag indicating if the historical hashrate data is initialized.
    int historical_hashrate_init;

    // The current calculated hashrate of the system at this moment.
    double current_hashrate;

    // The average hashrate over a defined period of time.
    double avg_hashrate;

    // The hashrate without accounting for errors or invalid data points.
    double hashrate_no_error;

    // The hashrate accounting for errors or invalid data points.
    double hashrate_error;

    // The starting timestamp of the mining session.
    int64_t start_time;

    // A count of shares that have been accepted by the pool.
    uint64_t shares_accepted;

    // A count of shares that have been rejected by the pool.
    uint64_t shares_rejected;

    // An array to store statistics about reasons for share rejections.
    RejectedReasonStat rejected_reason_stats[10];

    // The number of different rejection reason statistics recorded.
    int rejected_reason_stats_count;

    // A variable indicating which screen page is currently active or being displayed.
    int screen_page;

    // The difficulty (nonce) for the best solution found during mining.
    uint64_t best_nonce_diff;

    // A string representing the difficulty of the best nonce in readable format.
    char best_diff_string[DIFF_STRING_SIZE];

    // The difficulty (nonce) for the best solution found in this session.
    uint64_t best_session_nonce_diff;

    // A string representing the difficulty of the best session nonce in readable format.
    char best_session_diff_string[DIFF_STRING_SIZE];
} SystemModule;

extern SystemModule SYSTEM_MODULE;

#endif