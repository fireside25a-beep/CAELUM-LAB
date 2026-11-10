/* Concrete external human-readable linear model adapter; real bidirectional import/export. */
#include "caelum_port.h"
#include "locale_numeric.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
static cp_status write_external(const char *path,const cp_linear_model *m){
 if(!path||!m||m->features<1||m->features>CP_MAX_FEATURES ||
    !isfinite(m->training_rmse)||m->training_rmse<0)return CP_INVALID;
 for(unsigned j=0;j<CP_MAX_COEFFICIENTS;j++)
  if(!isfinite(m->beta[j])||(j>m->features&&m->beta[j]!=0))return CP_INVALID;
 char text[2048];size_t used=0;
 int n=cp_snprintf_c(text,sizeof(text),"CAELUM-LINEAR-EXCHANGE,1,%llu,%llu\n",(unsigned long long)m->features,(unsigned long long)m->fitted_rows);
 if(n<0||(size_t)n>=sizeof(text))return CP_RANGE;
 used=(size_t)n;
 for(unsigned j=0;j<=m->features;j++){
  n=cp_snprintf_c(text+used,sizeof(text)-used,"%.17g%c",m->beta[j],j==m->features?'\n':',');
  if(n<0||(size_t)n>=sizeof(text)-used)return CP_RANGE;
  used+=(size_t)n;
 }
 n=cp_snprintf_c(text+used,sizeof(text)-used,"%.17g\n",m->training_rmse);
 if(n<0||(size_t)n>=sizeof(text)-used)return CP_RANGE;
 used+=(size_t)n;
 return cp_write_atomic_file(path,(const uint8_t*)text,used);
}
static cp_status read_external(const char *path,cp_linear_model *out){
 if(!path||!out)return CP_INVALID;
 FILE *f=fopen(path,"rb");if(!f)return CP_IO;
 char a[256],b[2048],c[256];
 if(!fgets(a,sizeof(a),f)||!fgets(b,sizeof(b),f)||!fgets(c,sizeof(c),f)){int e=ferror(f);fclose(f);return e?CP_IO:CP_CORRUPT;}
 int extra=fgetc(f);if(ferror(f)){fclose(f);return CP_IO;}
 if(extra!=EOF){fclose(f);return CP_CORRUPT;}
 const char *prefix="CAELUM-LINEAR-EXCHANGE,1,";
 if(strncmp(a,prefix,strlen(prefix))){fclose(f);return CP_ABI_MISMATCH;}
 char *header=a+strlen(prefix),*stop=NULL;uint64_t fields[2]={0};
 for(unsigned k=0;k<2;k++){
  if(*header<'0'||*header>'9'){fclose(f);return CP_CORRUPT;}
  errno=0;unsigned long long value=strtoull(header,&stop,10);
  if(errno||!stop||*stop!=(k==0?',':'\n')){fclose(f);return CP_CORRUPT;}
  fields[k]=(uint64_t)value;header=stop+1;
 }
 uint64_t features=fields[0],rows=fields[1];
 if(*header||features==0||features>CP_MAX_FEATURES){fclose(f);return CP_ABI_MISMATCH;}
 cp_linear_model m={0};m.features=features;m.fitted_rows=rows;
 char *p=b;
 for(unsigned j=0;j<=m.features;j++){
  errno=0;char *end;double val=cp_strtod_c(p,&end);
  if(p==end||(errno==ERANGE&&val==0)||!isfinite(val)|| (j<m.features && *end!=',')||(j==m.features && *end!='\n')){fclose(f);return CP_CORRUPT;}
  m.beta[j]=val;p=end+1;
 }
 errno=0;char *end;m.training_rmse=cp_strtod_c(c,&end);
 if(end==c||(errno==ERANGE&&m.training_rmse==0)||*end!='\n'||!isfinite(m.training_rmse)||m.training_rmse<0){fclose(f);return CP_CORRUPT;}
 if(fclose(f)!=0)return CP_IO;
 *out=m;return CP_OK;
}
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const cp_adapter_v1 *cp_adapter_get_v1(void){
 static const cp_adapter_v1 api={CP_ABI_MAJOR,CP_ABI_MINOR,sizeof(cp_adapter_v1),1,"caelum-linear-text-v1",read_external,write_external};
 return &api;
}
