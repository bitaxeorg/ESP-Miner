/* Parsed-field test seams only, not re-signed certificates or TLS handshakes. */
#define main baseline_main
#include "probe.c"
#undef main
static int timed_lookup(void *ctx,const mbedtls_x509_crt *child,mbedtls_x509_crt **out){
 int mode=*(int*)ctx;int ret=real_roots_lookup(NULL,child,out);
 if(!ret)for(mbedtls_x509_crt *r=*out;r;r=r->next){if(mode==3)r->valid_to.year=2000;if(mode==4)r->valid_from.year=2100;}
 return ret;
}
int main(int argc,char **argv){
 if(argc!=3)return 64;if(psa_crypto_init()!=PSA_SUCCESS)return 72;
 for(int mode=0;mode<5;mode++){
  live=peak=calls=fail_at=limit=0;
  mbedtls_x509_crt peer;mbedtls_x509_crt_init(&peer);
  mbedtls_ssl_config conf;mbedtls_ssl_config_init(&conf);
  if(mbedtls_x509_crt_parse_file(&peer,argv[1]))return 1;
  if(mode==1)peer.valid_to.year=2000;if(mode==2)peer.valid_from.year=2100;
  real_roots_attach(&conf);mbedtls_ssl_conf_ca_cb(&conf,timed_lookup,&mode);
  uint32_t flags=0;
  int ret=mbedtls_x509_crt_verify_with_ca_cb(&peer,conf.MBEDTLS_PRIVATE(f_ca_cb),conf.MBEDTLS_PRIVATE(p_ca_cb),&mbedtls_x509_crt_profile_default,argv[2],&flags,conf.MBEDTLS_PRIVATE(f_vrfy),conf.MBEDTLS_PRIVATE(p_vrfy));
  mbedtls_x509_crt_free(&peer);mbedtls_ssl_config_free(&conf);
  uint32_t expected=mode==0?0:(mode==1||mode==3)?MBEDTLS_X509_BADCERT_EXPIRED:MBEDTLS_X509_BADCERT_FUTURE;
  printf("time-seam=%d ret=%d flags=0x%x residual=%zu\n",mode,ret,flags,live);
  if(live || (mode==0?ret!=0:ret==0) || flags!=expected)return 2;
 }
 return 0;
}
