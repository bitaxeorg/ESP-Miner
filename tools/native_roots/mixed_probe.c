/* Mixed concurrent identities and failure/retry, using the exact base harness. */
#define main baseline_main
#include "probe.c"
#undef main
int main(int argc,char **argv){
 if(argc!=4)return 64;
 if(psa_crypto_init()!=PSA_SUCCESS)return 72;
 unsigned char random[32];if(psa_generate_random(random,sizeof(random))!=PSA_SUCCESS)return 73;
 job warm_job={.chain=argv[1],.host=argv[2]};once(&warm_job,0);
 mbedtls_platform_set_calloc_free(alloc,dealloc);
 size_t callback_fault=strtoull(argv[3],NULL,10);
 for(int mode=0;mode<3;mode++){
  pthread_t threads[8];job jobs[8];
  for(int i=0;i<8;i++){
   jobs[i]=(job){.chain=argv[1],.host=argv[2],.loops=50,.expected=0};
   if(i&1){jobs[i].expected=2;
    if(mode==0)jobs[i].host="wrong.example";
    if(mode==1)jobs[i].fail=callback_fault;
    if(mode==2)jobs[i].cap=54000;
   }
   if(pthread_create(threads+i,NULL,worker,jobs+i))return 71;
  }
  for(int i=0;i<8;i++){pthread_join(threads[i],NULL);if(jobs[i].failed){printf("mode=%d thread=%d failed\n",mode,i);return 2;}}
 }
 puts("mixed-hostname/mixed-callback-fault/mixed-heap-cap threads=8 loops=50 modes=3 failed=0");return 0;
}
