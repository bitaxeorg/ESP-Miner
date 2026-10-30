/* Native experiment runner. No sockets, device access, or firmware side effects. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "real_roots.h"
static _Thread_local size_t live,peak,calls,fail_at,limit;
typedef union { size_t n;max_align_t align; } header;
static void *alloc(size_t n,size_t s) {
 calls++;if(calls==fail_at || (s && n>(SIZE_MAX-sizeof(header))/s))return NULL;
 size_t bytes=n*s;
 if(limit && (bytes>limit || live>limit-bytes))return NULL;
 header *h=calloc(1,sizeof(header)+bytes);if(!h)return NULL;
 h->n=bytes;live+=bytes;if(live>peak)peak=live;return h+1;
}
static void dealloc(void *p) {if(p){header *h=(header*)p-1;live-=h->n;free(h);}}
static _Thread_local int lookup_error;
static _Thread_local size_t lookup_count,lookup_retained;
static int observed_lookup(void *ctx,const mbedtls_x509_crt *child,mbedtls_x509_crt **out) {
 size_t before=live;int r=real_roots_lookup(ctx,child,out);lookup_count++;
 if(r)lookup_error=r;
 if(!r && live-before>lookup_retained)lookup_retained=live-before;
 return r;
}
static int stale_verify_hook(void *ctx,mbedtls_x509_crt *crt,int depth,uint32_t *flags) {
 (void)ctx;(void)crt;(void)depth;(void)flags;return MBEDTLS_ERR_X509_FATAL_ERROR;
}

typedef struct {const char *chain,*host;size_t fail,cap;int expected,loops;int failed;} job;
static int once(job *j,int verbose) {
 live=peak=calls=lookup_count=lookup_retained=0;lookup_error=0;fail_at=j->fail;limit=j->cap;
 mbedtls_x509_crt peer;mbedtls_x509_crt_init(&peer);
 mbedtls_ssl_config conf;mbedtls_ssl_config_init(&conf);
 mbedtls_ssl_context ssl;mbedtls_ssl_init(&ssl);
 uint32_t flags=0;
 int ret=mbedtls_ssl_config_defaults(&conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
 const char *stage="defaults";
 if(!ret){stage="attach";mbedtls_ssl_conf_verify(&conf,stale_verify_hook,NULL);ret=real_roots_attach(&conf);}
 if(!ret && (conf.MBEDTLS_PRIVATE(f_ca_cb)!=real_roots_lookup || conf.MBEDTLS_PRIVATE(f_vrfy)!=NULL))ret=MBEDTLS_ERR_X509_BAD_INPUT_DATA;
 if(!ret){stage="setup";mbedtls_ssl_conf_ca_cb(&conf,observed_lookup,NULL);ret=mbedtls_ssl_setup(&ssl,&conf);}
 if(!ret){stage="hostname";ret=mbedtls_ssl_set_hostname(&ssl,j->host);}
 if(!ret){stage="peer";ret=mbedtls_x509_crt_parse_file(&peer,j->chain);}
 if(!ret){stage="verify";ret=mbedtls_x509_crt_verify_with_ca_cb(&peer,conf.MBEDTLS_PRIVATE(f_ca_cb),conf.MBEDTLS_PRIVATE(p_ca_cb),conf.MBEDTLS_PRIVATE(cert_profile),j->host,&flags,conf.MBEDTLS_PRIVATE(f_vrfy),conf.MBEDTLS_PRIVATE(p_vrfy));}
 mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&conf);mbedtls_x509_crt_free(&peer);
 if(verbose)printf("stage=%s ret=%d flags=0x%x calls=%zu peak=%zu residual=%zu lookup_error=%d lookup_count=%zu lookup_retained=%zu\n",stage,ret,flags,calls,peak,live,lookup_error,lookup_count,lookup_retained);
 return live || (!ret && (lookup_error || flags))?70:ret?2:0;
}
static void *worker(void *arg){job *j=arg;for(int i=0;i<j->loops;i++){int r=once(j,0);if(r!=j->expected){j->failed=1;break;}}return NULL;}
int main(int argc,char **argv) {
 if(argc!=6 && argc!=7)return 64;
 /* PSA process-global RNG is initialized before per-verification accounting.
  * No process-global allocator changes occur while workers run. */
 if(psa_crypto_init()!=PSA_SUCCESS)return 72;
 unsigned char warm[32]; if(psa_generate_random(warm,sizeof(warm))!=PSA_SUCCESS)return 73;
 job warm_job={.chain=argv[1],.host=argv[2]}; once(&warm_job,0);
 /* PSA lazily creates persistent key-slot storage on first import. Exclude
  * process-global storage from per-verification ownership, not actual keys. */
 mbedtls_platform_set_calloc_free(alloc,dealloc);
 job j={.chain=argv[1],.host=argv[2],.fail=strtoull(argv[3],0,10),.cap=strtoull(argv[4],0,10),.expected=atoi(argv[5]),.loops=50};
 if(argc==7){pthread_t t[8];job jobs[8];for(int i=0;i<8;i++){jobs[i]=j;if(pthread_create(t+i,NULL,worker,jobs+i))return 71;}int bad=0;for(int i=0;i<8;i++){pthread_join(t[i],NULL);bad|=jobs[i].failed;}printf("threads=8 loops=50 failed=%d\n",bad);return bad?2:0;}
 return once(&j,1);
}
