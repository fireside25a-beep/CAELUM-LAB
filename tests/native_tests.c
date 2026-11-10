#include "caelum_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(v) do{if(!(v)){fprintf(stderr,"TEST FAIL %s:%d: %s\n",__FILE__,__LINE__,#v);return 1;}checks++;}while(0)
static int checks=0;
static int near(double a,double b,double tol){return fabs(a-b)<=tol;}
int main(int argc,char **argv){
 CHECK(argc==2);
 const uint8_t abc[]={ 'a','b','c' };uint8_t digest[32];cp_sha256(abc,3,digest);
 const uint8_t expected[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
 CHECK(memcmp(digest,expected,32)==0);
 const double x[]={0,0,1,0,0,1,2,1,3,2,-1,2,4,-2,5,3,6,4};double y[9];
 for(int i=0;i<9;i++)y[i]=4+1.5*x[2*i]-2*x[2*i+1];
 cp_matrix_view view={x,9,2,2};cp_linear_model m={0};
 CHECK(cp_fit_linear_qr(view,y,&m)==CP_OK);
 CHECK(near(m.beta[0],4,1e-12));CHECK(near(m.beta[1],1.5,1e-12));CHECK(near(m.beta[2],-2,1e-12));
 CHECK(m.training_rmse<1e-11);
 double pred=0;const double point[]={2,7};CHECK(cp_predict_linear(&m,point,&pred)==CP_OK);CHECK(near(pred,-7,1e-12));
 CHECK(cp_capsule_save("test_native.cpm",&m)==CP_OK);
 cp_linear_model mr={0};CHECK(cp_capsule_load("test_native.cpm",&mr)==CP_OK);CHECK(memcmp(&m,&mr,sizeof(m))==0);
 FILE *f=fopen("test_native.cpm","r+b");CHECK(f!=NULL);CHECK(fseek(f,35,SEEK_SET)==0);int old=fgetc(f);CHECK(old!=EOF);CHECK(fseek(f,35,SEEK_SET)==0);CHECK(fputc(old^1,f)!=EOF);CHECK(fclose(f)==0);
 CHECK(cp_capsule_load("test_native.cpm",&mr)==CP_CORRUPT);
 CHECK(cp_capsule_save("test_native.cpm",&m)==CP_OK);
 cp_adapter_handle a={0};CHECK(cp_adapter_open(argv[1],&a)==CP_OK);
 CHECK(a.api->write_model("test_external.txt",&m)==CP_OK);
 cp_linear_model imported={0};CHECK(a.api->read_model("test_external.txt",&imported)==CP_OK);
 CHECK(memcmp(&m,&imported,sizeof(m))==0);
 cp_linear_model invalid_export=m;invalid_export.training_rmse=-1;
 CHECK(a.api->write_model("invalid_adapter_output.txt",&invalid_export)==CP_INVALID);
 invalid_export=m;invalid_export.beta[CP_MAX_FEATURES]=1;
 CHECK(a.api->write_model("invalid_adapter_output.txt",&invalid_export)==CP_INVALID);
 cp_adapter_close(&a);
 cp_dataset d={0};d.x=(double*)x;d.y=y;d.rows=9;d.features=2;
 CHECK(cp_dataset_write_csv("test_numbers.csv",&d)==CP_OK);
 cp_dataset back={0};CHECK(cp_dataset_read_csv("test_numbers.csv",&back)==CP_OK);
 CHECK(back.rows==9 && back.features==2);
 for(int i=0;i<9;i++){CHECK(back.y[i]==y[i]);CHECK(back.x[2*i]==x[2*i]);CHECK(back.x[2*i+1]==x[2*i+1]);}
 cp_dataset_release(&back);
 const double xx[]={1,1,2,2,3,3};const double yy[]={1,2,3};cp_matrix_view singular={xx,3,2,2};
 CHECK(cp_fit_linear_qr(singular,yy,&mr)==CP_RANK_DEFICIENT);
 double bad[]={NAN,1,2,3};cp_matrix_view invalid={bad,2,2,2};CHECK(cp_fit_linear_qr(invalid,yy,&mr)!=CP_OK);
 cp_cpu_report r;cp_status status=cp_cpu_query(&r);
 CHECK(status==CP_OK||status==CP_UNSUPPORTED);
 uintptr_t rip=0;cp_status s=cp_native_rip(&rip);
 if(r.arch_x86_64){CHECK(s==CP_OK || s==CP_UNSUPPORTED);if(s==CP_OK)CHECK(rip!=0);}
 else CHECK(s==CP_UNSUPPORTED);
 uint64_t w=0;unsigned tries=0;CHECK(cp_native_word("os",&w,&tries)==CP_INVALID);
 if(r.has_rdrand){s=cp_native_word("rdrand",&w,&tries);CHECK(s==CP_OK || s==CP_UNAVAILABLE);CHECK(tries>=1);}
 if(r.has_rdseed){s=cp_native_word("rdseed",&w,&tries);CHECK(s==CP_OK || s==CP_UNAVAILABLE);CHECK(tries>=1);}
 if(r.has_rdtscp){uint32_t aux=0;s=cp_native_tsc(&w,&aux);CHECK(s==CP_OK && w!=0);}
 CHECK(cp_adapter_open("/some-nonexistent-library.so",&a)==CP_PLUGIN_ERROR);
 printf("PASS: %d concrete native assertions; QR, caps, tamper, adapter, CSV, CPUID, RIP, hardware instruction branches\n",checks);
 return 0;
}
