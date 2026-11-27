#ifndef WEBHOOK_ALERT_UTILS_H_
#define WEBHOOK_ALERT_UTILS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool WEBHOOK_ALERT_UTILS_is_valid_url(const char *url, size_t max_len);
bool WEBHOOK_ALERT_UTILS_result_matches(uint32_t expected_request_id, uint32_t actual_request_id);
uint32_t WEBHOOK_ALERT_UTILS_remaining_ticks(uint32_t start_ticks, uint32_t now_ticks,
                                             uint32_t timeout_ticks);
bool WEBHOOK_ALERT_UTILS_deadline_expired(int64_t start_us, int64_t now_us, uint32_t timeout_ms);

#endif /* WEBHOOK_ALERT_UTILS_H_ */
