#ifndef SV2_CONNECTION_TEST_H
#define SV2_CONNECTION_TEST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool supported;       // true if the pool answered the SV2 Noise handshake
    char message[160];    // human-readable result for the UI
} sv2_test_result_t;

// Probe whether host:port speaks Stratum V2. Opens a throwaway TCP connection,
// sends a Noise handshake init and checks whether the pool replies with a Noise
// response. Does not complete the handshake and does not touch the live mining
// connection. Fills out with the verdict and a message.
void sv2_test_connection(const char *host, uint16_t port, sv2_test_result_t *out);

#endif /* SV2_CONNECTION_TEST_H */
