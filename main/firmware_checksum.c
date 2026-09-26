#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "psa/crypto.h"
#include "firmware_checksum.h"

#define HASH_CHUNK_SIZE 4096
#define SHA256_LEN 32

static const char *TAG = "firmware_checksum";

static char cached_sha256_hex[SHA256_LEN * 2 + 1] = {0};
static uint32_t cached_image_len = 0;

static esp_err_t compute_app_sha256(const esp_partition_t *partition, uint8_t digest[SHA256_LEN], uint32_t *image_len)
{
    esp_partition_pos_t part_pos = {
        .offset = partition->address,
        .size = partition->size,
    };
    esp_image_metadata_t metadata;
    esp_err_t err = esp_image_get_metadata(&part_pos, &metadata);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t *buf = malloc(HASH_CHUNK_SIZE);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    psa_status_t status = psa_hash_setup(&op, PSA_ALG_SHA_256);

    for (uint32_t offset = 0; status == PSA_SUCCESS && offset < metadata.image_len; offset += HASH_CHUNK_SIZE) {
        size_t len = MIN(HASH_CHUNK_SIZE, metadata.image_len - offset);
        err = esp_partition_read(partition, offset, buf, len);
        if (err != ESP_OK) {
            break;
        }
        status = psa_hash_update(&op, buf, len);
    }
    free(buf);

    if (err != ESP_OK || status != PSA_SUCCESS) {
        psa_hash_abort(&op);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    size_t digest_len = 0;
    status = psa_hash_finish(&op, digest, SHA256_LEN, &digest_len);
    if (status != PSA_SUCCESS || digest_len != SHA256_LEN) {
        return ESP_FAIL;
    }

    *image_len = metadata.image_len;
    return ESP_OK;
}

esp_err_t firmware_checksum_get_running(const char **sha256_hex, uint32_t *image_len)
{
    if (sha256_hex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cached_sha256_hex[0] == '\0') {
        const esp_partition_t *running = esp_ota_get_running_partition();
        if (running == NULL) {
            return ESP_ERR_NOT_FOUND;
        }

        uint8_t digest[SHA256_LEN];
        esp_err_t err = compute_app_sha256(running, digest, &cached_image_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to hash running firmware: %s", esp_err_to_name(err));
            return err;
        }
        for (int i = 0; i < SHA256_LEN; i++) {
            snprintf(&cached_sha256_hex[i * 2], 3, "%02x", digest[i]);
        }
    }

    *sha256_hex = cached_sha256_hex;
    if (image_len != NULL) {
        *image_len = cached_image_len;
    }
    return ESP_OK;
}
