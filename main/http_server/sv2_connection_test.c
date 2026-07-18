#include "sv2_connection_test.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "stratum_socket.h"

static const char *TAG = "sv2_test";

#define SV2_TEST_CONNECT_TIMEOUT_MS 5000
#define SV2_TEST_READ_TIMEOUT_MS    3000
#define SV2_NOISE_ACT1_LEN          64   // ephemeral key the initiator sends
#define SV2_NOISE_ACT2_LEN          234  // responder's reply for Noise_NX

static void set_result(sv2_test_result_t *out, bool supported, const char *msg)
{
    out->supported = supported;
    strncpy(out->message, msg, sizeof(out->message) - 1);
    out->message[sizeof(out->message) - 1] = '\0';
}

// A text protocol (SV1 JSON-RPC, HTTP, ...) replies with printable ASCII; the
// binary Noise response almost certainly has non-printable bytes early on.
static bool looks_like_text(const uint8_t *buf, int len)
{
    int n = len < 16 ? len : 16;
    for (int i = 0; i < n; i++) {
        uint8_t c = buf[i];
        bool printable = (c >= 0x20 && c <= 0x7e) || c == '\r' || c == '\n' || c == '\t';
        if (!printable) {
            return false;
        }
    }
    return true;
}

void sv2_test_connection(const char *host, uint16_t port, sv2_test_result_t *out)
{
    if (!host || host[0] == '\0' || port == 0) {
        set_result(out, false, "Invalid pool address");
        return;
    }

    stratum_connection_info_t conn_info;
    if (stratum_socket_resolve(host, port, &conn_info) != ESP_OK) {
        set_result(out, false, "Could not resolve pool host");
        return;
    }

    esp_transport_handle_t transport = esp_transport_tcp_init();
    if (!transport) {
        set_result(out, false, "Internal error (transport init)");
        return;
    }

    if (esp_transport_connect(transport, conn_info.host_ip, port, SV2_TEST_CONNECT_TIMEOUT_MS) != 0) {
        set_result(out, false, "Pool unreachable");
        esp_transport_destroy(transport);
        return;
    }

    // Noise_NX Act 1: the initiator sends a 64-byte ElligatorSwift-encoded
    // ephemeral public key. ElligatorSwift decodes any 64 bytes to a valid
    // curve point, so a throwaway random value is enough to make an SV2
    // responder reply with its Act 2 — we only care whether it answers, not
    // about completing the handshake, so this needs no crypto on our side.
    uint8_t act1[SV2_NOISE_ACT1_LEN];
    esp_fill_random(act1, sizeof(act1));
    if (esp_transport_write(transport, (const char *)act1, sizeof(act1), SV2_TEST_CONNECT_TIMEOUT_MS) <= 0) {
        set_result(out, false, "Pool unreachable");
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        return;
    }

    // An SV2 pool replies with the 234-byte Act 2. An SV1 / text pool either
    // ignores the binary blob (read times out) or answers with text.
    uint8_t resp[SV2_NOISE_ACT2_LEN];
    int got = esp_transport_read(transport, (char *)resp, sizeof(resp), SV2_TEST_READ_TIMEOUT_MS);

    esp_transport_close(transport);
    esp_transport_destroy(transport);

    if (got <= 0) {
        ESP_LOGI(TAG, "%s:%u did not answer the SV2 handshake", host, port);
        set_result(out, false, "Pool did not respond to a Stratum V2 handshake — it likely only supports Stratum V1");
        return;
    }

    if (looks_like_text(resp, got)) {
        ESP_LOGI(TAG, "%s:%u replied with a text response (%d bytes)", host, port, got);
        set_result(out, false, "Pool replied with a non-SV2 response — it likely only supports Stratum V1");
        return;
    }

    ESP_LOGI(TAG, "%s:%u answered the SV2 handshake (%d bytes)", host, port, got);
    set_result(out, true, "Pool responded to the Stratum V2 handshake — Stratum V2 is supported");
}
