#include "caelum_lab.h"
#include <string.h>
#include <stdlib.h>
void cp_philox4x32_10(const uint32_t counter[4],const uint32_t key[2],uint32_t out[4]){
 if(!counter||!key||!out)return;
 uint32_t c[4];memcpy(c,counter,sizeof(c));uint32_t k0=key[0],k1=key[1];
 for(unsigned round=0;round<10;round++){
  uint64_t p0=(uint64_t)UINT32_C(0xd2511f53)*c[0],p1=(uint64_t)UINT32_C(0xcd9e8d57)*c[2];
  uint32_t next[4]={(uint32_t)(p1>>32)^c[1]^k0,(uint32_t)p1,(uint32_t)(p0>>32)^c[3]^k1,(uint32_t)p0};
  memcpy(c,next,sizeof(c));k0+=UINT32_C(0x9e3779b9);k1+=UINT32_C(0xbb67ae85);
 }
 memcpy(out,c,sizeof(c));
}
uint64_t cp_random_v2_at(const cp_random_stream *r,uint64_t counter){
 if(!r)return 0;
 uint32_t c[4]={(uint32_t)counter,(uint32_t)(counter>>32),(uint32_t)r->stream,(uint32_t)(r->stream>>32)};
 uint32_t key[2]={(uint32_t)r->seed,(uint32_t)(r->seed>>32)},out[4];cp_philox4x32_10(c,key,out);
 return (uint64_t)out[0]|((uint64_t)out[1]<<32);
}
cp_status cp_random_named(const cp_random_stream *root,const char *purpose,uint64_t epoch,cp_random_stream *out){
 if(!root||!purpose||!out||!purpose[0]||strlen(purpose)>128||root->source>3)return CP_INVALID;
 uint8_t message[192]={0};size_t n=0;const char tag[]="CAELUM-RNG-DOMAIN-v2";
 memcpy(message,tag,sizeof(tag));n=sizeof(tag);
 uint64_t words[3]={root->seed,root->stream,epoch};
 for(unsigned k=0;k<3;k++)for(unsigned b=0;b<8;b++)message[n++]=(uint8_t)(words[k]>>(8*b));
 size_t len=strlen(purpose);message[n++]=(uint8_t)len;memcpy(message+n,purpose,len);n+=len;
 uint8_t digest[32];cp_sha256(message,n,digest);cp_random_stream r=*root;r.seed=0;r.stream=0;
 for(unsigned b=0;b<8;b++){r.seed|=(uint64_t)digest[b]<<(8*b);r.stream|=(uint64_t)digest[b+8]<<(8*b);}
 *out=r;return CP_OK;
}
