#ifndef MINING_WATCHDOG_H_
#define MINING_WATCHDOG_H_

#include <stdbool.h>
#include <stdint.h>

// Watchdogs for the "flatline": the chain stops returning results while it
// keeps drawing power, or the pool stops sending work or answering shares.
// They watch counters, not averages, because the averages freeze in place.
// The step functions are called once a second, so every count is in seconds.
// No clock is read here, so the logic can be tested off the device.

#define WD_ASIC_MIN_WINDOW_S      150         // never decide on less than this
#define WD_ASIC_WINDOW_INTERVALS  30          // expected result intervals in a window
#define WD_ASIC_MAX_WINDOW_S      (30 * 60)   // longer than this: too slow to watch
#define WD_ASIC_FROZEN_S          (3 * 60)    // 1 min hashrate not changing at all while mining

// Recovery budget: at most WD_ASIC_MAX_RECOVERIES, refilled once
// WD_ASIC_BUDGET_S seconds have passed without a recovery
#define WD_ASIC_MAX_RECOVERIES    3
#define WD_ASIC_BUDGET_S          (60 * 60)

#define WD_POOL_NO_WORK_S         (5 * 60)    // pools send work at least every 30-60 s
#define WD_POOL_NO_REPLY_S        (3 * 60)    // a submitted share with no reply at all

typedef enum {
    WD_ASIC_NONE = 0,
    WD_ASIC_REINIT,     // re-initialise the chain
    WD_ASIC_RESTART,    // a re-init didn't get the signal moving again: restart the ESP32
    WD_ASIC_GIVE_UP,    // recovery budget used up
} wd_asic_action_t;

typedef enum {
    WD_ASIC_REASON_NONE = 0,
    WD_ASIC_REASON_NO_RESULTS,      // no result frames from the chain
    WD_ASIC_REASON_FROZEN_HASHRATE, // the 1 min hashrate stopped changing
} wd_asic_reason_t;

typedef struct {
    uint32_t last_results;
    uint32_t silent_s;
    float last_hashrate;
    uint32_t frozen_s;
    uint8_t recoveries;             // used from the budget
    uint32_t since_recovery_s;
    wd_asic_action_t last_action;
    wd_asic_reason_t reason;        // what fired the last action
    bool results_since_recovery;
    bool hashrate_since_recovery;
} wd_asic_t;

typedef struct {
    bool running;       // chain up at its target clock; not paused, overheated or in self-test
    bool has_work;      // the pool has given work to hash
    uint32_t results;   // running count of results from the chain
    float hashrate_1m;  // compared exactly: while hashing it moves every second
    uint32_t window_s;  // from wd_asic_window_s(); 0 disables the no-results check
} wd_asic_input_t;

typedef enum {
    WD_POOL_NONE = 0,
    WD_POOL_NO_WORK,    // no mining.notify / new job for too long
    WD_POOL_NO_REPLIES, // shares go out, nothing comes back
} wd_pool_action_t;

typedef struct {
    bool active;
    uint64_t last_work;
    uint32_t no_work_s;
    uint64_t last_submitted;
    uint64_t last_resolved;
    bool waiting;           // a share went out and nothing has been answered since
    uint32_t waiting_s;
} wd_pool_t;

// recoveries and last_action carry over a restart (kept in RTC memory by the caller)
void wd_asic_init(wd_asic_t *wd, uint8_t recoveries, wd_asic_action_t last_action);
// One second of observation. After an action, wd->reason says which signal fired.
wd_asic_action_t wd_asic_step(wd_asic_t *wd, const wd_asic_input_t *in);

// No-results window for the expected result rate: the chain returns a result
// for every 2^diff_power * 2^32 hashes
uint32_t wd_asic_window_s(double expected_ghs, uint8_t diff_power);

void wd_pool_init(wd_pool_t *wd);
// One second of observation
wd_pool_action_t wd_pool_step(wd_pool_t *wd, bool active, uint64_t work_received, uint64_t submitted,
                              uint64_t resolved);

#endif /* MINING_WATCHDOG_H_ */
