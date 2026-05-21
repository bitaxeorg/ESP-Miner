#include "gridpool_direct_builder.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coinbase_decoder.h"
#include "esp_check.h"
#include "sdkconfig.h"
#if CONFIG_SPIRAM
#include "esp_heap_caps.h"
#include "esp_psram.h"
#endif
#include "libbase58.h"
#include "mbedtls/sha256.h"
#include "segwit_addr.h"
#include "utils.h"

#define GRIDPOOL_DIRECT_COINBASE_VERSION 2
#define GRIDPOOL_DIRECT_SEQUENCE_FINAL 0xffffffffU
#define GRIDPOOL_DIRECT_MAX_COINBASE_SCRIPTSIG_LEN 100

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    bool count_only;
} byte_writer_t;

static bool my_sha256(void *digest, const void *data, size_t datasz)
{
    mbedtls_sha256(data, datasz, digest, 0);
    return true;
}

static void ensure_base58_init(void)
{
    if (b58_sha256_impl == NULL) {
        b58_sha256_impl = my_sha256;
    }
}

static void *gridpool_malloc(size_t size)
{
    void *ptr = NULL;
#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
#endif
    return ptr ? ptr : malloc(size);
}

static void *gridpool_realloc(void *ptr, size_t size)
{
#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) {
        return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
#endif
    return realloc(ptr, size);
}

static bool writer_reserve(byte_writer_t *writer, size_t extra)
{
    if (writer->len > SIZE_MAX - extra) {
        return false;
    }
    if (!writer->count_only && writer->len + extra > writer->cap) {
        return false;
    }
    return true;
}

static bool writer_put_u8(byte_writer_t *writer, uint8_t value)
{
    if (!writer_reserve(writer, 1)) {
        return false;
    }
    if (!writer->count_only) {
        writer->data[writer->len] = value;
    }
    writer->len++;
    return true;
}

static bool writer_put_bytes(byte_writer_t *writer, const uint8_t *data, size_t len)
{
    if (!writer_reserve(writer, len)) {
        return false;
    }
    if (!writer->count_only && len > 0) {
        memcpy(writer->data + writer->len, data, len);
    }
    writer->len += len;
    return true;
}

static bool writer_put_u16_le(byte_writer_t *writer, uint16_t value)
{
    return writer_put_u8(writer, value & 0xff) &&
           writer_put_u8(writer, (value >> 8) & 0xff);
}

static bool writer_put_u32_le(byte_writer_t *writer, uint32_t value)
{
    return writer_put_u8(writer, value & 0xff) &&
           writer_put_u8(writer, (value >> 8) & 0xff) &&
           writer_put_u8(writer, (value >> 16) & 0xff) &&
           writer_put_u8(writer, (value >> 24) & 0xff);
}

static bool writer_put_u64_le(byte_writer_t *writer, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        if (!writer_put_u8(writer, (value >> (i * 8)) & 0xff)) {
            return false;
        }
    }
    return true;
}

static bool writer_put_varint(byte_writer_t *writer, uint64_t value)
{
    if (value < 0xfd) {
        return writer_put_u8(writer, (uint8_t)value);
    }
    if (value <= 0xffff) {
        return writer_put_u8(writer, 0xfd) && writer_put_u16_le(writer, (uint16_t)value);
    }
    if (value <= 0xffffffffULL) {
        return writer_put_u8(writer, 0xfe) && writer_put_u32_le(writer, (uint32_t)value);
    }
    return writer_put_u8(writer, 0xff) && writer_put_u64_le(writer, value);
}

size_t gridpool_direct_varint_len(uint64_t value)
{
    if (value < 0xfd) {
        return 1;
    }
    if (value <= 0xffff) {
        return 3;
    }
    if (value <= 0xffffffffULL) {
        return 5;
    }
    return 9;
}

static bool writer_put_push(byte_writer_t *writer, const uint8_t *data, size_t len)
{
    if (len > 75) {
        return false;
    }
    return writer_put_u8(writer, (uint8_t)len) && writer_put_bytes(writer, data, len);
}

static bool is_hex_string_len(const char *text, size_t len)
{
    if (!text || strlen(text) != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)text[i])) {
            return false;
        }
    }
    return true;
}

static esp_err_t parse_hex_exact(const char *hex, size_t hex_len, uint8_t *out, size_t out_len)
{
    if (!hex || !out || hex_len != out_len * 2) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < hex_len; i++) {
        if (!isxdigit((unsigned char)hex[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (hex2bin(hex, out, out_len) != out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t parse_hex_span(const char *hex, size_t hex_len, uint8_t *out, size_t out_len)
{
    if (!hex || !out || hex_len != out_len * 2) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < hex_len; i++) {
        if (!isxdigit((unsigned char)hex[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (size_t i = 0; i < out_len; i++) {
        uint8_t high = (uint8_t)(isdigit((unsigned char)hex[i * 2])
                                     ? hex[i * 2] - '0'
                                     : (tolower((unsigned char)hex[i * 2]) - 'a' + 10));
        uint8_t low = (uint8_t)(isdigit((unsigned char)hex[i * 2 + 1])
                                    ? hex[i * 2 + 1] - '0'
                                    : (tolower((unsigned char)hex[i * 2 + 1]) - 'a' + 10));
        out[i] = (high << 4) | low;
    }
    return ESP_OK;
}

static void reverse_bytes_32(const uint8_t src[32], uint8_t dest[32])
{
    for (int i = 0; i < 32; i++) {
        dest[i] = src[31 - i];
    }
}

static void display_hash_hex_to_internal(const char *display_hex, uint8_t internal[32])
{
    uint8_t display[32];
    hex2bin(display_hex, display, sizeof(display));
    reverse_bytes_32(display, internal);
}

static esp_err_t display_hash_to_stratum_prevhash(const char *display_hex, char out[65])
{
    uint8_t display[32];
    uint8_t stratum_words[32];

    esp_err_t err = parse_hex_exact(display_hex, 64, display, sizeof(display));
    if (err != ESP_OK) {
        return err;
    }
    reverse_32bit_words(display, stratum_words);
    if (bin2hex(stratum_words, sizeof(stratum_words), out, 65) != 64) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t parse_u32_hex(const char *hex, uint32_t *out)
{
    if (!hex || !out || !is_hex_string_len(hex, 8)) {
        return ESP_ERR_INVALID_ARG;
    }

    char *end = NULL;
    unsigned long value = strtoul(hex, &end, 16);
    if (!end || *end != '\0' || value > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (uint32_t)value;
    return ESP_OK;
}

static const char *find_json_value(const char *json, const char *key)
{
    char pattern[96];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (!json || !key || written <= 0 || (size_t)written >= sizeof(pattern)) {
        return NULL;
    }

    const char *match = strstr(json, pattern);
    if (!match) {
        return NULL;
    }

    const char *colon = strchr(match + written, ':');
    if (!colon) {
        return NULL;
    }

    const char *value = colon + 1;
    while (*value && isspace((unsigned char)*value)) {
        value++;
    }
    return value;
}

static bool scan_json_u64_in_span(const char *start, const char *end, const char *key, uint64_t *out)
{
    char pattern[96];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (!start || !end || !key || !out || written <= 0 || (size_t)written >= sizeof(pattern)) {
        return false;
    }

    const char *match = start;
    while (match && match < end) {
        match = strstr(match, pattern);
        if (!match || match >= end) {
            return false;
        }
        const char *colon = strchr(match + written, ':');
        if (!colon || colon >= end) {
            return false;
        }
        const char *value = colon + 1;
        while (value < end && isspace((unsigned char)*value)) {
            value++;
        }
        if (value >= end || !isdigit((unsigned char)*value)) {
            match += written;
            continue;
        }
        *out = strtoull(value, NULL, 10);
        return true;
    }

    return false;
}

static bool scan_json_string_in_span(const char *start, const char *end, const char *key, char *dest, size_t dest_len)
{
    char pattern[96];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (!start || !end || !key || !dest || dest_len == 0 || written <= 0 || (size_t)written >= sizeof(pattern)) {
        return false;
    }
    dest[0] = '\0';

    const char *match = start;
    while (match && match < end) {
        match = strstr(match, pattern);
        if (!match || match >= end) {
            return false;
        }

        const char *colon = strchr(match + written, ':');
        if (!colon || colon >= end) {
            return false;
        }
        const char *value = colon + 1;
        while (value < end && isspace((unsigned char)*value)) {
            value++;
        }
        if (value >= end || *value != '"') {
            match += written;
            continue;
        }

        const char *src = value + 1;
        size_t offset = 0;
        bool escaped = false;
        while (src < end && *src) {
            if (escaped) {
                if (offset + 1 >= dest_len) {
                    dest[offset] = '\0';
                    return false;
                }
                dest[offset++] = *src++;
                escaped = false;
                continue;
            }
            if (*src == '\\') {
                escaped = true;
                src++;
                continue;
            }
            if (*src == '"') {
                dest[offset] = '\0';
                return true;
            }
            if (offset + 1 >= dest_len) {
                dest[offset] = '\0';
                return false;
            }
            dest[offset++] = *src++;
        }
        dest[offset] = '\0';
        return false;
    }

    return false;
}

static bool find_json_string_value_span(const char *start,
                                        const char *end,
                                        const char *key,
                                        const char **value_start,
                                        size_t *value_len)
{
    char pattern[96];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (!start || !end || !key || !value_start || !value_len ||
        written <= 0 || (size_t)written >= sizeof(pattern)) {
        return false;
    }

    const char *match = start;
    while (match && match < end) {
        match = strstr(match, pattern);
        if (!match || match >= end) {
            return false;
        }

        const char *colon = strchr(match + written, ':');
        if (!colon || colon >= end) {
            return false;
        }
        const char *value = colon + 1;
        while (value < end && isspace((unsigned char)*value)) {
            value++;
        }
        if (value >= end || *value != '"') {
            match += written;
            continue;
        }

        const char *src = value + 1;
        bool escaped = false;
        while (src < end && *src) {
            if (escaped) {
                escaped = false;
                src++;
                continue;
            }
            if (*src == '\\') {
                escaped = true;
                src++;
                continue;
            }
            if (*src == '"') {
                *value_start = value + 1;
                *value_len = (size_t)(src - (value + 1));
                return true;
            }
            src++;
        }
        return false;
    }

    return false;
}

static bool scan_json_string(const char *json, const char *key, char *dest, size_t dest_len)
{
    if (!json) {
        return false;
    }
    return scan_json_string_in_span(json, json + strlen(json), key, dest, dest_len);
}

static bool scan_json_u64(const char *json, const char *key, uint64_t *out)
{
    if (!json) {
        return false;
    }
    return scan_json_u64_in_span(json, json + strlen(json), key, out);
}

static void set_parse_stage(char *stage, size_t stage_len, const char *value)
{
    if (stage && stage_len > 0) {
        snprintf(stage, stage_len, "%s", value ? value : "");
    }
}

static const char *find_array_start(const char *json, const char *key)
{
    const char *value = find_json_value(json, key);
    if (!value || *value != '[') {
        return NULL;
    }
    return value;
}

static const char *skip_string(const char *p)
{
    bool escaped = false;
    for (p++; *p; p++) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (*p == '\\') {
            escaped = true;
        } else if (*p == '"') {
            return p + 1;
        }
    }
    return p;
}

static const char *find_matching_object_end(const char *object_start)
{
    int depth = 0;
    for (const char *p = object_start; *p; p++) {
        if (*p == '"') {
            p = skip_string(p) - 1;
            continue;
        }
        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                return p + 1;
            }
        }
    }
    return NULL;
}

static esp_err_t count_transaction_entries(const char *transactions, size_t *count, size_t *tx_data_len)
{
    int array_depth = 1;
    int object_depth = 0;
    size_t txids = 0;
    size_t data_total = 0;

    for (const char *p = transactions + 1; *p && array_depth > 0; p++) {
        if (*p == '"') {
            p = skip_string(p) - 1;
            continue;
        }
        if (*p == '[') {
            array_depth++;
        } else if (*p == ']') {
            array_depth--;
        } else if (*p == '{') {
            object_depth++;
            if (array_depth == 1 && object_depth == 1) {
                const char *end = find_matching_object_end(p);
                char txid[65];
                const char *data_hex = NULL;
                size_t data_hex_len = 0;
                if (!end || !scan_json_string_in_span(p, end, "txid", txid, sizeof(txid)) || !is_hex_string_len(txid, 64)) {
                    return ESP_ERR_INVALID_ARG;
                }
                if (!find_json_string_value_span(p, end, "data", &data_hex, &data_hex_len) ||
                    (data_hex_len % 2) != 0) {
                    return ESP_ERR_INVALID_ARG;
                }
                if (data_total > SIZE_MAX - (data_hex_len / 2)) {
                    return ESP_ERR_INVALID_SIZE;
                }
                data_total += data_hex_len / 2;
                txids++;
                p = end - 1;
                object_depth = 0;
            }
        } else if (*p == '}') {
            object_depth--;
        }
    }

    if (array_depth != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = txids;
    if (tx_data_len) {
        *tx_data_len = data_total;
    }
    return ESP_OK;
}

static void tx_bundle_free(gridpool_direct_tx_bundle_t *bundle)
{
    if (!bundle) {
        return;
    }
    free(bundle->tx_data);
    free(bundle->tx_offsets);
    free(bundle->tx_lengths);
    free(bundle->tx_weights);
    free(bundle);
}

void gridpool_direct_tx_bundle_retain(gridpool_direct_tx_bundle_t *bundle)
{
    if (bundle) {
        __atomic_add_fetch(&bundle->ref_count, 1, __ATOMIC_RELAXED);
    }
}

void gridpool_direct_tx_bundle_release(gridpool_direct_tx_bundle_t *bundle)
{
    if (!bundle) {
        return;
    }
    if (__atomic_sub_fetch(&bundle->ref_count, 1, __ATOMIC_ACQ_REL) == 0) {
        tx_bundle_free(bundle);
    }
}

static esp_err_t alloc_tx_bundle(size_t tx_count,
                                 size_t tx_data_len,
                                 uint8_t *tx_data_owner,
                                 gridpool_direct_tx_bundle_t **bundle_out)
{
    if (!bundle_out) {
        return ESP_ERR_INVALID_ARG;
    }
    *bundle_out = NULL;

    gridpool_direct_tx_bundle_t *bundle = calloc(1, sizeof(*bundle));
    if (!bundle) {
        return ESP_ERR_NO_MEM;
    }
    bundle->ref_count = 1;
    bundle->tx_count = tx_count;
    bundle->tx_data_len = tx_data_len;

    if (tx_count > 0) {
        bundle->tx_offsets = gridpool_malloc(tx_count * sizeof(uint32_t));
        bundle->tx_lengths = gridpool_malloc(tx_count * sizeof(uint32_t));
        bundle->tx_weights = gridpool_malloc(tx_count * sizeof(uint32_t));
        if (!bundle->tx_offsets || !bundle->tx_lengths || !bundle->tx_weights) {
            tx_bundle_free(bundle);
            return ESP_ERR_NO_MEM;
        }
    }
    if (tx_data_owner) {
        bundle->tx_data = tx_data_owner;
    } else if (tx_data_len > 0) {
        bundle->tx_data = gridpool_malloc(tx_data_len);
        if (!bundle->tx_data) {
            tx_bundle_free(bundle);
            return ESP_ERR_NO_MEM;
        }
    }

    *bundle_out = bundle;
    return ESP_OK;
}

static esp_err_t parse_transactions(const char *transactions,
                                    uint8_t *txid_hashes,
                                    size_t tx_count,
                                    gridpool_direct_tx_bundle_t *bundle,
                                    uint32_t *tx_weight_total)
{
    int array_depth = 1;
    int object_depth = 0;
    size_t index = 0;
    size_t data_offset = 0;
    uint64_t weight_total = 0;

    for (const char *p = transactions + 1; *p && array_depth > 0; p++) {
        if (*p == '"') {
            p = skip_string(p) - 1;
            continue;
        }
        if (*p == '[') {
            array_depth++;
        } else if (*p == ']') {
            array_depth--;
        } else if (*p == '{') {
            object_depth++;
            if (array_depth == 1 && object_depth == 1) {
                const char *end = find_matching_object_end(p);
                char txid[65];
                const char *data_hex = NULL;
                size_t data_hex_len = 0;
                uint64_t weight = 0;
                if (!end || index >= tx_count ||
                    !scan_json_string_in_span(p, end, "txid", txid, sizeof(txid)) ||
                    !is_hex_string_len(txid, 64) ||
                    !find_json_string_value_span(p, end, "data", &data_hex, &data_hex_len) ||
                    (data_hex_len % 2) != 0 ||
                    !scan_json_u64_in_span(p, end, "weight", &weight) ||
                    weight > UINT32_MAX) {
                    return ESP_ERR_INVALID_ARG;
                }
                display_hash_hex_to_internal(txid, txid_hashes + index * 32);

                size_t data_len = data_hex_len / 2;
                size_t tx_offset = data_offset;
                if (!bundle || !bundle->tx_data || tx_offset > bundle->tx_data_len ||
                    data_len > bundle->tx_data_len - tx_offset ||
                    tx_offset > UINT32_MAX || data_len > UINT32_MAX ||
                    parse_hex_span(data_hex, data_hex_len, bundle->tx_data + tx_offset, data_len) != ESP_OK) {
                    return ESP_ERR_INVALID_ARG;
                }
                bundle->tx_offsets[index] = (uint32_t)tx_offset;
                bundle->tx_lengths[index] = (uint32_t)data_len;
                bundle->tx_weights[index] = (uint32_t)weight;
                if (weight_total > UINT32_MAX - weight) {
                    return ESP_ERR_INVALID_SIZE;
                }
                weight_total += weight;
                data_offset += data_len;
                index++;
                p = end - 1;
                object_depth = 0;
            }
        } else if (*p == '}') {
            object_depth--;
        }
    }

    if (index != tx_count || !bundle || data_offset != bundle->tx_data_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (tx_weight_total) {
        *tx_weight_total = (uint32_t)weight_total;
    }
    return ESP_OK;
}

static esp_err_t build_bip34_height_script(uint32_t height, uint8_t *height_script, size_t *height_script_len)
{
    if (!height_script || !height_script_len) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t encoded[5];
    size_t len = 0;
    uint32_t value = height;
    while (value > 0) {
        encoded[len++] = value & 0xff;
        value >>= 8;
    }
    if (len == 0) {
        encoded[len++] = 0;
    }
    if (encoded[len - 1] & 0x80) {
        encoded[len++] = 0;
    }
    if (len > 5) {
        return ESP_ERR_INVALID_ARG;
    }

    height_script[0] = (uint8_t)len;
    memcpy(height_script + 1, encoded, len);
    *height_script_len = len + 1;
    return ESP_OK;
}

static esp_err_t build_scriptsig(uint32_t height,
                                 const uint8_t *extranonce,
                                 size_t extranonce_len,
                                 const char *tag,
                                 uint8_t *scriptsig,
                                 size_t *scriptsig_len)
{
    if (!scriptsig || !scriptsig_len || extranonce_len > 75) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *effective_tag = tag ? tag : GRIDPOOL_DIRECT_DEFAULT_TAG;
    size_t tag_len = strlen(effective_tag);
    if (tag_len > 75) {
        return ESP_ERR_INVALID_ARG;
    }

    byte_writer_t writer = {
        .data = scriptsig,
        .len = 0,
        .cap = GRIDPOOL_DIRECT_MAX_COINBASE_SCRIPTSIG_LEN,
        .count_only = false,
    };

    uint8_t height_script[6];
    size_t height_script_len = 0;
    ESP_RETURN_ON_ERROR(build_bip34_height_script(height, height_script, &height_script_len), "gridpool_builder", "height script");

    if (!writer_put_bytes(&writer, height_script, height_script_len) ||
        !writer_put_push(&writer, (const uint8_t *)effective_tag, tag_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (extranonce_len > 0 && (!extranonce || !writer_put_push(&writer, extranonce, extranonce_len))) {
        return ESP_ERR_INVALID_ARG;
    }

    *scriptsig_len = writer.len;
    return ESP_OK;
}

static esp_err_t decode_base58_script(const char *address, uint8_t *script, size_t script_cap, size_t *script_len)
{
    uint8_t decoded[25];
    size_t decoded_len = sizeof(decoded);

    ensure_base58_init();
    if (!b58tobin(decoded, &decoded_len, address, 0) || decoded_len != sizeof(decoded)) {
        return ESP_ERR_INVALID_ARG;
    }

    int version = b58check(decoded, decoded_len, address, 0);
    if (version < 0) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint8_t *hash160 = decoded + 1;
    if (version == 0x00 || version == 0x6f) {
        if (script_cap < 25) {
            return ESP_ERR_INVALID_SIZE;
        }
        script[0] = OP_DUP;
        script[1] = OP_HASH160;
        script[2] = OP_PUSHDATA_20;
        memcpy(script + 3, hash160, 20);
        script[23] = OP_EQUALVERIFY;
        script[24] = OP_CHECKSIG;
        *script_len = 25;
        return ESP_OK;
    }

    if (version == 0x05 || version == 0xc4) {
        if (script_cap < 23) {
            return ESP_ERR_INVALID_SIZE;
        }
        script[0] = OP_HASH160;
        script[1] = OP_PUSHDATA_20;
        memcpy(script + 2, hash160, 20);
        script[22] = OP_EQUAL;
        *script_len = 23;
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t decode_segwit_script(const char *address, uint8_t *script, size_t script_cap, size_t *script_len)
{
    static const char *hrps[] = { "bc", "tb", "bcrt" };
    uint8_t program[40];
    size_t program_len = 0;
    int version = 0;

    for (size_t i = 0; i < sizeof(hrps) / sizeof(hrps[0]); i++) {
        program_len = sizeof(program);
        if (!segwit_addr_decode(&version, program, &program_len, hrps[i], address)) {
            continue;
        }

        if (version < 0 || version > 16 || program_len < 2 || program_len > 40) {
            return ESP_ERR_INVALID_ARG;
        }
        if (version == 0 && program_len != 20 && program_len != 32) {
            return ESP_ERR_INVALID_ARG;
        }
        if (script_cap < program_len + 2) {
            return ESP_ERR_INVALID_SIZE;
        }

        script[0] = version == 0 ? OP_0 : (uint8_t)(OP_1 + version - 1);
        script[1] = (uint8_t)program_len;
        memcpy(script + 2, program, program_len);
        *script_len = program_len + 2;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

esp_err_t gridpool_direct_address_to_script(const char *address,
                                            uint8_t *script,
                                            size_t script_cap,
                                            size_t *script_len)
{
    if (!address || !address[0] || !script || !script_len) {
        return ESP_ERR_INVALID_ARG;
    }
    *script_len = 0;

    esp_err_t err = decode_segwit_script(address, script, script_cap, script_len);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    return decode_base58_script(address, script, script_cap, script_len);
}

static esp_err_t parse_template_json_internal(char *json,
                                              gridpool_direct_template_t *template_out,
                                              char *stage,
                                              size_t stage_len,
                                              bool tx_data_in_place)
{
    if (!json || !template_out) {
        set_parse_stage(stage, stage_len, "input");
        return ESP_ERR_INVALID_ARG;
    }

    memset(template_out, 0, sizeof(*template_out));
    set_parse_stage(stage, stage_len, "version");

    uint64_t value = 0;
    char bits[9] = { 0 };

    if (!scan_json_u64(json, "version", &value) || value > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    template_out->version = (uint32_t)value;

    set_parse_stage(stage, stage_len, "previousblockhash");
    if (!scan_json_string(json, "previousblockhash", template_out->previous_block_hash, sizeof(template_out->previous_block_hash)) ||
        !is_hex_string_len(template_out->previous_block_hash, 64)) {
        return ESP_ERR_INVALID_ARG;
    }

    set_parse_stage(stage, stage_len, "bits");
    if (!scan_json_string(json, "bits", bits, sizeof(bits)) || parse_u32_hex(bits, &template_out->nbits) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    set_parse_stage(stage, stage_len, "curtime");
    if (!scan_json_u64(json, "curtime", &value) || value > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    template_out->curtime = (uint32_t)value;

    set_parse_stage(stage, stage_len, "height");
    if (!scan_json_u64(json, "height", &value) || value > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    template_out->height = (uint32_t)value;

    set_parse_stage(stage, stage_len, "weightlimit");
    if (scan_json_u64(json, "weightlimit", &value) && value <= UINT32_MAX) {
        template_out->weight_limit = (uint32_t)value;
    } else {
        template_out->weight_limit = 4000000U;
    }

    set_parse_stage(stage, stage_len, "coinbasevalue");
    if (!scan_json_u64(json, "coinbasevalue", &template_out->coinbase_value)) {
        return ESP_ERR_INVALID_ARG;
    }

    set_parse_stage(stage, stage_len, "default_witness_commitment");
    scan_json_string(json, "default_witness_commitment",
                     template_out->default_witness_commitment,
                     sizeof(template_out->default_witness_commitment));
    if (template_out->default_witness_commitment[0] != '\0') {
        size_t commitment_len = strlen(template_out->default_witness_commitment);
        if ((commitment_len % 2) != 0 ||
            commitment_len / 2 > GRIDPOOL_DIRECT_MAX_SCRIPT_PUBKEY_LEN ||
            !is_hex_string_len(template_out->default_witness_commitment, commitment_len)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    set_parse_stage(stage, stage_len, "transactions array");
    const char *transactions = find_array_start(json, "transactions");
    if (!transactions) {
        return ESP_ERR_INVALID_ARG;
    }

    set_parse_stage(stage, stage_len, "transactions count");
    size_t tx_data_len = 0;
    esp_err_t err = count_transaction_entries(transactions, &template_out->tx_count, &tx_data_len);
    if (err != ESP_OK) {
        return err;
    }

    if (template_out->tx_count > 0) {
        if (template_out->tx_count > SIZE_MAX / 32) {
            set_parse_stage(stage, stage_len, "transactions size");
            return ESP_ERR_INVALID_SIZE;
        }
        set_parse_stage(stage, stage_len, "transactions allocate");
        template_out->txid_hashes = gridpool_malloc(template_out->tx_count * 32);
        if (!template_out->txid_hashes) {
            return ESP_ERR_NO_MEM;
        }
        err = alloc_tx_bundle(template_out->tx_count,
                              tx_data_len,
                              tx_data_in_place ? (uint8_t *)json : NULL,
                              &template_out->tx_bundle);
        if (err != ESP_OK) {
            gridpool_direct_template_free(template_out);
            return err;
        }
        set_parse_stage(stage, stage_len, "transactions txids");
        err = parse_transactions(transactions,
                                 template_out->txid_hashes,
                                 template_out->tx_count,
                                 template_out->tx_bundle,
                                 &template_out->tx_weight_total);
        if (err != ESP_OK) {
            gridpool_direct_template_free(template_out);
            return err;
        }
        if (tx_data_in_place && tx_data_len > 0) {
            uint8_t *shrunk = gridpool_realloc(template_out->tx_bundle->tx_data, tx_data_len);
            if (shrunk) {
                template_out->tx_bundle->tx_data = shrunk;
            }
        }
    } else {
        err = alloc_tx_bundle(0,
                              0,
                              tx_data_in_place ? (uint8_t *)json : NULL,
                              &template_out->tx_bundle);
        if (err != ESP_OK) {
            gridpool_direct_template_free(template_out);
            return err;
        }
    }

    set_parse_stage(stage, stage_len, "ok");
    return ESP_OK;
}

esp_err_t gridpool_direct_parse_template_json_with_stage(const char *json,
                                                         gridpool_direct_template_t *template_out,
                                                         char *stage,
                                                         size_t stage_len)
{
    return parse_template_json_internal((char *)json, template_out, stage, stage_len, false);
}

esp_err_t gridpool_direct_parse_template_json_buffer(char *json,
                                                     gridpool_direct_template_t *template_out,
                                                     char *stage,
                                                     size_t stage_len)
{
    return parse_template_json_internal(json, template_out, stage, stage_len, true);
}

esp_err_t gridpool_direct_parse_template_json(const char *json,
                                              gridpool_direct_template_t *template_out)
{
    return gridpool_direct_parse_template_json_with_stage(json, template_out, NULL, 0);
}

void gridpool_direct_template_free(gridpool_direct_template_t *template_data)
{
    if (!template_data) {
        return;
    }
    free(template_data->txid_hashes);
    gridpool_direct_tx_bundle_release(template_data->tx_bundle);
    memset(template_data, 0, sizeof(*template_data));
}

esp_err_t gridpool_direct_parse_payout_outputs_json(const char *json,
                                                    gridpool_direct_payout_output_t *outputs,
                                                    size_t max_outputs,
                                                    size_t *output_count,
                                                    uint64_t *sequence)
{
    if (!json || !outputs || !output_count) {
        return ESP_ERR_INVALID_ARG;
    }

    *output_count = 0;
    if (sequence) {
        *sequence = 0;
        scan_json_u64(json, "sequence", sequence);
    }

    const char *array = find_array_start(json, "coinbaseOutputs");
    if (!array) {
        return ESP_ERR_INVALID_ARG;
    }

    int array_depth = 1;
    int object_depth = 0;
    size_t index = 0;

    for (const char *p = array + 1; *p && array_depth > 0; p++) {
        if (*p == '"') {
            p = skip_string(p) - 1;
            continue;
        }
        if (*p == '[') {
            array_depth++;
        } else if (*p == ']') {
            array_depth--;
        } else if (*p == '{') {
            object_depth++;
            if (array_depth == 1 && object_depth == 1) {
                const char *end = find_matching_object_end(p);
                if (!end) {
                    return ESP_ERR_INVALID_ARG;
                }
                if (index >= max_outputs) {
                    return ESP_ERR_INVALID_SIZE;
                }

                uint64_t value = 0;
                char address[GRIDPOOL_DIRECT_ADDRESS_MAX_LEN];
                if (!scan_json_u64_in_span(p, end, "value", &value) ||
                    !scan_json_string_in_span(p, end, "address", address, sizeof(address)) ||
                    address[0] == '\0') {
                    return ESP_ERR_INVALID_ARG;
                }

                outputs[index].value_sats = value;
                snprintf(outputs[index].address, sizeof(outputs[index].address), "%s", address);
                index++;
                p = end - 1;
                object_depth = 0;
            }
        } else if (*p == '}') {
            object_depth--;
        }
    }

    if (array_depth != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *output_count = index;
    return ESP_OK;
}

static esp_err_t serialize_coinbase_tx(const gridpool_direct_template_t *template_data,
                                       const char *slot0_address,
                                       const gridpool_direct_payout_output_t *gridpool_outputs,
                                       size_t gridpool_output_count,
                                       const uint8_t *extranonce,
                                       size_t extranonce_len,
                                       const char *tag,
                                       bool include_witness,
                                       uint8_t *out,
                                       size_t out_cap,
                                       size_t *out_len,
                                       uint64_t *slot0_value_sats)
{
    if (!template_data || !slot0_address || (!gridpool_outputs && gridpool_output_count > 0) ||
        !out_len || (out == NULL && out_cap != 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t gridpool_total = 0;
    for (size_t i = 0; i < gridpool_output_count; i++) {
        if (UINT64_MAX - gridpool_total < gridpool_outputs[i].value_sats) {
            return ESP_ERR_INVALID_ARG;
        }
        gridpool_total += gridpool_outputs[i].value_sats;
    }
    if (gridpool_total > template_data->coinbase_value) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t scriptsig[GRIDPOOL_DIRECT_MAX_COINBASE_SCRIPTSIG_LEN];
    size_t scriptsig_len = 0;
    ESP_RETURN_ON_ERROR(build_scriptsig(template_data->height, extranonce, extranonce_len, tag, scriptsig, &scriptsig_len),
                        "gridpool_builder", "build scriptsig");

    uint8_t slot0_script[GRIDPOOL_DIRECT_MAX_SCRIPT_PUBKEY_LEN];
    size_t slot0_script_len = 0;
    ESP_RETURN_ON_ERROR(gridpool_direct_address_to_script(slot0_address, slot0_script, sizeof(slot0_script), &slot0_script_len),
                        "gridpool_builder", "slot0 script");

    uint8_t commitment_script[GRIDPOOL_DIRECT_MAX_SCRIPT_PUBKEY_LEN];
    size_t commitment_script_len = 0;
    if (template_data->default_witness_commitment[0]) {
        commitment_script_len = strlen(template_data->default_witness_commitment) / 2;
        ESP_RETURN_ON_ERROR(parse_hex_exact(template_data->default_witness_commitment,
                                            strlen(template_data->default_witness_commitment),
                                            commitment_script,
                                            commitment_script_len),
                            "gridpool_builder", "witness commitment");
    }

    byte_writer_t writer = {
        .data = out,
        .len = 0,
        .cap = out_cap,
        .count_only = out == NULL,
    };

    uint64_t output_count = 1 + gridpool_output_count + (commitment_script_len > 0 ? 1 : 0);
    uint8_t zero_hash[32] = { 0 };
    uint8_t witness_reserved[32] = { 0 };
    uint64_t slot0_value = template_data->coinbase_value - gridpool_total;

    if (!writer_put_u32_le(&writer, GRIDPOOL_DIRECT_COINBASE_VERSION)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (include_witness && (!writer_put_u8(&writer, 0x00) || !writer_put_u8(&writer, 0x01))) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!writer_put_varint(&writer, 1) ||
        !writer_put_bytes(&writer, zero_hash, sizeof(zero_hash)) ||
        !writer_put_u32_le(&writer, GRIDPOOL_DIRECT_SEQUENCE_FINAL) ||
        !writer_put_varint(&writer, scriptsig_len) ||
        !writer_put_bytes(&writer, scriptsig, scriptsig_len) ||
        !writer_put_u32_le(&writer, GRIDPOOL_DIRECT_SEQUENCE_FINAL) ||
        !writer_put_varint(&writer, output_count) ||
        !writer_put_u64_le(&writer, slot0_value) ||
        !writer_put_varint(&writer, slot0_script_len) ||
        !writer_put_bytes(&writer, slot0_script, slot0_script_len)) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < gridpool_output_count; i++) {
        uint8_t payout_script[GRIDPOOL_DIRECT_MAX_SCRIPT_PUBKEY_LEN];
        size_t payout_script_len = 0;
        ESP_RETURN_ON_ERROR(gridpool_direct_address_to_script(gridpool_outputs[i].address,
                                                              payout_script,
                                                              sizeof(payout_script),
                                                              &payout_script_len),
                            "gridpool_builder", "payout script");
        if (!writer_put_u64_le(&writer, gridpool_outputs[i].value_sats) ||
            !writer_put_varint(&writer, payout_script_len) ||
            !writer_put_bytes(&writer, payout_script, payout_script_len)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (commitment_script_len > 0) {
        if (!writer_put_u64_le(&writer, 0) ||
            !writer_put_varint(&writer, commitment_script_len) ||
            !writer_put_bytes(&writer, commitment_script, commitment_script_len)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (include_witness) {
        if (!writer_put_varint(&writer, 1) ||
            !writer_put_varint(&writer, sizeof(witness_reserved)) ||
            !writer_put_bytes(&writer, witness_reserved, sizeof(witness_reserved))) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!writer_put_u32_le(&writer, 0)) {
        return ESP_ERR_INVALID_SIZE;
    }

    *out_len = writer.len;
    if (slot0_value_sats) {
        *slot0_value_sats = slot0_value;
    }
    return ESP_OK;
}

esp_err_t gridpool_direct_build_coinbase(const gridpool_direct_template_t *template_data,
                                         const char *slot0_address,
                                         const gridpool_direct_payout_output_t *gridpool_outputs,
                                         size_t gridpool_output_count,
                                         const uint8_t *extranonce,
                                         size_t extranonce_len,
                                         const char *tag,
                                         uint8_t **coinbase_tx,
                                         size_t *coinbase_tx_len,
                                         uint8_t coinbase_txid[32],
                                         uint64_t *slot0_value_sats)
{
    if (!coinbase_tx || !coinbase_tx_len || !coinbase_txid) {
        return ESP_ERR_INVALID_ARG;
    }

    *coinbase_tx = NULL;
    *coinbase_tx_len = 0;

    size_t full_len = 0;
    esp_err_t err = serialize_coinbase_tx(template_data, slot0_address, gridpool_outputs, gridpool_output_count,
                                          extranonce, extranonce_len, tag, true, NULL, 0, &full_len, slot0_value_sats);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t *full_tx = gridpool_malloc(full_len);
    if (!full_tx) {
        return ESP_ERR_NO_MEM;
    }

    err = serialize_coinbase_tx(template_data, slot0_address, gridpool_outputs, gridpool_output_count,
                                extranonce, extranonce_len, tag, true, full_tx, full_len, coinbase_tx_len, slot0_value_sats);
    if (err != ESP_OK) {
        free(full_tx);
        return err;
    }

    size_t legacy_len = 0;
    err = serialize_coinbase_tx(template_data, slot0_address, gridpool_outputs, gridpool_output_count,
                                extranonce, extranonce_len, tag, false, NULL, 0, &legacy_len, NULL);
    if (err != ESP_OK) {
        free(full_tx);
        return err;
    }

    uint8_t *legacy_tx = gridpool_malloc(legacy_len);
    if (!legacy_tx) {
        free(full_tx);
        return ESP_ERR_NO_MEM;
    }

    err = serialize_coinbase_tx(template_data, slot0_address, gridpool_outputs, gridpool_output_count,
                                extranonce, extranonce_len, tag, false, legacy_tx, legacy_len, &legacy_len, NULL);
    if (err != ESP_OK) {
        free(legacy_tx);
        free(full_tx);
        return err;
    }

    double_sha256_bin(legacy_tx, legacy_len, coinbase_txid);
    free(legacy_tx);

    *coinbase_tx = full_tx;
    return ESP_OK;
}

esp_err_t gridpool_direct_estimate_coinbase_weight(const gridpool_direct_template_t *template_data,
                                                   const char *slot0_address,
                                                   const gridpool_direct_payout_output_t *gridpool_outputs,
                                                   size_t gridpool_output_count,
                                                   size_t extranonce_len,
                                                   const char *tag,
                                                   uint32_t *coinbase_weight,
                                                   size_t *coinbase_full_len)
{
    if (!coinbase_weight) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t zero_extranonce[16] = { 0 };
    if (extranonce_len > sizeof(zero_extranonce)) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t full_len = 0;
    size_t legacy_len = 0;
    esp_err_t err = serialize_coinbase_tx(template_data,
                                          slot0_address,
                                          gridpool_outputs,
                                          gridpool_output_count,
                                          zero_extranonce,
                                          extranonce_len,
                                          tag,
                                          true,
                                          NULL,
                                          0,
                                          &full_len,
                                          NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = serialize_coinbase_tx(template_data,
                                slot0_address,
                                gridpool_outputs,
                                gridpool_output_count,
                                zero_extranonce,
                                extranonce_len,
                                tag,
                                false,
                                NULL,
                                0,
                                &legacy_len,
                                NULL);
    if (err != ESP_OK) {
        return err;
    }
    if (legacy_len > UINT32_MAX / 4 || full_len < legacy_len ||
        (full_len - legacy_len) > UINT32_MAX - (legacy_len * 4)) {
        return ESP_ERR_INVALID_SIZE;
    }

    *coinbase_weight = (uint32_t)(legacy_len * 4 + (full_len - legacy_len));
    if (coinbase_full_len) {
        *coinbase_full_len = full_len;
    }
    return ESP_OK;
}

esp_err_t gridpool_direct_trim_template_to_weight(gridpool_direct_template_t *template_data,
                                                  uint32_t coinbase_weight)
{
    if (!template_data || (template_data->tx_count > 0 && !template_data->tx_bundle)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t weight_limit = template_data->weight_limit > 0 ? template_data->weight_limit : 4000000U;
    uint64_t tx_weight_total = template_data->tx_weight_total;
    size_t tx_count = template_data->tx_count;

    while (tx_count > 0) {
        uint64_t block_weight = 80ULL * 4ULL +
                                (uint64_t)gridpool_direct_varint_len(tx_count + 1) * 4ULL +
                                coinbase_weight +
                                tx_weight_total;
        if (block_weight <= weight_limit) {
            break;
        }
        tx_count--;
        tx_weight_total -= template_data->tx_bundle->tx_weights[tx_count];
    }

    template_data->tx_count = tx_count;
    template_data->tx_weight_total = (uint32_t)tx_weight_total;
    if (template_data->tx_bundle) {
        template_data->tx_bundle->tx_count = tx_count;
    }
    return ESP_OK;
}

static esp_err_t gridpool_direct_build_legacy_coinbase(const gridpool_direct_template_t *template_data,
                                                       const char *slot0_address,
                                                       const gridpool_direct_payout_output_t *gridpool_outputs,
                                                       size_t gridpool_output_count,
                                                       const uint8_t *extranonce,
                                                       size_t extranonce_len,
                                                       const char *tag,
                                                       uint8_t **coinbase_tx,
                                                       size_t *coinbase_tx_len)
{
    if (!coinbase_tx || !coinbase_tx_len) {
        return ESP_ERR_INVALID_ARG;
    }

    *coinbase_tx = NULL;
    *coinbase_tx_len = 0;

    size_t legacy_len = 0;
    esp_err_t err = serialize_coinbase_tx(template_data,
                                          slot0_address,
                                          gridpool_outputs,
                                          gridpool_output_count,
                                          extranonce,
                                          extranonce_len,
                                          tag,
                                          false,
                                          NULL,
                                          0,
                                          &legacy_len,
                                          NULL);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t *legacy_tx = gridpool_malloc(legacy_len);
    if (!legacy_tx) {
        return ESP_ERR_NO_MEM;
    }

    err = serialize_coinbase_tx(template_data,
                                slot0_address,
                                gridpool_outputs,
                                gridpool_output_count,
                                extranonce,
                                extranonce_len,
                                tag,
                                false,
                                legacy_tx,
                                legacy_len,
                                coinbase_tx_len,
                                NULL);
    if (err != ESP_OK) {
        free(legacy_tx);
        return err;
    }

    *coinbase_tx = legacy_tx;
    return ESP_OK;
}

esp_err_t gridpool_direct_compute_merkle_path(const uint8_t coinbase_txid[32],
                                              const uint8_t *txid_hashes,
                                              size_t tx_count,
                                              uint8_t **merkle_path,
                                              size_t *merkle_path_count,
                                              uint8_t merkle_root[32])
{
    if (!coinbase_txid || (!txid_hashes && tx_count > 0) || !merkle_path || !merkle_path_count || !merkle_root) {
        return ESP_ERR_INVALID_ARG;
    }

    *merkle_path = NULL;
    *merkle_path_count = 0;

    if (tx_count == 0) {
        memcpy(merkle_root, coinbase_txid, 32);
        return ESP_OK;
    }

    if (tx_count + 1 > SIZE_MAX / 32) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t leaf_count = tx_count + 1;
    uint8_t *level = gridpool_malloc(leaf_count * 32);
    if (!level) {
        return ESP_ERR_NO_MEM;
    }

    uint8_t *path = gridpool_malloc(GRIDPOOL_DIRECT_MAX_MERKLE_BRANCHES * 32);
    if (!path) {
        free(level);
        return ESP_ERR_NO_MEM;
    }

    memcpy(level, coinbase_txid, 32);
    memcpy(level + 32, txid_hashes, tx_count * 32);

    size_t count = leaf_count;
    size_t index = 0;
    size_t path_count = 0;
    while (count > 1) {
        if (path_count >= GRIDPOOL_DIRECT_MAX_MERKLE_BRANCHES) {
            free(path);
            free(level);
            return ESP_ERR_INVALID_SIZE;
        }

        size_t sibling = (index ^ 1);
        if (sibling >= count) {
            sibling = index;
        }
        memcpy(path + path_count * 32, level + sibling * 32, 32);
        path_count++;

        size_t next_count = 0;
        for (size_t i = 0; i < count; i += 2) {
            uint8_t pair[64];
            size_t right = i + 1 < count ? i + 1 : i;
            memcpy(pair, level + i * 32, 32);
            memcpy(pair + 32, level + right * 32, 32);
            double_sha256_bin(pair, sizeof(pair), level + next_count * 32);
            next_count++;
        }

        index /= 2;
        count = next_count;
    }

    memcpy(merkle_root, level, 32);
    free(level);

    if (path_count == 0) {
        free(path);
        path = NULL;
    }

    *merkle_path = path;
    *merkle_path_count = path_count;
    return ESP_OK;
}

esp_err_t gridpool_direct_compute_root_from_path(const uint8_t coinbase_txid[32],
                                                 const uint8_t *merkle_path,
                                                 size_t merkle_path_count,
                                                 uint8_t merkle_root[32])
{
    if (!coinbase_txid || (!merkle_path && merkle_path_count > 0) || !merkle_root ||
        merkle_path_count > GRIDPOOL_DIRECT_MAX_MERKLE_BRANCHES) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(merkle_root, coinbase_txid, 32);
    for (size_t i = 0; i < merkle_path_count; i++) {
        uint8_t pair[64];
        memcpy(pair, merkle_root, 32);
        memcpy(pair + 32, merkle_path + i * 32, 32);
        double_sha256_bin(pair, sizeof(pair), merkle_root);
    }

    return ESP_OK;
}

esp_err_t gridpool_direct_build_job(const gridpool_direct_template_t *template_data,
                                    const char *slot0_address,
                                    const gridpool_direct_payout_output_t *gridpool_outputs,
                                    size_t gridpool_output_count,
                                    const uint8_t *extranonce,
                                    size_t extranonce_len,
                                    const char *tag,
                                    double submit_difficulty,
                                    gridpool_direct_job_t *job_out)
{
    if (!template_data || !job_out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(job_out, 0, sizeof(*job_out));

    esp_err_t err = gridpool_direct_build_coinbase(template_data,
                                                   slot0_address,
                                                   gridpool_outputs,
                                                   gridpool_output_count,
                                                   extranonce,
                                                   extranonce_len,
                                                   tag,
                                                   &job_out->coinbase_tx,
                                                   &job_out->coinbase_tx_len,
                                                   job_out->coinbase_txid,
                                                   &job_out->slot0_value_sats);
    if (err != ESP_OK) {
        gridpool_direct_job_free(job_out);
        return err;
    }

    err = gridpool_direct_build_legacy_coinbase(template_data,
                                                slot0_address,
                                                gridpool_outputs,
                                                gridpool_output_count,
                                                extranonce,
                                                extranonce_len,
                                                tag,
                                                &job_out->coinbase_legacy_tx,
                                                &job_out->coinbase_legacy_tx_len);
    if (err != ESP_OK) {
        gridpool_direct_job_free(job_out);
        return err;
    }

    err = gridpool_direct_compute_merkle_path(job_out->coinbase_txid,
                                              template_data->txid_hashes,
                                              template_data->tx_count,
                                              &job_out->merkle_path,
                                              &job_out->merkle_path_count,
                                              job_out->merkle_root);
    if (err != ESP_OK) {
        gridpool_direct_job_free(job_out);
        return err;
    }

    char prevhash_for_construct[65];
    err = display_hash_to_stratum_prevhash(template_data->previous_block_hash, prevhash_for_construct);
    if (err != ESP_OK) {
        gridpool_direct_job_free(job_out);
        return err;
    }

    mining_notify notify = {
        .prev_block_hash = prevhash_for_construct,
        .version = template_data->version,
        .target = template_data->nbits,
        .ntime = template_data->curtime,
    };

    construct_bm_job(&notify, job_out->merkle_root, 0, submit_difficulty, &job_out->bm_job);

    job_out->version = template_data->version;
    job_out->nbits = template_data->nbits;
    job_out->ntime = template_data->curtime;
    job_out->height = template_data->height;
    snprintf(job_out->previous_block_hash, sizeof(job_out->previous_block_hash), "%s", template_data->previous_block_hash);

    return ESP_OK;
}

esp_err_t gridpool_direct_build_job_from_path(const gridpool_direct_template_t *template_data,
                                              const char *slot0_address,
                                              const gridpool_direct_payout_output_t *gridpool_outputs,
                                              size_t gridpool_output_count,
                                              const uint8_t *merkle_path,
                                              size_t merkle_path_count,
                                              const uint8_t *extranonce,
                                              size_t extranonce_len,
                                              const char *tag,
                                              double submit_difficulty,
                                              gridpool_direct_job_t *job_out)
{
    if (!template_data || !job_out || (!merkle_path && merkle_path_count > 0) ||
        merkle_path_count > GRIDPOOL_DIRECT_MAX_MERKLE_BRANCHES) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(job_out, 0, sizeof(*job_out));

    esp_err_t err = gridpool_direct_build_coinbase(template_data,
                                                   slot0_address,
                                                   gridpool_outputs,
                                                   gridpool_output_count,
                                                   extranonce,
                                                   extranonce_len,
                                                   tag,
                                                   &job_out->coinbase_tx,
                                                   &job_out->coinbase_tx_len,
                                                   job_out->coinbase_txid,
                                                   &job_out->slot0_value_sats);
    if (err != ESP_OK) {
        gridpool_direct_job_free(job_out);
        return err;
    }

    err = gridpool_direct_build_legacy_coinbase(template_data,
                                                slot0_address,
                                                gridpool_outputs,
                                                gridpool_output_count,
                                                extranonce,
                                                extranonce_len,
                                                tag,
                                                &job_out->coinbase_legacy_tx,
                                                &job_out->coinbase_legacy_tx_len);
    if (err != ESP_OK) {
        gridpool_direct_job_free(job_out);
        return err;
    }

    if (merkle_path_count > 0) {
        if (merkle_path_count > SIZE_MAX / 32) {
            gridpool_direct_job_free(job_out);
            return ESP_ERR_INVALID_SIZE;
        }
        job_out->merkle_path = gridpool_malloc(merkle_path_count * 32);
        if (!job_out->merkle_path) {
            gridpool_direct_job_free(job_out);
            return ESP_ERR_NO_MEM;
        }
        memcpy(job_out->merkle_path, merkle_path, merkle_path_count * 32);
        job_out->merkle_path_count = merkle_path_count;
    }

    err = gridpool_direct_compute_root_from_path(job_out->coinbase_txid,
                                                 merkle_path,
                                                 merkle_path_count,
                                                 job_out->merkle_root);
    if (err != ESP_OK) {
        gridpool_direct_job_free(job_out);
        return err;
    }

    char prevhash_for_construct[65];
    err = display_hash_to_stratum_prevhash(template_data->previous_block_hash, prevhash_for_construct);
    if (err != ESP_OK) {
        gridpool_direct_job_free(job_out);
        return err;
    }

    mining_notify notify = {
        .prev_block_hash = prevhash_for_construct,
        .version = template_data->version,
        .target = template_data->nbits,
        .ntime = template_data->curtime,
    };

    construct_bm_job(&notify, job_out->merkle_root, 0, submit_difficulty, &job_out->bm_job);

    job_out->version = template_data->version;
    job_out->nbits = template_data->nbits;
    job_out->ntime = template_data->curtime;
    job_out->height = template_data->height;
    snprintf(job_out->previous_block_hash, sizeof(job_out->previous_block_hash), "%s", template_data->previous_block_hash);

    return ESP_OK;
}

void gridpool_direct_job_free(gridpool_direct_job_t *job)
{
    if (!job) {
        return;
    }
    free(job->coinbase_tx);
    free(job->coinbase_legacy_tx);
    free(job->merkle_path);
    free(job->bm_job.jobid);
    free(job->bm_job.extranonce2);
    memset(job, 0, sizeof(*job));
}

esp_err_t gridpool_direct_serialize_header(const gridpool_direct_job_t *job,
                                           uint32_t nonce,
                                           uint32_t rolled_version,
                                           uint8_t header[80])
{
    if (!job || !header || !is_hex_string_len(job->previous_block_hash, 64)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t display_prev_hash[32];
    uint8_t internal_prev_hash[32];
    ESP_RETURN_ON_ERROR(parse_hex_exact(job->previous_block_hash, 64, display_prev_hash, sizeof(display_prev_hash)),
                        "gridpool_builder", "prev hash");
    reverse_bytes_32(display_prev_hash, internal_prev_hash);

    byte_writer_t writer = {
        .data = header,
        .len = 0,
        .cap = 80,
        .count_only = false,
    };

    if (!writer_put_u32_le(&writer, rolled_version) ||
        !writer_put_bytes(&writer, internal_prev_hash, sizeof(internal_prev_hash)) ||
        !writer_put_bytes(&writer, job->merkle_root, 32) ||
        !writer_put_u32_le(&writer, job->ntime) ||
        !writer_put_u32_le(&writer, job->nbits) ||
        !writer_put_u32_le(&writer, nonce)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return writer.len == 80 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t gridpool_direct_hash_to_display_hex(const uint8_t hash[32],
                                              char *hex,
                                              size_t hex_len)
{
    if (!hash || !hex || hex_len < 65) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reversed[32];
    reverse_bytes_32(hash, reversed);
    return bin2hex(reversed, sizeof(reversed), hex, hex_len) == 64 ? ESP_OK : ESP_FAIL;
}
