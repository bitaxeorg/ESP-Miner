/* Cold-start and repeated in-process ownership regression. No sockets.
 * Count BOTH SDK libc and mbedTLS allocator domains; the old SDK allocates in
 * libc and hands ownership to mbedTLS. Keeping domains visible catches that
 * mismatch even when both happen to use the same host heap. Never reset counts
 * or swap allocators to hide PSA lazy storage; free PSA before final assertions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
static struct { void *ptr; size_t bytes; int domain; } owned[32768];
static size_t count, crossed;
static void *tracked_alloc(size_t n, size_t size, int domain)
{
    if (size && n > SIZE_MAX / size) return NULL;
    if (count == sizeof(owned)/sizeof(owned[0])) exit(80);
    void *p = calloc(n, size);
    if (p) { owned[count].ptr=p; owned[count].bytes=n*size; owned[count++].domain=domain; }
    return p;
}
static void tracked_free(void *p, int domain)
{
    if (!p) return;
    for (size_t i=0; i<count; i++) {
        if (owned[i].ptr != p) continue;
        if (owned[i].domain != domain) crossed++;
        owned[i]=owned[--count];
        free(p);
        return;
    }
    /* An unaccounted free means the regression's allocator coverage is broken. */
    exit(81);
}
static void *crypto_alloc(size_t n,size_t s) { return tracked_alloc(n,s,1); }
static void crypto_free(void *p) { tracked_free(p,1); }
#ifndef CUSTOM_ROOTS
static void *sdk_alloc(size_t n,size_t s) { return tracked_alloc(n,s,0); }
static void sdk_free(void *p) { tracked_free(p,0); }
#define calloc sdk_alloc
#define free sdk_free
#include "sdk_bundle.c"
#undef calloc
#undef free
#else
#include "real_roots.h"
#endif
static size_t bytes(int domain)
{
    size_t total=0;
    for (size_t i=0; i<count; i++) if (owned[i].domain==domain) total+=owned[i].bytes;
    return total;
}
int main(int argc,char **argv)
{
    if (argc != 6) return 64;
    int loops=atoi(argv[4]), expected=atoi(argv[5]);
    if (loops<1 || loops>1000) return 64;
    mbedtls_platform_set_calloc_free(crypto_alloc,crypto_free);
    if (psa_crypto_init()!=PSA_SUCCESS) return 65;
    FILE *f=fopen(argv[1],"rb");
    if (!f) return 66;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f);
    unsigned char *bundle=len>0?malloc((size_t)len):NULL;
    if (!bundle || fread(bundle,1,(size_t)len,f)!=(size_t)len) return 66;
    fclose(f);
    mbedtls_ssl_config conf; mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt peer; mbedtls_x509_crt_init(&peer);
    int r=mbedtls_ssl_config_defaults(&conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
#ifndef CUSTOM_ROOTS
    if (!r) r=esp_crt_bundle_set(bundle,(size_t)len);
    if (!r) r=esp_crt_bundle_attach(&conf);
#else
    if (!r) r=real_roots_attach(&conf);
#endif
    if (!r) r=mbedtls_x509_crt_parse_file(&peer,argv[2]);
    if (r) return 67;
    size_t first=0,last=0;
    int bad=0;
    for (int i=0;i<loops;i++) {
        uint32_t flags=0;
        if (conf.MBEDTLS_PRIVATE(f_ca_cb))
            r=mbedtls_x509_crt_verify_with_ca_cb(&peer,conf.MBEDTLS_PRIVATE(f_ca_cb),conf.MBEDTLS_PRIVATE(p_ca_cb),conf.MBEDTLS_PRIVATE(cert_profile),argv[3],&flags,conf.MBEDTLS_PRIVATE(f_vrfy),conf.MBEDTLS_PRIVATE(p_vrfy));
        else
            r=mbedtls_x509_crt_verify_with_profile(&peer,conf.MBEDTLS_PRIVATE(ca_chain),NULL,conf.MBEDTLS_PRIVATE(cert_profile),argv[3],&flags,conf.MBEDTLS_PRIVATE(f_vrfy),conf.MBEDTLS_PRIVATE(p_vrfy));
        if ((r?2:0)!=expected || (!r && flags) || (r && !flags)) bad=1;
        last=bytes(0)+bytes(1);
        if (!i) first=last;
    }
    mbedtls_x509_crt_free(&peer);
#ifndef CUSTOM_ROOTS
    esp_crt_bundle_detach(&conf);
#endif
    mbedtls_ssl_config_free(&conf);
    free(bundle);
    mbedtls_psa_crypto_free();
    printf("loops=%d first_live=%zu last_live=%zu remaining_allocations=%zu sdk_bytes=%zu crypto_bytes=%zu cross_domain_frees=%zu bad_results=%d\n",loops,first,last,count,bytes(0),bytes(1),crossed,bad);
    return bad?68:count?70:crossed?71:0;
}
