#ifndef STRATUM_RX_STREAM_H
#define STRATUM_RX_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_transport.h"

bool stratum_rx_stream_init(void);

bool stratum_rx_stream_read_line(esp_transport_handle_t transport,
                                 const char **out_line,
                                 size_t *out_line_len,
                                 size_t *out_raw_len);

void stratum_rx_stream_consume_line(size_t line_len);

bool stratum_rx_stream_has_coinb2(const char *c2_str, size_t *out_len);

uint8_t *stratum_rx_stream_take_coinb2(size_t *out_len);

#endif // STRATUM_RX_STREAM_H

