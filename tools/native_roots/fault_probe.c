/* SDK 6.0.2 complete-path allocation sweep, PSA initialized once. */
#define main baseline_main
#include "probe.c"
#undef main
int main(int argc,char **argv){
 if(argc!=4)return 64;
 if(psa_crypto_init()!=PSA_SUCCESS)return 72;
 unsigned char random[32]; if(psa_generate_random(random,sizeof(random))!=PSA_SUCCESS)return 73;
 job j={.chain=argv[1],.host=argv[2],.expected=atoi(argv[3])};
 once(&j,0); /* SDK PSA process-global lazy allocation, outside per-client budget. */
 mbedtls_platform_set_calloc_free(alloc,dealloc);
 int baseline=once(&j,0); if(baseline!=j.expected)return 1;
 size_t n=calls,accepted=0,rejected=0,lookupfail=0;
 for(size_t i=1;i<=n;i++){
  j.fail=i;int r=once(&j,0);
  if(r==70 || (j.expected==2 && r!=2) || (r!=0&&r!=2)) {printf("fault=%zu ret=%d residual=%zu lookup_error=%d\n",i,r,live,lookup_error);return 2;}
  if(lookup_error)lookupfail++;
  if(r)rejected++;else accepted++;
 }
 printf("baseline-allocation-indices=%zu accepted=%zu rejected=%zu lookup-errors=%zu residual=0\n",n,accepted,rejected,lookupfail);return 0;
}
