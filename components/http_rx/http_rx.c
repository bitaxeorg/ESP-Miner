#include "http_rx.h"

http_rx_body_size_result_t http_rx_body_size_result(size_t content_len,
                                                    size_t buffer_size)
{
    if (buffer_size < 2) {
        return HTTP_RX_BODY_SIZE_INVALID;
    }
    if (content_len == 0) {
        return HTTP_RX_BODY_SIZE_EMPTY;
    }
    if (content_len >= buffer_size) {
        return HTTP_RX_BODY_SIZE_TOO_LARGE;
    }
    return HTTP_RX_BODY_SIZE_OK;
}

http_rx_body_read_result_t http_rx_body_read_update(size_t content_len,
                                                    size_t *received_total,
                                                    int received,
                                                    int timeout_code,
                                                    int64_t now,
                                                    int64_t deadline)
{
    if (received_total == NULL || *received_total > content_len) {
        return HTTP_RX_BODY_READ_INCOMPLETE;
    }

    if (received > 0) {
        size_t received_size = (size_t)received;
        if (received_size > content_len - *received_total) {
            return HTTP_RX_BODY_READ_INCOMPLETE;
        }
        *received_total += received_size;
        if (*received_total == content_len) {
            return HTTP_RX_BODY_READ_COMPLETE;
        }
        return now >= deadline ? HTTP_RX_BODY_READ_TIMEOUT
                               : HTTP_RX_BODY_READ_CONTINUE;
    }

    if (received == timeout_code) {
        return now >= deadline ? HTTP_RX_BODY_READ_TIMEOUT
                               : HTTP_RX_BODY_READ_CONTINUE;
    }
    return HTTP_RX_BODY_READ_INCOMPLETE;
}

bool http_rx_upload_deadline_expired(int64_t now, int64_t started_at,
                                     int64_t last_progress_at,
                                     int64_t total_timeout,
                                     int64_t stall_timeout)
{
    return now - started_at >= total_timeout ||
           now - last_progress_at >= stall_timeout;
}
