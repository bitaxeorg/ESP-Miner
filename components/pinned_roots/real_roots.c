/* Bounded lookup for the pinned roots. Only immutable approved real DER
 * is returned. mbedTLS owns every returned object and makes ALL trust decisions.
 * CN matching is only an overinclusive allocation prefilter: the native verifier
 * still compares the complete DN, CA usage, constraints, key, signature and time.
 * ASCII folding (all tags) deliberately includes a superset of SDK name matches.
 */
#include <stddef.h>
#include <string.h>
#include "mbedtls/platform.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ssl.h"
#include "real_roots.h"
typedef struct { const unsigned char *der; size_t len; const unsigned char *cn; size_t cn_len; } root_entry;
#include "roots.inc"
static unsigned char fold(unsigned char c) { return c>='A' && c<='Z' ? c+32 : c; }
static int prefilter(const mbedtls_x509_crt *child,const root_entry *r) {
    if (!r->cn_len) return 1; /* intentionally overinclusive */
    const mbedtls_x509_name *n=&child->issuer;
    for (;n;n=n->next) {
        if(n->oid.len==3 && !memcmp(n->oid.p,"\x55\x04\x03",3)) {
            if(n->val.len!=r->cn_len) return 0;
            for(size_t i=0;i<r->cn_len;i++) if(fold(n->val.p[i])!=fold(r->cn[i])) return 0;
            return 1;
        }
    }
    return 0;
}
int real_roots_lookup(void *ctx,const mbedtls_x509_crt *child,mbedtls_x509_crt **out) {
    (void)ctx;
    *out=NULL;
    if (!child || sizeof(roots)/sizeof(roots[0])!=ROOT_COUNT) return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
    size_t count=0,bytes=0;
    mbedtls_x509_crt *list=NULL;
    int ret=0;
    for(size_t i=0;i<ROOT_COUNT;i++) {
        if(!prefilter(child,&roots[i]))continue;
        if(++count>ROOT_CANDIDATE_CAP || roots[i].len>ROOT_DER_CAP-bytes) {
            ret=MBEDTLS_ERR_X509_BAD_INPUT_DATA;goto fail;
        }
        bytes+=roots[i].len;
        if(!list) {
            list=mbedtls_calloc(1,sizeof(*list));
            if(!list) { ret=MBEDTLS_ERR_X509_ALLOC_FAILED;goto fail; }
            mbedtls_x509_crt_init(list);
        }
        /* Copying parse, not nocopy: no flash lifetime or mutability assumptions. */
        ret=mbedtls_x509_crt_parse_der(list,roots[i].der,roots[i].len);
        if(ret!=0)goto fail;
    }
    *out=list;return 0;
fail:
    if(list) { mbedtls_x509_crt_free(list);mbedtls_free(list); }
    return ret;
}
int real_roots_attach(void *p) {
    if(!p)return -1;
    mbedtls_ssl_config *conf=(mbedtls_ssl_config*)p;
    /* Replace the old bundle post-verification hook, not its flags. A reused
     * config must not retain a legacy hook that can override native failure. */
    mbedtls_ssl_conf_verify(conf,NULL,NULL);
    mbedtls_ssl_conf_ca_cb(conf,real_roots_lookup,NULL);
    return 0;
}
