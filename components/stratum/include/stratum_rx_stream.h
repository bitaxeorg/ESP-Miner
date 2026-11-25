#ifndef STRATUM_RX_STREAM_H
#define STRATUM_RX_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_transport.h"

typedef struct {
    char *json_rpc_buffer;
    char *rx_read_buffer;
    size_t json_rpc_buffer_size;
    size_t json_rpc_buffer_len;
    uint8_t *suffix;
    size_t suffix_len;
    bool suffix_valid;
    int64_t line_start_us;
} stratum_rx_stream_t;

bool stratum_rx_stream_init(stratum_rx_stream_t *stream);
void stratum_rx_stream_free(stratum_rx_stream_t *stream);

bool stratum_rx_stream_read_line(stratum_rx_stream_t *stream,
                                 esp_transport_handle_t transport,
                                 const char **out_line,
                                 size_t *out_line_len,
                                 size_t *out_raw_len);

void stratum_rx_stream_consume_line(stratum_rx_stream_t *stream, size_t line_len);

bool stratum_rx_stream_has_coinb2(const stratum_rx_stream_t *stream,
                                  const char *c2_str,
                                  size_t *out_len);

uint8_t *stratum_rx_stream_take_coinb2(stratum_rx_stream_t *stream, size_t *out_len);

int64_t stratum_rx_stream_line_start_us(const stratum_rx_stream_t *stream);

#endif // STRATUM_RX_STREAM_H
