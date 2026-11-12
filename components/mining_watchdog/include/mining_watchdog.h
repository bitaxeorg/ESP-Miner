#ifndef MINING_WATCHDOG_H_
#define MINING_WATCHDOG_H_

#include <stdbool.h>
#include <stdint.h>

// Watchdogs for the "flatline": the chain stops returning results while it
// keeps drawing power, or the pool stops sending work or answering shares.
// They watch counters, not averages, because the averages freeze in place.
// Pure logic with the time passed in, so it can be tested off the device.

#define WD_ASIC_MIN_WINDOW_MS     (150 * 1000)        // never decide on less than this
#define WD_ASIC_WINDOW_INTERVALS  30                  // expected result intervals in a window
#define WD_ASIC_MAX_WINDOW_MS     (30 * 60 * 1000)    // longer than this: too slow to watch
#define WD_ASIC_MAX_RECOVERIES    3                   // per hour
#define WD_ASIC_BUDGET_RESET_MS   (60 * 60 * 1000)    // an hour without a recovery restores the budget
#define WD_STEP_CAP_MS            1000                // a long gap between ticks counts as one second

#define WD_POOL_NO_WORK_MS        (5 * 60 * 1000)     // pools send work at least every 30-60 s
#define WD_POOL_NO_REPLY_MS       (3 * 60 * 1000)     // a submitted share with no reply at all

typedef enum {
    WD_ASIC_NONE = 0,
    WD_ASIC_REINIT,     // re-initialise the chain
    WD_ASIC_RESTART,    // a re-init didn't bring results back: restart the ESP32
    WD_ASIC_GIVE_UP,    // recovery budget used up
} wd_asic_action_t;

typedef struct {
    uint64_t last_ms;
    uint32_t last_results;
    uint32_t silent_ms;
    uint8_t recoveries;             // used from the hourly budget
    uint64_t last_recovery_ms;
    wd_asic_action_t last_action;
    bool results_since_recovery;
} wd_asic_t;

typedef struct {
    bool running;       // chain up at its target clock; not paused, overheated or in self-test
    bool has_work;      // the pool has given work to hash
    uint32_t results;   // running count of results from the chain
    uint32_t window_ms; // from wd_asic_window_ms(); 0 disables the check
} wd_asic_input_t;

typedef enum {
    WD_POOL_NONE = 0,
    WD_POOL_NO_WORK,    // no mining.notify / new job for too long
    WD_POOL_NO_REPLIES, // shares go out, nothing comes back
} wd_pool_action_t;

typedef struct {
    bool active;
    uint64_t last_work;
    uint64_t work_since_ms;
    uint64_t last_submitted;
    uint64_t last_resolved;
    uint64_t unanswered_since_ms;   // 0: no share waiting for a reply
} wd_pool_t;

// recoveries and last_action carry over a restart (kept in RTC memory by the caller)
void wd_asic_init(wd_asic_t *wd, uint64_t now_ms, uint8_t recoveries, wd_asic_action_t last_action);
wd_asic_action_t wd_asic_step(wd_asic_t *wd, uint64_t now_ms, const wd_asic_input_t *in);

// Window for the expected result rate: the chain returns a result for every
// 2^diff_power * 2^32 hashes.
uint32_t wd_asic_window_ms(double expected_ghs, uint8_t diff_power);

void wd_pool_init(wd_pool_t *wd);
wd_pool_action_t wd_pool_step(wd_pool_t *wd, uint64_t now_ms, bool active,
                              uint64_t work_received, uint64_t submitted, uint64_t resolved);

#endif /* MINING_WATCHDOG_H_ */
