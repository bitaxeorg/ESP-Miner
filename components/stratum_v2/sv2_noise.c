#include "sv2_noise.h"
#include "sv2_api.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "mbedtls/chachapoly.h"

#include "secp256k1.h"
#include "secp256k1_ellswift.h"
#include "secp256k1_schnorrsig.h"
#include "secp256k1_extrakeys.h"

static const char *TAG = "sv2_noise";

#define TRANSPORT_TIMEOUT_MS 5000
#define RECV_TIMEOUT_MS      (60 * 3 * 1000)

// Noise protocol name used to initialize h and ck
static const char NOISE_PROTOCOL_NAME[] = "Noise_NX_Secp256k1+EllSwift_ChaChaPoly_SHA256";

struct sv2_noise_ctx {
    uint8_t h[32];              // handshake hash
    uint8_t ck[32];             // chaining key
    uint8_t e_priv[32];         // ephemeral private key (zeroed after handshake)
    uint8_t e_pub_encoded[64];  // ElligatorSwift-encoded ephemeral pubkey
    uint8_t send_key[32];       // c1: initiator -> responder
    uint8_t recv_key[32];       // c2: responder -> initiator
    uint64_t send_nonce;
    uint64_t recv_nonce;
    bool handshake_complete;
    secp256k1_context *secp_ctx;
};

// --- Transport helpers ---

static int noise_recv_exact(esp_transport_handle_t transport, uint8_t *buf, int len)
{
    int received = 0;
    while (received < len) {
        int r = esp_transport_read(transport, (char *)buf + received, len - received, RECV_TIMEOUT_MS);
        if (r <= 0) {
            ESP_LOGE(TAG, "recv failed: r=%d", r);
            return -1;
        }
        received += r;
    }
    return 0;
}

static int noise_send_all(esp_transport_handle_t transport, const uint8_t *buf, int len)
{
    int ret = esp_transport_write(transport, (const char *)buf, len, TRANSPORT_TIMEOUT_MS);
    if (ret < 0) {
        ESP_LOGE(TAG, "send failed: ret=%d", ret);
        return -1;
    }
    return 0;
}

// --- Crypto helpers ---

// h = SHA-256(h || data)
static void mix_hash(uint8_t h[32], const uint8_t *data, size_t len)
{
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, h, 32);
    mbedtls_sha256_update(&sha, data, len);
    mbedtls_sha256_finish(&sha, h);
    mbedtls_sha256_free(&sha);
}

// HMAC-SHA256
static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32])
{
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&md, key, key_len);
    mbedtls_md_hmac_update(&md, data, data_len);
    mbedtls_md_hmac_finish(&md, out);
    mbedtls_md_free(&md);
}

// Noise HKDF-2: derive two 32-byte keys from chaining key and input key material
static void hkdf2(uint8_t ck[32], const uint8_t *ikm, size_t ikm_len,
                  uint8_t out1[32], uint8_t out2[32])
{
    uint8_t prk[32];
    hmac_sha256(ck, 32, ikm, ikm_len, prk);

    // out1 = HMAC(prk, 0x01)
    uint8_t one = 0x01;
    hmac_sha256(prk, 32, &one, 1, out1);

    // out2 = HMAC(prk, out1 || 0x02)
    uint8_t buf[33];
    memcpy(buf, out1, 32);
    buf[32] = 0x02;
    hmac_sha256(prk, 32, buf, 33, out2);
}

// Build 12-byte nonce: 4 zero bytes + 8-byte LE counter
static void build_nonce(uint64_t counter, uint8_t nonce[12])
{
    memset(nonce, 0, 4);
    for (int i = 0; i < 8; i++) {
        nonce[4 + i] = (uint8_t)(counter >> (i * 8));
    }
}

// ChaCha20-Poly1305 encrypt
// out must have room for pt_len + 16 bytes
static int noise_encrypt(const uint8_t key[32], uint64_t nonce_counter,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *plaintext, size_t pt_len,
                         uint8_t *out)
{
    uint8_t nonce[12];
    build_nonce(nonce_counter, nonce);

    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    mbedtls_chachapoly_setkey(&ctx, key);

    int ret = mbedtls_chachapoly_encrypt_and_tag(&ctx, pt_len,
                                                  nonce, aad, aad_len,
                                                  plaintext, out,
                                                  out + pt_len); // 16-byte tag appended
    mbedtls_chachapoly_free(&ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "encrypt failed: %d", ret);
        return -1;
    }
    return 0;
}

// ChaCha20-Poly1305 decrypt
// ciphertext includes 16-byte tag at end. out receives ct_len - 16 bytes.
static int noise_decrypt(const uint8_t key[32], uint64_t nonce_counter,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *ciphertext, size_t ct_len,
                         uint8_t *out)
{
    if (ct_len < 16) return -1;

    uint8_t nonce[12];
    build_nonce(nonce_counter, nonce);

    size_t pt_len = ct_len - 16;
    const uint8_t *tag = ciphertext + pt_len;

    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    mbedtls_chachapoly_setkey(&ctx, key);

    int ret = mbedtls_chachapoly_auth_decrypt(&ctx, pt_len,
                                               nonce, aad, aad_len,
                                               tag, ciphertext, out);
    mbedtls_chachapoly_free(&ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "decrypt failed: %d", ret);
        return -1;
    }
    return 0;
}

// --- Public API ---

sv2_noise_ctx_t *sv2_noise_create(void)
{
    sv2_noise_ctx_t *ctx = calloc(1, sizeof(sv2_noise_ctx_t));
    if (!ctx) return NULL;

    ctx->secp_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx->secp_ctx) {
        free(ctx);
        return NULL;
    }

    // Randomize the context for side-channel protection
    uint8_t seed[32];
    esp_fill_random(seed, sizeof(seed));
    secp256k1_context_randomize(ctx->secp_ctx, seed);

    return ctx;
}

void sv2_noise_destroy(sv2_noise_ctx_t *ctx)
{
    if (!ctx) return;

    // Securely zero sensitive material
    memset(ctx->e_priv, 0, 32);
    memset(ctx->send_key, 0, 32);
    memset(ctx->recv_key, 0, 32);

    if (ctx->secp_ctx) {
        secp256k1_context_destroy(ctx->secp_ctx);
    }
    free(ctx);
}

// Self-test: verify secp256k1 ellswift ECDH produces matching shared secrets
// Uses deterministic keys to test both sides of the ECDH
static bool sv2_noise_selftest(secp256k1_context *secp_ctx)
{
    // Fixed test keys (arbitrary 32-byte values)
    uint8_t seckey_a[32] = {0};
    uint8_t seckey_b[32] = {0};
    seckey_a[31] = 1;  // private key = 1 (valid for secp256k1)
    seckey_b[31] = 2;  // private key = 2

    uint8_t ell_a[64], ell_b[64];
    if (!secp256k1_ellswift_create(secp_ctx, ell_a, seckey_a, NULL) ||
        !secp256k1_ellswift_create(secp_ctx, ell_b, seckey_b, NULL)) {
        ESP_LOGE(TAG, "SELFTEST: ellswift_create failed");
        return false;
    }

    // A computes shared secret
    uint8_t shared_a[32];
    if (!secp256k1_ellswift_xdh(secp_ctx, shared_a, ell_a, ell_b, seckey_a, 0,
                                 secp256k1_ellswift_xdh_hash_function_bip324, NULL)) {
        ESP_LOGE(TAG, "SELFTEST: xdh from A failed");
        return false;
    }

    // B computes shared secret
    uint8_t shared_b[32];
    if (!secp256k1_ellswift_xdh(secp_ctx, shared_b, ell_a, ell_b, seckey_b, 1,
                                 secp256k1_ellswift_xdh_hash_function_bip324, NULL)) {
        ESP_LOGE(TAG, "SELFTEST: xdh from B failed");
        return false;
    }

    if (memcmp(shared_a, shared_b, 32) != 0) {
        ESP_LOGE(TAG, "SELFTEST: shared secrets MISMATCH!");
        ESP_LOGI(TAG, "shared_a:"); ESP_LOG_BUFFER_HEX_LEVEL(TAG, shared_a, 32, ESP_LOG_INFO);
        ESP_LOGI(TAG, "shared_b:"); ESP_LOG_BUFFER_HEX_LEVEL(TAG, shared_b, 32, ESP_LOG_INFO);
        return false;
    }

    ESP_LOGI(TAG, "SELFTEST: ellswift ECDH OK");
    return true;
}

int sv2_noise_handshake(sv2_noise_ctx_t *ctx, esp_transport_handle_t transport,
                        const uint8_t *authority_pubkey)
{
    int64_t hs_start_us = esp_timer_get_time();

    // Run self-test on first handshake attempt
    static bool selftest_done = false;
    if (!selftest_done) {
        if (!sv2_noise_selftest(ctx->secp_ctx)) {
            ESP_LOGE(TAG, "secp256k1 library self-test FAILED - library may be misconfigured");
            return -1;
        }
        selftest_done = true;
    }

    // Step 1: Initialize h and ck = SHA-256(protocol_name)
    mbedtls_sha256((const uint8_t *)NOISE_PROTOCOL_NAME,
                   strlen(NOISE_PROTOCOL_NAME), ctx->h, 0);
    memcpy(ctx->ck, ctx->h, 32);

    // Verify initial hash matches known reference value
    static const uint8_t expected_h[32] = {
        46, 180, 120, 129, 32, 142, 158, 238, 31, 102, 159, 103, 198, 110, 231, 14,
        169, 234, 136, 9, 13, 80, 63, 232, 48, 220, 75, 200, 62, 41, 191, 16
    };
    if (memcmp(ctx->h, expected_h, 32) != 0) {
        ESP_LOGE(TAG, "Initial protocol hash mismatch! SHA-256 implementation issue.");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, ctx->h, 32, ESP_LOG_ERROR);
        return -1;
    }

    // MixHash(prologue): SV2 uses an empty prologue, so h = SHA-256(h || "")
    // This is required by the Noise framework before processing any handshake tokens
    mix_hash(ctx->h, (const uint8_t *)"", 0);

    ESP_LOGI(TAG, "h after MixHash(prologue):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, ctx->h, 32, ESP_LOG_INFO);

    // Step 2: Generate ephemeral keypair
    ESP_LOGI(TAG, "Generating ephemeral keypair (ElligatorSwift)");
    esp_fill_random(ctx->e_priv, 32);

    uint8_t auxrand[32];
    esp_fill_random(auxrand, sizeof(auxrand));

    if (!secp256k1_ellswift_create(ctx->secp_ctx, ctx->e_pub_encoded,
                                    ctx->e_priv, auxrand)) {
        ESP_LOGE(TAG, "Failed to generate ephemeral key");
        return -1;
    }

    // Step 3: mix_hash(h, e_pub_encoded) — process 'e' token
    mix_hash(ctx->h, ctx->e_pub_encoded, 64);

    // Step 3b: Noise pattern requires EncryptAndHash(empty_payload) after 'e' token
    // Since k=None at this point, this is just mix_hash(h, empty) = h = SHA-256(h)
    mix_hash(ctx->h, (const uint8_t *)"", 0);

    // Step 4: Send our 64-byte encoded ephemeral pubkey (-> Act 1)
    ESP_LOGI(TAG, "-> Sending ephemeral public key (64 bytes)");
    ESP_LOGI(TAG, "e_pub (first 16):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, ctx->e_pub_encoded, 16, ESP_LOG_INFO);
    if (noise_send_all(transport, ctx->e_pub_encoded, 64) != 0) {
        ESP_LOGE(TAG, "Failed to send ephemeral key");
        return -1;
    }

    // Step 5: Receive 234 bytes (responder's message = Act 2)
    uint8_t resp[234];
    ESP_LOGI(TAG, "<- Waiting for server response...");
    if (noise_recv_exact(transport, resp, 234) != 0) {
        ESP_LOGE(TAG, "Failed to receive server Noise response");
        return -1;
    }
    ESP_LOGI(TAG, "<- Received server response (234 bytes)");

    // Step 6: Parse responder ephemeral (bytes 0-63), mix into hash
    const uint8_t *re_pub = resp;
    ESP_LOGI(TAG, "re_pub (first 16):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, re_pub, 16, ESP_LOG_INFO);
    mix_hash(ctx->h, re_pub, 64);

    // Step 7: ECDH #1 — our ephemeral with responder ephemeral
    uint8_t shared[32];
    if (!secp256k1_ellswift_xdh(ctx->secp_ctx, shared,
                                 ctx->e_pub_encoded, re_pub,
                                 ctx->e_priv, 0,
                                 secp256k1_ellswift_xdh_hash_function_bip324,
                                 NULL)) {
        ESP_LOGE(TAG, "ECDH key exchange #1 failed");
        return -1;
    }

    // Step 8: HKDF to derive ck and temp_k
    uint8_t temp_k[32];
    ESP_LOGI(TAG, "ECDH shared (first 16):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, shared, 16, ESP_LOG_INFO);
    ESP_LOGI(TAG, "ck before HKDF:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, ctx->ck, 16, ESP_LOG_INFO);
    hkdf2(ctx->ck, shared, 32, ctx->ck, temp_k);

    // DEBUG: Log all the values for comparison
    ESP_LOGI(TAG, "h (AAD for decrypt):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, ctx->h, 32, ESP_LOG_INFO);
    ESP_LOGI(TAG, "ck after HKDF:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, ctx->ck, 16, ESP_LOG_INFO);
    ESP_LOGI(TAG, "temp_k for decrypt:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, temp_k, 32, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Ciphertext (first 32 bytes):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, resp + 64, 32, ESP_LOG_INFO);
    ESP_LOGI(TAG, "MAC (last 16 of encrypted static):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, resp + 64 + 64, 16, ESP_LOG_INFO);

    // Step 9: Decrypt responder's encrypted static key (bytes 64-143 = 80 bytes)
    // 80 bytes = 64 bytes ciphertext + 16 bytes MAC
    uint8_t rs_static[64]; // responder static key (ElligatorSwift encoded)
    if (noise_decrypt(temp_k, 0, ctx->h, 32, resp + 64, 80, rs_static) != 0) {
        ESP_LOGE(TAG, "Failed to decrypt server static key (MAC verification failed)");
        return -1;
    }
    ESP_LOGI(TAG, "Decrypted server static key");

    // Step 10: mix_hash with the raw ciphertext+MAC (before decryption)
    mix_hash(ctx->h, resp + 64, 80);

    // Step 11: ECDH #2 — our ephemeral with responder static
    uint8_t shared2[32];
    if (!secp256k1_ellswift_xdh(ctx->secp_ctx, shared2,
                                 ctx->e_pub_encoded, rs_static,
                                 ctx->e_priv, 0,
                                 secp256k1_ellswift_xdh_hash_function_bip324,
                                 NULL)) {
        ESP_LOGE(TAG, "ECDH key exchange #2 failed");
        return -1;
    }

    // Step 12: HKDF to derive ck and temp_k2
    uint8_t temp_k2[32];
    hkdf2(ctx->ck, shared2, 32, ctx->ck, temp_k2);

    // Step 13: Decrypt signature message (bytes 144-233 = 90 bytes)
    // 90 bytes = 74 bytes plaintext + 16 bytes MAC
    uint8_t sig_msg[74];
    if (noise_decrypt(temp_k2, 0, ctx->h, 32, resp + 144, 90, sig_msg) != 0) {
        ESP_LOGE(TAG, "Failed to decrypt server certificate (MAC verification failed)");
        return -1;
    }

    // Step 14: Parse signature message
    // version(u16 LE) + valid_from(u32 LE) + not_valid_after(u32 LE) + schnorr_sig(64B)
    uint16_t cert_version = sig_msg[0] | ((uint16_t)sig_msg[1] << 8);
    uint32_t valid_from = sig_msg[2] | ((uint32_t)sig_msg[3] << 8) |
                          ((uint32_t)sig_msg[4] << 16) | ((uint32_t)sig_msg[5] << 24);
    uint32_t not_valid_after = sig_msg[6] | ((uint32_t)sig_msg[7] << 8) |
                               ((uint32_t)sig_msg[8] << 16) | ((uint32_t)sig_msg[9] << 24);
    const uint8_t *schnorr_sig = sig_msg + 10;

    ESP_LOGI(TAG, "Server certificate: version=%d, valid_from=%lu, not_valid_after=%lu",
             cert_version, valid_from, not_valid_after);

    // Step 15: Verify Schnorr signature if authority pubkey provided
    if (authority_pubkey) {
        ESP_LOGI(TAG, "Verifying server certificate (Schnorr/BIP-340)...");

        // Decode the responder's static public key from ElligatorSwift to get x-only bytes
        uint8_t sig_hash[32];
        {
            mbedtls_sha256_context sha;
            mbedtls_sha256_init(&sha);
            mbedtls_sha256_starts(&sha, 0);
            mbedtls_sha256_update(&sha, sig_msg, 2);   // version
            mbedtls_sha256_update(&sha, sig_msg + 2, 4); // valid_from
            mbedtls_sha256_update(&sha, sig_msg + 6, 4); // not_valid_after
            // Decode rs_static to get the actual 32-byte x-only pubkey
            secp256k1_pubkey decoded_pubkey;
            secp256k1_ellswift_decode(ctx->secp_ctx, &decoded_pubkey, rs_static);
            secp256k1_xonly_pubkey xonly_pk;
            int pk_parity;
            secp256k1_xonly_pubkey_from_pubkey(ctx->secp_ctx, &xonly_pk, &pk_parity, &decoded_pubkey);
            uint8_t xonly_bytes[32];
            secp256k1_xonly_pubkey_serialize(ctx->secp_ctx, xonly_bytes, &xonly_pk);
            mbedtls_sha256_update(&sha, xonly_bytes, 32);
            mbedtls_sha256_finish(&sha, sig_hash);
            mbedtls_sha256_free(&sha);
        }

        // Parse authority pubkey
        secp256k1_xonly_pubkey auth_pk;
        if (!secp256k1_xonly_pubkey_parse(ctx->secp_ctx, &auth_pk, authority_pubkey)) {
            ESP_LOGE(TAG, "Invalid authority public key");
            return -1;
        }

        // Verify Schnorr signature
        if (!secp256k1_schnorrsig_verify(ctx->secp_ctx, schnorr_sig,
                                          sig_hash, 32, &auth_pk)) {
            ESP_LOGE(TAG, "Server certificate INVALID - Schnorr signature verification failed!");
            return -1;
        }
        ESP_LOGI(TAG, "Server certificate verified OK");
    } else {
        ESP_LOGW(TAG, "Skipping certificate verification (no authority pubkey)");
    }

    // Step 16: Key split — derive send_key and recv_key
    hkdf2(ctx->ck, (const uint8_t *)"", 0, ctx->send_key, ctx->recv_key);

    // Step 17: Zero ephemeral private key and temporaries
    memset(ctx->e_priv, 0, 32);
    memset(ctx->ck, 0, 32);
    memset(ctx->h, 0, 32);

    ctx->send_nonce = 0;
    ctx->recv_nonce = 0;
    ctx->handshake_complete = true;

    float hs_elapsed_ms = (float)(esp_timer_get_time() - hs_start_us) / 1000.0f;
    ESP_LOGI(TAG, "Noise handshake complete (%.0f ms)", hs_elapsed_ms);
    return 0;
}

int sv2_noise_send(sv2_noise_ctx_t *ctx, esp_transport_handle_t transport,
                   const uint8_t *frame, int frame_len)
{
    if (!ctx || !ctx->handshake_complete || frame_len < SV2_FRAME_HEADER_SIZE) {
        return -1;
    }

    // Encrypt header (6 bytes -> 22 bytes)
    uint8_t enc_hdr[22];
    if (noise_encrypt(ctx->send_key, ctx->send_nonce++, NULL, 0,
                      frame, SV2_FRAME_HEADER_SIZE, enc_hdr) != 0) {
        return -1;
    }

    if (noise_send_all(transport, enc_hdr, 22) != 0) {
        return -1;
    }

    // Encrypt payload if present
    int payload_len = frame_len - SV2_FRAME_HEADER_SIZE;
    if (payload_len > 0) {
        uint8_t *enc_payload = malloc(payload_len + 16);
        if (!enc_payload) return -1;

        if (noise_encrypt(ctx->send_key, ctx->send_nonce++, NULL, 0,
                          frame + SV2_FRAME_HEADER_SIZE, payload_len,
                          enc_payload) != 0) {
            free(enc_payload);
            return -1;
        }

        int ret = noise_send_all(transport, enc_payload, payload_len + 16);
        free(enc_payload);
        if (ret != 0) return -1;
    }

    return 0;
}

int sv2_noise_recv(sv2_noise_ctx_t *ctx, esp_transport_handle_t transport,
                   uint8_t hdr_out[6], uint8_t *payload_out,
                   int max_payload_len, int *payload_len_out)
{
    if (!ctx || !ctx->handshake_complete) {
        return -1;
    }

    *payload_len_out = 0;

    // Receive and decrypt header (22 bytes -> 6 bytes)
    uint8_t enc_hdr[22];
    if (noise_recv_exact(transport, enc_hdr, 22) != 0) {
        return -1;
    }

    if (noise_decrypt(ctx->recv_key, ctx->recv_nonce++, NULL, 0,
                      enc_hdr, 22, hdr_out) != 0) {
        ESP_LOGE(TAG, "Failed to decrypt frame header");
        return -1;
    }

    // Parse header to get msg_length
    sv2_frame_header_t hdr;
    sv2_parse_frame_header(hdr_out, &hdr);

    if (hdr.msg_length == 0) {
        return 0;
    }

    if ((int)hdr.msg_length > max_payload_len) {
        ESP_LOGE(TAG, "Payload too large: %lu > %d", hdr.msg_length, max_payload_len);
        return -1;
    }

    // Receive and decrypt payload
    int enc_len = hdr.msg_length + 16;
    uint8_t *enc_payload = malloc(enc_len);
    if (!enc_payload) return -1;

    if (noise_recv_exact(transport, enc_payload, enc_len) != 0) {
        free(enc_payload);
        return -1;
    }

    if (noise_decrypt(ctx->recv_key, ctx->recv_nonce++, NULL, 0,
                      enc_payload, enc_len, payload_out) != 0) {
        ESP_LOGE(TAG, "Failed to decrypt payload");
        free(enc_payload);
        return -1;
    }

    free(enc_payload);
    *payload_len_out = hdr.msg_length;
    return 0;
}
