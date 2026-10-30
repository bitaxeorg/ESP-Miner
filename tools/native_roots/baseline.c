#ifdef CUSTOM_ROOTS
#include <stdio.h>
#include <stdlib.h>
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "real_roots.h"
#else
#include "sdk_bundle.c"
#endif

/* Dispatch through the configuration installed by esp_crt_bundle_attach().
 * ca_chain alone bypasses the SDK's cross-sign trusted-certificate callback. */
int main(int argc, char **argv)
{
    if (argc != 4 || psa_crypto_init() != PSA_SUCCESS) return 4;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 4;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    unsigned char *bundle = length > 0 ? malloc(length) : NULL;
    if (!bundle || fread(bundle, 1, length, file) != (size_t)length) {
        fclose(file);
        free(bundle);
        return 4;
    }
    fclose(file);
    mbedtls_x509_crt peer;
    mbedtls_x509_crt_init(&peer);
    mbedtls_ssl_config conf;
    mbedtls_ssl_config_init(&conf);
    int ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
#ifdef CUSTOM_ROOTS
    if (!ret) ret = real_roots_attach(&conf);
#else
    if (!ret) ret = esp_crt_bundle_set(bundle, length);
    if (!ret) ret = esp_crt_bundle_attach(&conf);
#endif
    if (!ret) ret = mbedtls_x509_crt_parse_file(&peer, argv[2]);
    uint32_t flags = 0;
    if (!ret) {
        if (conf.MBEDTLS_PRIVATE(f_ca_cb)) {
            ret = mbedtls_x509_crt_verify_with_ca_cb(
                &peer, conf.MBEDTLS_PRIVATE(f_ca_cb), conf.MBEDTLS_PRIVATE(p_ca_cb),
                conf.MBEDTLS_PRIVATE(cert_profile), argv[3], &flags,
                conf.MBEDTLS_PRIVATE(f_vrfy), conf.MBEDTLS_PRIVATE(p_vrfy));
        } else {
            ret = mbedtls_x509_crt_verify_with_profile(
                &peer, conf.MBEDTLS_PRIVATE(ca_chain), NULL,
                conf.MBEDTLS_PRIVATE(cert_profile), argv[3], &flags,
                conf.MBEDTLS_PRIVATE(f_vrfy), conf.MBEDTLS_PRIVATE(p_vrfy));
        }
    }
    printf("verify_return=%d flags=0x%x\n", ret, flags);
    mbedtls_x509_crt_free(&peer);
    mbedtls_ssl_config_free(&conf);
    free(bundle);
    mbedtls_psa_crypto_free();
    return ret ? 2 : 0;
}
