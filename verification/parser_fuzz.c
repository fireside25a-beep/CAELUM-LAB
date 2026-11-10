#include "json.h"
#include "caelum_lab.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Deterministic mutation campaign over the actual first-party parser. */
static uint64_t rng=UINT64_C(0xa73c59c211880044);
static uint32_t word(void){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return (uint32_t)rng;}
int main(void){
 const char *seeds[]={"{}","[]","{\"key\":1,\"nested\":[true,false,null,1.5e-200]}","{\"x\":1,\"x\":2}","{\"a\":1,\"\\u0061\":2}","{\"s\":\"\\u1234\"}","-0.12e+3"};
 cp_json *j=malloc(sizeof(*j));if(!j)return 2;
 if(cp_json_parse(j,seeds[3],strlen(seeds[3]))==CP_OK||cp_json_parse(j,seeds[4],strlen(seeds[4]))==CP_OK){free(j);return 1;}
 unsigned accepted=0,rejected=0;
 for(unsigned iteration=0;iteration<100000;iteration++){
  char bytes[2048];size_t n=0;
  if(iteration%2){const char *seed=seeds[word()%7];n=strlen(seed);memcpy(bytes,seed,n);unsigned mutations=1+word()%8;for(unsigned k=0;k<mutations;k++){
   unsigned operation=word()%3;
   if(operation==0&&n)bytes[word()%n]=(char)(word()%256);
   else if(operation==1&&n<sizeof(bytes)){size_t at=word()%(n+1);memmove(bytes+at+1,bytes+at,n-at);bytes[at]=(char)(word()%256);n++;}
   else if(n){size_t at=word()%n;memmove(bytes+at,bytes+at+1,n-at-1);n--;}
  }}else{n=word()%sizeof(bytes);for(size_t k=0;k<n;k++)bytes[k]=(char)(word()%256);}
  cp_status status=cp_json_parse(j,bytes,n);
  if(status==CP_OK){
   accepted++;for(unsigned t=0;t<j->count;t++){
    if(j->tokens[t].next>j->count||j->tokens[t].begin>=j->tokens[t].end||j->tokens[t].end>n){free(j);return 1;}
    if(j->tokens[t].type=='s'){char text[2048];(void)cp_json_string(j,t,text,sizeof(text));}
    if(j->tokens[t].type=='n'){double value=0;(void)cp_json_number(j,t,&value);}
    if(j->tokens[t].type=='{'){(void)cp_json_get(j,t,"key");const char *keys[]={"key"};(void)cp_json_keys(j,t,keys,1);}
    if(j->tokens[t].type=='[')for(unsigned k=0;k<j->tokens[t].children+1;k++)(void)cp_json_at(j,t,k);
   }
  }else rejected++;
 }
 free(j);printf("PASS 100000 bounded parser mutations: %u accepted, %u rejected\n",accepted,rejected);return 0;
}
