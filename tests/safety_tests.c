/* Regressions against R1: live native calls, with small owned buffers. */
#include "caelum_science.h"
#include "caelum_exchange.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s:%d %s\n",argv[1],__LINE__,#x);return 1;} } while(0)
static cp_law law(void){
 cp_law m={0};m.inputs=2;m.degree=2;m.term_count=3;
 m.selection_steps=3;m.attempted_candidates=2;
 m.powers[1][1]=1;m.powers[2][0]=2;
 m.coefficients[1]=1;m.coefficients[2]=1;return m;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 cp_law m=law();
 if(!strcmp(argv[1],"basis_alias")){
  double x[16]={2,3};CHECK(cp_law_basis(&m,x,x)==CP_OK);
  CHECK(x[0]==3&&x[1]==4);
 }else if(!strcmp(argv[1],"basis_atomic")){
  double x[2]={1e200,3},out[2]={17,19};
  CHECK(cp_law_basis(&m,x,out)==CP_NUMERIC);CHECK(out[0]==17&&out[1]==19);
 }else if(!strcmp(argv[1],"holm_overlap")){
  double p[5]={.03,.001,.01,.3,7};
  CHECK(cp_holm_adjust(p,4,p+1)==CP_INVALID);CHECK(p[4]==7);
 }else if(!strcmp(argv[1],"matrix_span")){
  double x[120]={0},y[120]={0};cp_matrix_view v={x,100,1,UINT64_MAX};
  cp_linear_model linear={0};cp_law laws[3]={{0}};cp_dynamics_audit a={0};
  CHECK(cp_fit_linear_qr(v,y,&linear)==CP_RANGE);
  CHECK(cp_law_discover(v,y,1,3,&m)==CP_RANGE);
  CHECK(cp_dynamics_discover(v,y,1,3,laws)==CP_RANGE);
  CHECK(cp_dynamics_audit_holdout(laws,v,y,&a)==CP_RANGE);
 }else if(!strcmp(argv[1],"dlpack_flags")){
  double x[4]={1,2,3,4},y[2]={1,2};cp_dataset ds={x,y,2,2};
  cp_dl_managed_tensor_versioned *t=NULL;cp_owned_matrix out={0};
  CHECK(cp_dlpack_export_features(&ds,&t)==CP_OK);CHECK((t->flags&2u)!=0);
  t->flags=UINT64_C(1)<<63;CHECK(cp_dlpack_import_features(t,&out)==CP_UNSUPPORTED);
  t->flags=2;t->dl_tensor.device.device_id=1;
  CHECK(cp_dlpack_import_features(t,&out)==CP_UNSUPPORTED);
  t->deleter(t);
 }else if(!strcmp(argv[1],"arrow_dictionary")){
  double x[4]={1,2,3,4},y[2]={1,2};cp_dataset ds={x,y,2,2},out={0};
  struct ArrowArray a={0};struct ArrowSchema s={0};
  CHECK(cp_arrow_export_dataset(&ds,&a,&s)==CP_OK);
  s.dictionary=&s;CHECK(cp_arrow_import_dataset(&a,&s,&out)==CP_UNSUPPORTED);
  s.dictionary=NULL;a.children[0]->dictionary=&a;
  CHECK(cp_arrow_import_dataset(&a,&s,&out)==CP_UNSUPPORTED);
  a.children[0]->dictionary=NULL;a.release(&a);s.release(&s);
 }else if(!strcmp(argv[1],"midpoint_branch")){
  cp_law f={0};f.inputs=1;f.degree=2;f.term_count=2;
  f.selection_steps=2;f.attempted_candidates=1;f.powers[1][0]=2;
  f.coefficients[0]=-5;f.coefficients[1]=-5;
  double x=-3,out=17;
  CHECK(cp_dynamics_step_midpoint(&f,1,&x,-1,&out)!=CP_OK);CHECK(out==17);
  f.coefficients[0]=0;f.coefficients[1]=1;x=0.5;
  CHECK(cp_dynamics_step_midpoint(&f,1,&x,1,&out)!=CP_OK);CHECK(out==17);
 }else if(!strcmp(argv[1],"csv_nul")){
  const char data[]={'1',',','2',0,'x'};FILE *f=fopen("nul.csv","wb");CHECK(f);
  CHECK(fwrite(data,1,sizeof(data),f)==sizeof(data));CHECK(fclose(f)==0);
  cp_dataset ds={0};CHECK(cp_dataset_read_csv("nul.csv",&ds)==CP_CORRUPT);
 }else return 2;
 printf("PASS %s\n",argv[1]);return 0;
}
