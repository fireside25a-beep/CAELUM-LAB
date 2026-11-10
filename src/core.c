#include "caelum_port.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include "bounds.h"
#include "io.h"
#include "numeric_internal.h"
#include "locale_numeric.h"

const char *cp_status_string(cp_status s){
 static const char *const names[]={"ok","invalid_argument","range_error","numeric_error","rank_deficient","io_error","corrupt_data","unsupported","unavailable","abi_mismatch","adapter_error","out_of_memory"};
 if((unsigned)s>=sizeof(names)/sizeof(names[0]))return "unknown_status";
 return names[s];
}
uint32_t cp_abi_version(void){return (CP_ABI_MAJOR<<16)|CP_ABI_MINOR;}
static int sane_model(const cp_linear_model *m){
 if(!m || m->features==0 || m->features>CP_MAX_FEATURES || !isfinite(m->training_rmse)||m->training_rmse<0)return 0;
 for(unsigned j=0;j<CP_MAX_COEFFICIENTS;j++)if(!isfinite(m->beta[j]) || (j>m->features && m->beta[j]!=0))return 0;
 return 1;
}
cp_status cp_predict_linear(const cp_linear_model *m,const double *x,double *out){
 if(!sane_model(m)||!x||!out)return CP_INVALID;
 double y=m->beta[0];
 for(unsigned j=0;j<m->features;j++){if(!isfinite(x[j]))return CP_NUMERIC;y+=m->beta[j+1]*x[j];}
 if(!isfinite(y))return CP_NUMERIC;
 *out=y;return CP_OK;
}
cp_status cp_fit_linear_qr_v1(cp_matrix_view x,const double *y,cp_linear_model *out){
 if(!out || !x.values || !y || x.columns==0 || x.columns>CP_MAX_FEATURES || x.rows<x.columns+1 || x.stride<x.columns || x.rows>1000000)return CP_INVALID;
 if(!cp_matrix_span_valid(x))return CP_RANGE;
 const size_t m=(size_t)x.rows,p=(size_t)x.columns+1;
 if(m>SIZE_MAX/p/sizeof(double))return CP_RANGE;
 double *a=malloc(m*p*sizeof(double));double *rhs=malloc(m*sizeof(double));double *v=malloc(m*sizeof(double));
 if(!a||!rhs||!v){free(a);free(rhs);free(v);return CP_NO_MEMORY;}
 cp_status st=CP_OK;cp_linear_model tmp={0};tmp.features=x.columns;tmp.fitted_rows=x.rows;
 for(size_t i=0;i<m;i++){
  if(!isfinite(y[i])){st=CP_NUMERIC;goto done;}rhs[i]=y[i];a[i*p]=1.0;
  for(size_t j=1;j<p;j++){double q=x.values[i*x.stride+j-1];if(!isfinite(q)){st=CP_NUMERIC;goto done;}a[i*p+j]=q;}
 }
 /* Thin Householder QR, double precision. Reject rank deficiency rather than invent coefficients. */
 double scale=0;
 for(size_t i=0;i<m*p;i++)if(fabs(a[i])>scale)scale=fabs(a[i]);
 for(size_t k=0;k<p;k++){
  double norm=0;for(size_t i=k;i<m;i++)norm=hypot(norm,a[i*p+k]);
  if(!isfinite(norm)||norm<=1e-12*scale){st=CP_RANK_DEFICIENT;goto done;}
  double alpha=(a[k*p+k]>=0)?-norm:norm;
  for(size_t i=k;i<m;i++)v[i]=a[i*p+k];
  v[k]-=alpha;
  double vv=0;for(size_t i=k;i<m;i++)vv+=v[i]*v[i];
  if(!isfinite(vv)||vv==0){st=CP_NUMERIC;goto done;}
  for(size_t j=k;j<p;j++){
   double proj=0;for(size_t i=k;i<m;i++)proj+=v[i]*a[i*p+j];proj*=2.0/vv;
   for(size_t i=k;i<m;i++)a[i*p+j]-=proj*v[i];
  }
  double proj=0;for(size_t i=k;i<m;i++)proj+=v[i]*rhs[i];proj*=2.0/vv;
  for(size_t i=k;i<m;i++)rhs[i]-=proj*v[i];
  a[k*p+k]=alpha;for(size_t i=k+1;i<m;i++)a[i*p+k]=0;
 }
 for(size_t kk=p;kk-->0;){
  double z=rhs[kk];for(size_t j=kk+1;j<p;j++)z-=a[kk*p+j]*tmp.beta[j];
  if(!isfinite(z)||!isfinite(a[kk*p+kk])||fabs(a[kk*p+kk])<=1e-12*scale){st=CP_NUMERIC;goto done;}
  tmp.beta[kk]=z/a[kk*p+kk];if(!isfinite(tmp.beta[kk])){st=CP_NUMERIC;goto done;}
 }
 {
  double squares=0;
  for(size_t i=0;i<m;i++){
   double pred=tmp.beta[0];for(size_t j=1;j<p;j++)pred+=tmp.beta[j]*x.values[i*x.stride+j-1];
   double err=pred-y[i];if(!isfinite(err)){st=CP_NUMERIC;goto done;}squares=hypot(squares,err);
  }
  tmp.training_rmse=squares/sqrt((double)m);
 }
 *out=tmp;
 done:free(a);free(rhs);free(v);return st;
}
void cp_dataset_release(cp_dataset *d){if(!d)return;free(d->x);free(d->y);memset(d,0,sizeof(*d));}
static cp_status dataset_parse(FILE *f,const uint8_t *buffer,size_t length,cp_dataset *out){
 if(!out||(!f&&!buffer))return CP_INVALID;
 size_t position=0;
 cp_dataset d={0};uint64_t allocated=0,columns=0;char line[8192];cp_status st=CP_OK;
 for(;;){
  size_t n=0;int ch=0;
  while((ch=f?fgetc(f):(position<length?(int)buffer[position++]:EOF))!=EOF&&ch!='\n'){
   if(ch==0){st=CP_CORRUPT;break;}
   if(n==sizeof(line)-1){st=CP_RANGE;break;}
   line[n++]=(char)ch;
  }
  if(st!=CP_OK)break;
  if(f&&ferror(f)){st=CP_IO;break;}
  if(ch==EOF&&n==0)break;
  line[n]=0;
  if(n&&line[n-1]=='\r')line[--n]=0;
  if(n==0){st=CP_CORRUPT;break;}
  double vals[CP_MAX_COEFFICIENTS+1];uint64_t count=0;
  char *p=line;
  while(1){
   if(count>=CP_MAX_COEFFICIENTS+1){st=CP_RANGE;break;}
   errno=0;char *end;double val=cp_strtod_c(p,&end);
   if(p==end || (errno==ERANGE && val==0) || !isfinite(val)){st=CP_NUMERIC;break;}
   vals[count++]=val;
   if(*end==0)break;
   if(*end!=','){st=CP_CORRUPT;break;}
   p=end+1;
   if(*p==0){st=CP_CORRUPT;break;}
  }
  if(st!=CP_OK)break;
  if(count<2){st=CP_INVALID;break;}
  if(columns==0)columns=count;
  if(count!=columns){st=CP_CORRUPT;break;}
  if(d.rows>=1000000){st=CP_RANGE;break;}
  if(d.rows==allocated){
   uint64_t next=allocated?allocated*2:64;if(next>1000000)next=1000000;
   if(next>SIZE_MAX/(columns-1)/sizeof(double)){st=CP_RANGE;break;}
   double *newx=malloc((size_t)(next*(columns-1)*sizeof(double)));
   double *newy=malloc((size_t)(next*sizeof(double)));
   if(!newx||!newy){free(newx);free(newy);st=CP_NO_MEMORY;break;}
   if(d.rows){memcpy(newx,d.x,(size_t)(d.rows*(columns-1)*sizeof(double)));memcpy(newy,d.y,(size_t)(d.rows*sizeof(double)));}
   free(d.x);free(d.y);d.x=newx;d.y=newy;allocated=next;
  }
  memcpy(d.x+d.rows*(columns-1),vals,(size_t)((columns-1)*sizeof(double)));
  d.y[d.rows]=vals[columns-1];d.rows++;
  if(ch==EOF)break;
 }
 if(f&&ferror(f))st=CP_IO;
 if(st==CP_OK && (d.rows==0||columns<2||columns>CP_MAX_FEATURES+1))st=CP_INVALID;
 if(st!=CP_OK){cp_dataset_release(&d);return st;}
 d.features=columns-1;*out=d;return CP_OK;
}
cp_status cp_dataset_parse_memory(const uint8_t *bytes,size_t size,cp_dataset *out){
 return dataset_parse(NULL,bytes,size,out);
}
cp_status cp_dataset_read_csv(const char *path,cp_dataset *out){
 if(!path||!out)return CP_INVALID;
 cp_status st=cp_recover_pair(path);if(st!=CP_OK)return st;
 FILE *f=fopen(path,"rb");if(!f)return CP_IO;
 cp_dataset result={0};st=dataset_parse(f,NULL,0,&result);
 if(fclose(f)!=0&&st==CP_OK)st=CP_IO;
 if(st==CP_OK)*out=result;else cp_dataset_release(&result);
 return st;
}
static cp_status dataset_write_stream(FILE *f,const void *context){
 const cp_dataset *d=context;
 for(uint64_t i=0;i<d->rows;i++){
  for(uint64_t j=0;j<d->features;j++)
   if(cp_fprintf_c(f,"%.17g,",d->x[i*d->features+j])<0)return CP_IO;
  if(cp_fprintf_c(f,"%.17g\n",d->y[i])<0)return CP_IO;
 }
 return CP_OK;
}
cp_status cp_dataset_write_csv(const char *path,const cp_dataset *d){
 if(!path||!d||!d->x||!d->y||d->rows==0||d->rows>1000000||
    d->features==0||d->features>CP_MAX_FEATURES||d->rows>SIZE_MAX/d->features/sizeof(double))return CP_INVALID;
 for(uint64_t i=0;i<d->rows;i++){
  if(!isfinite(d->y[i]))return CP_NUMERIC;
  for(uint64_t j=0;j<d->features;j++)if(!isfinite(d->x[i*d->features+j]))return CP_NUMERIC;
 }
 return cp_atomic_stream(path,dataset_write_stream,d);
}
/* Stable 200-byte little-endian on-disk capsule v1. Fixed payload length and SHA-256. */
static void w32(uint8_t *p,uint32_t v){for(int j=0;j<4;j++)p[j]=(uint8_t)(v>>(8*j));}
static void w64(uint8_t *p,uint64_t v){for(int j=0;j<8;j++)p[j]=(uint8_t)(v>>(8*j));}
static uint32_t r32(const uint8_t *p){uint32_t v=0;for(int j=3;j>=0;j--)v=(v<<8)|p[j];return v;}
static uint64_t r64(const uint8_t *p){uint64_t v=0;for(int j=7;j>=0;j--)v=(v<<8)|p[j];return v;}
static void wd(uint8_t *p,double v){uint64_t bits;memcpy(&bits,&v,8);w64(p,bits);}
static double rd(const uint8_t *p){uint64_t bits=r64(p);double v;memcpy(&v,&bits,8);return v;}
cp_status cp_capsule_encode(const cp_linear_model *m,uint8_t b[CP_CAPSULE_BYTES]){
 if(!b||!sane_model(m))return CP_INVALID;
 memset(b,0,CP_CAPSULE_BYTES);memcpy(b,"CAELM1\r\n",8);w32(b+8,CP_ABI_MAJOR);
 w32(b+12,(uint32_t)m->features);w64(b+16,m->fitted_rows);
 for(unsigned j=0;j<CP_MAX_COEFFICIENTS;j++)wd(b+24+8*j,m->beta[j]);
 wd(b+160,m->training_rmse);cp_sha256(b,168,b+168);return CP_OK;
}
cp_status cp_capsule_save(const char *path,const cp_linear_model *m){
 if(!path)return CP_INVALID;
 uint8_t b[CP_CAPSULE_BYTES];cp_status status=cp_capsule_encode(m,b);
 return status==CP_OK?cp_atomic_write(path,b,sizeof(b)):status;
}
cp_status cp_capsule_load(const char *path,cp_linear_model *out){
 if(!path||!out)return CP_INVALID;
 cp_status recovery=cp_recover_pair(path);if(recovery!=CP_OK)return recovery;
 FILE *f=fopen(path,"rb");if(!f)return CP_IO;
 uint8_t b[CP_CAPSULE_BYTES];size_t n=fread(b,1,sizeof(b),f);
 int e=ferror(f),extra=EOF;
 if(!e && n==sizeof(b))extra=fgetc(f);
 e |= ferror(f);int close=fclose(f);
 if(e||close)return CP_IO;
 if(n!=sizeof(b)||extra!=EOF)return CP_CORRUPT;
 return cp_capsule_decode(b,out);
}
cp_status cp_capsule_decode(const uint8_t b[CP_CAPSULE_BYTES],cp_linear_model *out){
 if(!b||!out)return CP_INVALID;
 if(memcmp(b,"CAELM1\r\n",8)!=0 || r32(b+8)!=CP_ABI_MAJOR)return CP_ABI_MISMATCH;
 uint8_t hash[32];cp_sha256(b,168,hash);if(memcmp(hash,b+168,32)!=0)return CP_CORRUPT;
 cp_linear_model m={0};m.features=r32(b+12);m.fitted_rows=r64(b+16);
 for(unsigned j=0;j<CP_MAX_COEFFICIENTS;j++)m.beta[j]=rd(b+24+8*j);
 m.training_rmse=rd(b+160);
 if(!sane_model(&m))return CP_CORRUPT;
 *out=m;return CP_OK;
}
