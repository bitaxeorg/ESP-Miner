#ifndef HTTP_RX_H_
#define HTTP_RX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HTTP_RX_BODY_SIZE_OK,
    HTTP_RX_BODY_SIZE_INVALID,
    HTTP_RX_BODY_SIZE_EMPTY,
    HTTP_RX_BODY_SIZE_TOO_LARGE,
} http_rx_body_size_result_t;

typedef enum {
    HTTP_RX_BODY_READ_CONTINUE,
    HTTP_RX_BODY_READ_COMPLETE,
    HTTP_RX_BODY_READ_TIMEOUT,
    HTTP_RX_BODY_READ_INCOMPLETE,
} http_rx_body_read_result_t;

http_rx_body_size_result_t http_rx_body_size_result(size_t content_len,
                                                    size_t buffer_size);
http_rx_body_read_result_t http_rx_body_read_update(size_t content_len,
                                                    size_t *received_total,
                                                    int received,
                                                    int timeout_code,
                                                    int64_t now,
                                                    int64_t deadline);
bool http_rx_upload_deadline_expired(int64_t now, int64_t started_at,
                                     int64_t last_progress_at,
                                     int64_t total_timeout,
                                     int64_t stall_timeout);

#endif /* HTTP_RX_H_ */
