#ifndef BZM_SERIAL_BUFFERS_H_
#define BZM_SERIAL_BUFFERS_H_

// Poll after each logical-engine assignment; retain 32 KiB until on-device backlog and scheduling latency are measured.
#define BZM_SERIAL_RX_BUFFER_BYTES (32U * 1024U)
#define BZM_SERIAL_TX_BUFFER_BYTES (2U * 1024U)
#define BZM_SERIAL_SERVICE_INTERVAL_MS 50U

#endif
