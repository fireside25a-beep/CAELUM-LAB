/* Full concrete Arrow C Data / DLPack / ONNX native exchange tests. */
#include "caelum_exchange.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(v) do { if(!(v)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#v);return 1;}checks++; }while(0)
static int checks=0;
int main(void){
 double x[]={1,2,3,4,5,6,7,8,9,10,11,12};
 double y[]={3,7,11,15};cp_dataset d={x,y,4,3};
 struct ArrowArray a={0};struct ArrowSchema s={0};
 CHECK(cp_arrow_export_dataset(&d,&a,&s)==CP_OK);
 CHECK(a.release&&s.release&&a.length==4&&a.n_children==4);
 CHECK(strcmp(s.format,"+s")==0&&strcmp(s.children[3]->name,"y")==0);
 cp_dataset imported={0};CHECK(cp_arrow_import_dataset(&a,&s,&imported)==CP_OK);
 CHECK(imported.rows==4&&imported.features==3);
 for(int i=0;i<12;i++)CHECK(imported.x[i]==x[i]);
 for(int i=0;i<4;i++)CHECK(imported.y[i]==y[i]);
 cp_dataset_release(&imported);
 const char *name=s.children[0]->name;s.children[0]->name="not_feature";
 CHECK(cp_arrow_import_dataset(&a,&s,&imported)==CP_INVALID);
 s.children[0]->name=name;
 const char *format=s.children[1]->format;s.children[1]->format="f";
 CHECK(cp_arrow_import_dataset(&a,&s,&imported)==CP_UNSUPPORTED);
 s.children[1]->format=format;
 a.release(&a);s.release(&s);CHECK(a.release==NULL&&s.release==NULL);

 cp_dl_managed_tensor_versioned *t=NULL;
 CHECK(cp_dlpack_export_features(&d,&t)==CP_OK&&t&&t->deleter);
 CHECK(t->version.major==1&&t->dl_tensor.ndim==2&&t->dl_tensor.dtype.bits==64);
 cp_owned_matrix mat={0};CHECK(cp_dlpack_import_features(t,&mat)==CP_OK);
 CHECK(mat.rows==4&&mat.columns==3);
 for(int i=0;i<12;i++)CHECK(mat.data[i]==x[i]);
 cp_owned_matrix_release(&mat);
 cp_dl_managed_tensor_versioned bad=*t;bad.version.major=99;
 CHECK(cp_dlpack_import_features(&bad,&mat)==CP_UNSUPPORTED);
 bad=*t;bad.dl_tensor.device.device_type=2;
 CHECK(cp_dlpack_import_features(&bad,&mat)==CP_UNSUPPORTED);
 t->deleter(t);

 /* External producer with two-dimensional positive-stride data. */
 double strided[]={1,99,2,99,3,99,4,99,5,99,6,99};
 int64_t shape[]={2,3},stride[]={6,2};cp_dl_managed_tensor_versioned external={0};
 external.version.major=1;external.dl_tensor.data=strided;external.dl_tensor.ndim=2;
 external.dl_tensor.device.device_type=1;external.dl_tensor.dtype.code=2;
 external.dl_tensor.dtype.bits=64;external.dl_tensor.dtype.lanes=1;
 external.dl_tensor.shape=shape;external.dl_tensor.strides=stride;
 CHECK(cp_dlpack_import_features(&external,&mat)==CP_OK);
 for(int i=0;i<6;i++)CHECK(mat.data[i]==(double)i+1);
 cp_owned_matrix_release(&mat);

 cp_linear_model m={0},round={0};m.features=3;m.fitted_rows=4;
 m.beta[0]=2.25;m.beta[1]=1.5;m.beta[2]=-4;m.beta[3]=0.75;m.training_rmse=0.0125;
 CHECK(cp_onnx_export_linear("exchange_model.onnx",&m)==CP_OK);
 CHECK(cp_onnx_import_linear("exchange_model.onnx",&round)==CP_OK);
 CHECK(memcmp(&m,&round,sizeof(m))==0);
 CHECK(cp_capsule_save("exchange_model.cpm",&round)==CP_OK);
 FILE *f=fopen("exchange_model.onnx","r+b");CHECK(f!=NULL);
 CHECK(fseek(f,0,SEEK_END)==0);long n=ftell(f);CHECK(n>100&&n<8192);
 CHECK(fseek(f,0,SEEK_SET)==0);int first=fgetc(f);CHECK(first!=EOF);
 CHECK(fseek(f,0,SEEK_SET)==0);CHECK(fputc(first^0xFF,f)!=EOF);CHECK(fclose(f)==0);
 CHECK(cp_onnx_import_linear("exchange_model.onnx",&round)!=CP_OK);
 CHECK(cp_onnx_export_linear("exchange_model.onnx",&m)==CP_OK);
 puts("PASS native Arrow, DLPack, stride and ONNX round trips, malformed input rejection");
 printf("native_exchange_checks=%d\n",checks);
 return 0;
}
