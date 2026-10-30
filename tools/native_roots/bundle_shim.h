#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "mbedtls/ssl.h"
#include "mbedtls/private/ecdsa.h"
typedef int esp_err_t;
#define MBEDTLS_ECDSA_MAX_LEN PSA_SIGNATURE_MAX_SIZE
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS 200
#define unlikely(x) (x)
#define likely(x) (x)
#define ESP_LOGE(...) do{}while(0)
#define ESP_LOGI(...) do{}while(0)
#define ESP_LOGD(...) do{}while(0)
const uint8_t x509_crt_imported_bundle_bin_start[1] asm("_binary_x509_crt_bundle_start")={0};
const uint8_t x509_crt_imported_bundle_bin_end[1] asm("_binary_x509_crt_bundle_end")={0};
