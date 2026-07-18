#ifndef SV2_NOISE_H
#define SV2_NOISE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_transport.h"

typedef struct sv2_noise_ctx sv2_noise_ctx_t;

// Create a new Noise context (allocates secp256k1 context internally).
sv2_noise_ctx_t *sv2_noise_create(void);

// Destroy a Noise context and free all resources.
void sv2_noise_destroy(sv2_noise_ctx_t *ctx);

// Perform Noise_NX handshake as initiator over the given transport.
// authority_pubkey is an optional 32-byte x-only public key for Schnorr
// signature verification of the responder's certificate. Pass NULL to skip.
// Returns 0 on success, -1 on error.
int sv2_noise_handshake(sv2_noise_ctx_t *ctx, esp_transport_handle_t transport,
                        const uint8_t *authority_pubkey);

// Send an SV2 frame (header + payload) encrypted via Noise.
// frame points to the complete plaintext frame (header + payload).
// Returns 0 on success, -1 on error.
int sv2_noise_send(sv2_noise_ctx_t *ctx, esp_transport_handle_t transport,
                   const uint8_t *frame, int frame_len);

// Receive and decrypt an SV2 frame via Noise.
// hdr_out receives the 6-byte decrypted frame header.
// payload_out receives the decrypted payload (up to max_payload_len bytes).
// payload_len_out receives the actual payload length.
// Returns 0 on success, -1 on error.
int sv2_noise_recv(sv2_noise_ctx_t *ctx, esp_transport_handle_t transport,
                   uint8_t hdr_out[6], uint8_t *payload_out,
                   int max_payload_len, int *payload_len_out);

#endif /* SV2_NOISE_H */
