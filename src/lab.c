#include "caelum_science.h"
#include "caelum_lab.h"
#include "identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include "file_identity.h"
#include "io.h"
#include "locale_numeric.h"

/* The evidence body is canonical ASCII. Hex floats retain all binary64 bits.
   Integrity digest protects accidental or deliberate bit corruption, but is
   emphatically NOT a signature or scientific validity certificate. */
static cp_status append(char *buf,size_t capacity,size_t *len,const char *fmt,...){
 if(*len>=capacity)return CP_RANGE;
 va_list ap;va_start(ap,fmt);
 int n=cp_vsnprintf_c(buf+*len,capacity-*len,fmt,ap);
 va_end(ap);
 if(n<0||(size_t)n>=capacity-*len)return CP_RANGE;
 *len+=(size_t)n;return CP_OK;
}
static void hex32(const uint8_t b[32],char text[65]){
 static const char h[]="0123456789abcdef";
 for(unsigned i=0;i<32;i++){text[2*i]=h[b[i]>>4];text[2*i+1]=h[b[i]&15];}
 text[64]=0;
}
static const char *origin(unsigned value){
 static const char *names[]={"deterministic","recorded","rdseed","rdrand"};
 return value<4?names[value]:NULL;
}
static cp_status assemble(const char *csv,unsigned degree,const cp_random_stream *rng,
 char body[CP_LAB_RECEIPT_LIMIT],size_t *body_length,cp_law *law_out,unsigned revision){
 if(!csv||!rng||!body||!body_length||!origin(rng->source)||degree<1||degree>3||revision<1||revision>2)return CP_INVALID;
 if(!cp_rounding_supported())return CP_NUMERIC;
 if((rng->source<2&&rng->hardware_attempts!=0)||
    (rng->source>=2&&(rng->hardware_attempts==0||rng->hardware_attempts>64)))return CP_INVALID;
 char digest[65];uint8_t raw_hash[32];cp_dataset ds={0};
 cp_status st=cp_csv_snapshot(csv,&ds,raw_hash);if(st!=CP_OK)return st;hex32(raw_hash,digest);
 cp_random_stream seed_stream=*rng;
 uint64_t split_seed=revision==1?cp_random_at(&seed_stream,0):cp_random_v2_at(&seed_stream,0);
 cp_law law={0};cp_matrix_view view={ds.x,ds.rows,ds.features,ds.features};
 st=revision==1?cp_law_discover(view,ds.y,degree,split_seed,&law):cp_law_discover_v2(view,ds.y,degree,split_seed,&law);
 cp_dataset_release(&ds);
 if(st!=CP_OK)return st;
 size_t used=0;
 st=append(body,CP_LAB_RECEIPT_LIMIT,&used,
  "CAELUM-EXPERIMENT-A-v%u\n"
  "data_sha256=%s\nsource=%s\nsource_attempts=%u\n"
  "seed=%016" PRIx64 "\nstream=%016" PRIx64 "\nsplit_seed=%016" PRIx64 "\n"
  "degree=%u\ninputs=%u\nterms=%u\ntrain_rows=%" PRIu64 "\nholdout_rows=%" PRIu64 "\n"
  "train_rmse=%a\nholdout_rmse=%a\ntraining_bic=%a\n",
  revision,digest,origin(rng->source),rng->hardware_attempts,rng->seed,rng->stream,
  split_seed,degree,law.inputs,law.term_count,law.train_rows,law.holdout_rows,
  law.train_rmse,law.holdout_rmse,law.training_bic);
 if(st!=CP_OK)return st;
 st=append(body,CP_LAB_RECEIPT_LIMIT,&used,
  "candidate_evaluations=%u\nrank_rejections=%u\nselection_steps=%u\n",
  law.attempted_candidates,law.rank_rejections,law.selection_steps);
 if(st!=CP_OK)return st;
 for(unsigned k=0;k<law.selection_steps;k++){
  st=append(body,CP_LAB_RECEIPT_LIMIT,&used,
   "selection.%02u=term_%u,bic=%a\n",k,
   law.selection_basis_ids[k],law.selection_bic[k]);
  if(st!=CP_OK)return st;
 }
 for(unsigned k=0;k<law.term_count;k++){
  st=append(body,CP_LAB_RECEIPT_LIMIT,&used,
   "term.%02u=%u,%u,%u,%a\n",k,
   (unsigned)law.powers[k][0],(unsigned)law.powers[k][1],
   (unsigned)law.powers[k][2],law.coefficients[k]);
  if(st!=CP_OK)return st;
 }
 if(revision==2){
  st=append(body,CP_LAB_RECEIPT_LIMIT,&used,
   "algorithm=polynomial-forward-bic-v2\ngenerator=philox4x32-10-v2\nsource_identity=%s\nbuild_identity=%s\ncompiler=%s\nrounding=nearest\nnumeric_locale=C\n",CP_SOURCE_ID,CP_BUILD_ID,CP_COMPILER_ID);
  if(st!=CP_OK)return st;
 }
 if(law_out)*law_out=law;
 *body_length=used;return CP_OK;
}
static cp_status sealed_text(char text[CP_LAB_RECEIPT_LIMIT],size_t *length,
 const char *csv,unsigned degree,const cp_random_stream *r,cp_law *law,unsigned revision){
 size_t len=0;cp_status st=assemble(csv,degree,r,text,&len,law,revision);
 if(st!=CP_OK)return st;
 uint8_t digest[32];char hex[65];cp_sha256((const uint8_t*)text,len,digest);hex32(digest,hex);
 st=append(text,CP_LAB_RECEIPT_LIMIT,&len,"receipt_sha256=%s\n",hex);
 if(st!=CP_OK)return st;
 *length=len;return CP_OK;
}
cp_status cp_lab_discover_file(const char *csv,unsigned degree,
 const cp_random_stream *random,const char *receipt){
 if(!receipt||!csv||!strcmp(receipt,csv))return CP_INVALID;
 /* Refuse writing a receipt over its source through aliases or hard links.
    The scientific input must never be destroyed by an output-path choice. */
 if(cp_same_file(csv,receipt))return CP_INVALID;
 char text[CP_LAB_RECEIPT_LIMIT];size_t len=0;
 cp_status st=sealed_text(text,&len,csv,degree,random,NULL,2);
 if(st!=CP_OK)return st;
 return cp_atomic_write(receipt,text,len);
}
static cp_status read_receipt(const char *file,char *text,size_t *length,
 cp_random_stream *rng,unsigned *degree,unsigned *revision){
 if(!file||!text||!length||!rng||!degree)return CP_INVALID;
 FILE *f=fopen(file,"rb");if(!f)return CP_IO;
 size_t n=fread(text,1,CP_LAB_RECEIPT_LIMIT,f);
 int error=ferror(f),extra=EOF;
 if(!error && n==CP_LAB_RECEIPT_LIMIT)extra=fgetc(f);
 error |= ferror(f);int close=fclose(f);
 if(error||close)return CP_IO;
 if(n<160||n>=CP_LAB_RECEIPT_LIMIT||extra!=EOF)return CP_CORRUPT;
 text[n]=0;
 char *end=strstr(text,"receipt_sha256=");
 if(!end||end==text||end[-1]!='\n'||strlen(end)!=80||end[79]!='\n')return CP_CORRUPT;
 if(strstr(end+1,"receipt_sha256=")!=NULL)return CP_CORRUPT;
 uint8_t dig[32];char hex[65];cp_sha256((uint8_t*)text,(size_t)(end-text),dig);hex32(dig,hex);
 if(memcmp(end+15,hex,64)!=0)return CP_CORRUPT;
 if(strncmp(text,"CAELUM-EXPERIMENT-A-v1\n",23)==0)*revision=1;
 else if(strncmp(text,"CAELUM-EXPERIMENT-A-v2\n",23)==0)*revision=2;
 else return CP_ABI_MISMATCH;
 if(*revision==2){
  const char *source=strstr(text,"\nsource_identity="),*build=strstr(text,"\nbuild_identity=");
  if(!source||!build||strncmp(source+strlen("\nsource_identity="),CP_SOURCE_ID,64)||strncmp(build+strlen("\nbuild_identity="),CP_BUILD_ID,64))return CP_ABI_MISMATCH;
 }
 char *line=strstr(text,"\nsource=");if(!line)return CP_CORRUPT;
 line+=8;char source[32]={0};
 char *finish=strchr(line,'\n');if(!finish||finish-line>20)return CP_CORRUPT;
 memcpy(source,line,(size_t)(finish-line));
 unsigned origin_code=4;
 for(unsigned i=0;i<4;i++){static const char *names[]={"deterministic","recorded","rdseed","rdrand"};
  if(!strcmp(source,names[i]))origin_code=i;
 }
 if(origin_code>3)return CP_CORRUPT;
 /* Bounded integer tokens: scanf overflow on hostile numeric text is not a
    valid parser contract, even when a later canonical comparison rejects it. */
 const char *keys[]={"\nsource_attempts=","\nseed=","\nstream=","\ndegree="};
 uint64_t values[4]={0};
 for(unsigned k=0;k<4;k++){
  const char *start=strstr(text,keys[k]);if(!start)return CP_CORRUPT;
  start+=strlen(keys[k]);const char *stop=strchr(start,'\n');if(!stop)return CP_CORRUPT;
  size_t width=(size_t)(stop-start),expected=(k==1||k==2)?16:0;
  if(!width||width>16||(expected&&width!=expected)||(!expected&&width>2))return CP_CORRUPT;
  char token[17]={0};memcpy(token,start,width);
  for(size_t i=0;i<width;i++){
   int valid=expected?((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f')):
                       (token[i]>='0'&&token[i]<='9');
   if(!valid)return CP_CORRUPT;
  }
  errno=0;char *parsed=NULL;unsigned long long value=strtoull(token,&parsed,expected?16:10);
  if(errno||!parsed||*parsed)return CP_CORRUPT;
  values[k]=(uint64_t)value;
 }
 if(values[0]>64||values[3]<1||values[3]>3)return CP_CORRUPT;
 *rng=(cp_random_stream){values[1],values[2],origin_code,(uint32_t)values[0]};
 *degree=(unsigned)values[3];*length=n;return CP_OK;
}
static cp_status compare_receipt(const char *csv,const char *receipt,cp_law *law,uint8_t receipt_hash[32],char data_hash[65]){
 char observed[CP_LAB_RECEIPT_LIMIT]={0};size_t actual=0;
 cp_random_stream rng={0};unsigned degree=0,revision=0;
 cp_status st=read_receipt(receipt,observed,&actual,&rng,&degree,&revision);
 if(st!=CP_OK)return st;
 char expected[CP_LAB_RECEIPT_LIMIT]={0};size_t expected_n=0;
 cp_law rebuilt={0};st=sealed_text(expected,&expected_n,csv,degree,&rng,&rebuilt,revision);
 if(st!=CP_OK)return st;
 if(actual!=expected_n||memcmp(observed,expected,actual)!=0)return CP_CORRUPT;
 if(receipt_hash)cp_sha256((const uint8_t*)observed,actual,receipt_hash);
 if(data_hash){const char *d=strstr(observed,"data_sha256=");if(!d)return CP_CORRUPT;memcpy(data_hash,d+12,64);data_hash[64]=0;}
 if(law)*law=rebuilt;
 return CP_OK;
}
cp_status cp_lab_replay_file(const char *csv,const char *receipt){
 if(!csv||!receipt)return CP_INVALID;
 return compare_receipt(csv,receipt,NULL,NULL,NULL);
}
cp_status cp_lab_receipt_law(const char *csv,const char *receipt,cp_law *out){
 if(!csv||!receipt||!out)return CP_INVALID;
 return compare_receipt(csv,receipt,out,NULL,NULL);
}

cp_status cp_lab_export_file(const char *csv,const char *receipt,const char *capsule,const char *basis){
 if(!csv||!receipt||!capsule||!basis||cp_same_file(capsule,basis)||cp_same_file(csv,capsule)||
 cp_same_file(csv,basis)||cp_same_file(receipt,capsule)||cp_same_file(receipt,basis))return CP_INVALID;
 cp_law law={0};uint8_t rh[32];char dh[65];
 cp_status status=compare_receipt(csv,receipt,&law,rh,dh);if(status!=CP_OK)return status;
 cp_linear_model model={0};status=cp_law_as_linear(&law,&model);if(status!=CP_OK)return status;
 uint8_t bytes[CP_CAPSULE_BYTES],hash[32];status=cp_capsule_encode(&model,bytes);if(status!=CP_OK)return status;
 char model_hash[65],receipt_hash[65];cp_sha256(bytes,sizeof(bytes),hash);hex32(hash,model_hash);hex32(rh,receipt_hash);
 char text[8192];size_t used=0;
 status=append(text,sizeof(text),&used,"CAELUM-POLYNOMIAL-BASIS-v2\nraw_inputs=%u\ntransformed_features=%u\nmodel_sha256=%s\nreceipt_sha256=%s\ndata_sha256=%s\nunits=unspecified\n",
 law.inputs,law.term_count-1,model_hash,receipt_hash,dh);
 for(unsigned j=1;status==CP_OK&&j<law.term_count;j++)
  status=append(text,sizeof(text),&used,"feature_%u=x0^%u*x1^%u*x2^%u\n",j-1,(unsigned)law.powers[j][0],(unsigned)law.powers[j][1],(unsigned)law.powers[j][2]);
 if(status!=CP_OK)return status;
 uint8_t basis_digest[32];char basis_hex[65];cp_sha256((const uint8_t*)text,used,basis_digest);hex32(basis_digest,basis_hex);
 status=append(text,sizeof(text),&used,"basis_sha256=%s\n",basis_hex);if(status!=CP_OK)return status;
 return cp_write_pair(capsule,bytes,sizeof(bytes),basis,text,used);
}
cp_status cp_lab_predict_export(const char *capsule,const char *basis,const double *raw,unsigned inputs,double *out){
 if(!capsule||!basis||!raw||!out||inputs<1||inputs>3)return CP_INVALID;
 cp_linear_model model={0};uint8_t *bytes=NULL,*manifest=NULL;size_t n=0,length=0;
 cp_status status=cp_read_pair(capsule,CP_CAPSULE_BYTES,&bytes,&n,basis,8192,&manifest,&length);if(status!=CP_OK)return status;
 if(n!=CP_CAPSULE_BYTES){free(bytes);free(manifest);return CP_CORRUPT;}
 status=cp_capsule_decode(bytes,&model);if(status!=CP_OK){free(bytes);free(manifest);return status;}
 uint8_t digest[32];char expected[65];cp_sha256(bytes,n,digest);hex32(digest,expected);free(bytes);bytes=NULL;
 if(!length||memchr(manifest,0,length)){free(bytes);free(manifest);return CP_CORRUPT;}
 char text[8193];memcpy(text,manifest,length);text[length]=0;free(manifest);
 char *seal=strstr(text,"basis_sha256=");uint8_t sealed_hash[32];char sealed_hex[65];
 if(!seal||seal==text||seal[-1]!='\n'||strlen(seal)!=78)return CP_CORRUPT;
 cp_sha256((const uint8_t*)text,(size_t)(seal-text),sealed_hash);hex32(sealed_hash,sealed_hex);
 if(memcmp(seal+13,sealed_hex,64))return CP_CORRUPT;
 const char *hash=strstr(text,"\nmodel_sha256=");
 if(strncmp(text,"CAELUM-POLYNOMIAL-BASIS-v2\n",strlen("CAELUM-POLYNOMIAL-BASIS-v2\n"))||!hash||strncmp(hash+strlen("\nmodel_sha256="),expected,64)||hash[strlen("\nmodel_sha256=")+64]!='\n')return CP_CORRUPT;
 /* Parse the bounded canonical integer fields without scanf overflow. */
 unsigned dims=0,features=0;char *d=strstr(text,"\nraw_inputs="),*f=strstr(text,"\ntransformed_features=");
 if(!d||!f)return CP_CORRUPT;
 char *end=NULL;unsigned long dv=strtoul(d+strlen("\nraw_inputs="),&end,10);if(!end||*end!='\n'||dv!=inputs)return CP_CORRUPT;dims=(unsigned)dv;
 unsigned long fv=strtoul(f+strlen("\ntransformed_features="),&end,10);if(!end||*end!='\n'||fv!=model.features||fv>15)return CP_CORRUPT;features=(unsigned)fv;
 double transformed[16]={0};
 for(unsigned j=0;j<dims;j++)if(!isfinite(raw[j]))return CP_NUMERIC;
 for(unsigned j=0;j<features;j++){
  char key[48];(void)cp_snprintf_c(key,sizeof(key),"\nfeature_%u=x0^",j);char *line=strstr(text,key);if(!line)return CP_CORRUPT;
  char *p=line+strlen(key);unsigned powers[3]={0};
  for(unsigned k=0;k<3;k++){
   if(*p<'0'||*p>'3')return CP_CORRUPT;
   powers[k]=(unsigned)(*p++-'0');
   if(k<2){char delimiter[8];(void)cp_snprintf_c(delimiter,sizeof(delimiter),"*x%u^",k+1);size_t count=strlen(delimiter);if(strncmp(p,delimiter,count))return CP_CORRUPT;p+=count;}
   else if(*p!='\n')return CP_CORRUPT;
  }
  if(powers[0]+powers[1]+powers[2]>3)return CP_CORRUPT;
  double v=1;for(unsigned k=0;k<3;k++){if(k>=dims&&powers[k])return CP_CORRUPT;for(unsigned e=0;e<powers[k];e++)v*=raw[k];}
  if(!isfinite(v))return CP_NUMERIC;
  transformed[j]=v;
 }
 return cp_predict_linear(&model,transformed,out);
}
