/* Exhaustive immutable-root selection and allocation cleanup tests. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "mbedtls/platform.h"
#include "mbedtls/x509_crt.h"
#include "real_roots.h"
typedef struct {const unsigned char *der;size_t len;const unsigned char *cn;size_t cn_len;} root_entry;
#include "roots.inc"
static size_t live,calls,fail_at,max_returned;
typedef union {size_t n;max_align_t align;} header;
static void *alloc(size_t n,size_t s){calls++;if(calls==fail_at)return NULL;header *h=calloc(1,sizeof(header)+n*s);if(!h)return NULL;h->n=n*s;live+=h->n;return h+1;}
static void dealloc(void *p){if(p){header *h=(header*)p-1;live-=h->n;free(h);}}
static int trial(size_t i,size_t failure,int normalise){
 calls=live=0;fail_at=failure;
 mbedtls_x509_crt root,*out=NULL;mbedtls_x509_crt_init(&root);
 int ret=mbedtls_x509_crt_parse_der(&root,roots[i].der,roots[i].len);
 if(!ret){
  /* Test lookup name equivalence, not a forged signed-certificate fixture. */
  mbedtls_x509_crt child=root;child.issuer=root.subject;
  mbedtls_x509_name names[32];unsigned char values[32][1024];
  if(normalise){const mbedtls_x509_name *n=&root.subject;size_t k=0;
   for(;n;n=n->next,k++){
    if(k>=32||n->val.len>1024){ret=-1;break;}
    names[k]=*n;memcpy(values[k],n->val.p,n->val.len);names[k].val.p=values[k];
    if(n->val.tag==12||n->val.tag==19){names[k].val.tag=n->val.tag==12?19:12;for(size_t j=0;j<n->val.len;j++)if(values[k][j]>='A'&&values[k][j]<='Z')values[k][j]+=32;}
    names[k].next=n->next?&names[k+1]:NULL;
   }
   child.issuer=names[0];
  }
  size_t before=live;
  if(!ret)ret=real_roots_lookup(NULL,&child,&out);
  if(!ret && live-before>max_returned)max_returned=live-before;
  if(!ret){
   int found=0;size_t count=0;
   for(mbedtls_x509_crt *p=out;p;p=p->next){
    int approved=0;count++;
    for(size_t j=0;j<ROOT_COUNT;j++)if(p->raw.len==roots[j].len&&!memcmp(p->raw.p,roots[j].der,p->raw.len)){approved=1;if(j==i)found=1;break;}
    if(!approved||count>ROOT_CANDIDATE_CAP)ret=-1;
   }
   if(!found)ret=-1;
  }
 }
 if(out){mbedtls_x509_crt_free(out);mbedtls_free(out);}mbedtls_x509_crt_free(&root);
 return live?70:ret?2:0;
}
int main(void){
 if(psa_crypto_init()!=PSA_SUCCESS)return 72;
 unsigned char random[32];if(psa_generate_random(random,sizeof(random))!=PSA_SUCCESS)return 73;
 for(size_t i=0;i<ROOT_COUNT;i++)if(trial(i,0,0))return 74;
 mbedtls_platform_set_calloc_free(alloc,dealloc);size_t faults=0;
 for(size_t i=0;i<ROOT_COUNT;i++){
  if(trial(i,0,0))return 1;size_t n=calls;
  if(trial(i,0,1))return 2;
  for(size_t f=1;f<=n;f++){int ret=trial(i,f,0);if(ret!=2){printf("root=%zu fault=%zu ret=%d residual=%zu\n",i,f,ret,live);return 3;}faults++;}
 }
 printf("roots=118 exact-selection=118 equivalent-name-prefilter=118 allocation-faults=%zu max-returned-requested-bytes=%zu residual=0\n",faults,max_returned);return 0;
}
