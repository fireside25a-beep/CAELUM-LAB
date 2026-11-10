#include "caelum_science.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define TEST(x) do{if(!(x)){fprintf(stderr,"SCIENCE FAIL line %d: %s\n",__LINE__,#x);return 1;} checks++;}while(0)
static int checks=0;
static int close_to(double a,double b,double tol){return isfinite(a)&&fabs(a-b)<=tol;}
static void known(double *x,double *y,unsigned rows){
 for(unsigned i=0;i<rows;i++){
  double a=sin((double)i*0.731)+cos((double)i*0.337);
  double b=cos((double)i*0.483)-sin((double)i*0.149);
  x[2*i]=a;x[2*i+1]=b;y[i]=1.25+2*a*a-0.75*a*b;
 }
}
int main(void){
 cp_random_stream r={0},r2={0};
 TEST(cp_random_init(&r,"deterministic",UINT64_C(0x123456789abcdef0),5)==CP_OK);
 TEST(cp_random_init(&r2,"recorded",r.seed,r.stream)==CP_OK);
 for(unsigned i=0;i<1000;i++){
  TEST(cp_random_at(&r,i)==cp_random_at(&r2,i));
  double u=cp_random_unit(&r,i);TEST(u>=0&&u<1);
 }
 TEST(cp_random_init(&r2,"os_random",1,1)==CP_INVALID);
 TEST(cp_random_at(NULL,1)==0);
 const unsigned rows=260;
 double *x=malloc(rows*2*sizeof(double)),*y=malloc(rows*sizeof(double));
 TEST(x&&y);known(x,y,rows);
 cp_matrix_view view={x,rows,2,2};cp_law m={0},b={0};
 TEST(cp_law_discover(view,y,2,12345,&m)==CP_OK);
 TEST(m.inputs==2&&m.term_count==3);
 TEST(m.train_rows+m.holdout_rows==rows);
 TEST(m.train_rmse<1e-11&&m.holdout_rmse<1e-11);
 for(unsigned i=0;i<rows;i++){
  double p=0;TEST(cp_law_predict(&m,&x[2*i],&p)==CP_OK);TEST(close_to(p,y[i],1e-11));
 }
 cp_law repeat={0};TEST(cp_law_discover(view,y,2,12345,&repeat)==CP_OK);
 TEST(memcmp(&repeat,&m,sizeof(m))==0);
 cp_linear_model linear={0};TEST(cp_law_as_linear(&m,&linear)==CP_OK);
 TEST(cp_capsule_save("science_model.cpm",&linear)==CP_OK);
 cp_linear_model load={0};TEST(cp_capsule_load("science_model.cpm",&load)==CP_OK);
 TEST(memcmp(&linear,&load,sizeof(load))==0);
 for(unsigned i=0;i<rows;i++){
  double terms[CP_LAW_TERM_LIMIT]={0},pred=0,back=0;
  TEST(cp_law_basis(&m,&x[2*i],terms)==CP_OK);
  TEST(cp_predict_linear(&load,terms,&pred)==CP_OK);
  TEST(cp_law_predict(&m,&x[2*i],&back)==CP_OK);
  TEST(close_to(pred,back,1e-12));
 }
 TEST(cp_onnx_export_linear("science_law.onnx",&load)==CP_OK);
 cp_linear_model onnx={0};TEST(cp_onnx_import_linear("science_law.onnx",&onnx)==CP_OK);
 for(unsigned i=0;i<CP_MAX_COEFFICIENTS;i++)TEST(onnx.beta[i]==linear.beta[i]);
 /* Exhaustive single-bit corruption: every one of the 1600 on-disk bits. */
 FILE *file=fopen("science_model.cpm","rb");TEST(file);
 uint8_t good[CP_CAPSULE_BYTES];TEST(fread(good,1,sizeof(good),file)==sizeof(good));TEST(fclose(file)==0);
 for(unsigned byte=0;byte<CP_CAPSULE_BYTES;byte++)for(unsigned bit=0;bit<8;bit++){
  uint8_t bad[CP_CAPSULE_BYTES];memcpy(bad,good,sizeof(bad));bad[byte]^=(uint8_t)(1u<<bit);
  file=fopen("science_mutated.cpm","wb");TEST(file);
  TEST(fwrite(bad,1,sizeof(bad),file)==sizeof(bad));TEST(fclose(file)==0);
  cp_linear_model untouched=load;cp_status s=cp_capsule_load("science_mutated.cpm",&untouched);
  TEST(s!=CP_OK);TEST(memcmp(&untouched,&load,sizeof(load))==0);
 }
 /* The intervention chooser exhausts its stated finite domain. */
 b=m;b.coefficients[1]+=1.0;
 cp_intervention_design grid={0};grid.inputs=2;grid.grid_points=7;
 grid.lower[0]=-2;grid.upper[0]=2;grid.lower[1]=-1;grid.upper[1]=1;
 cp_intervention_choice choice={0};
 TEST(cp_design_disagreement(&m,&b,&grid,&choice)==CP_OK);
 TEST(choice.enumerated==49);
 double maximum=0;
 for(int i=0;i<7;i++)for(int j=0;j<7;j++){
  double z[2]={-2+i*4.0/6.0,-1+j*2.0/6.0},a=0,c=0;
  TEST(cp_law_predict(&m,z,&a)==CP_OK);TEST(cp_law_predict(&b,z,&c)==CP_OK);
  if(fabs(a-c)>maximum)maximum=fabs(a-c);
 }
 TEST(close_to(choice.disagreement,maximum,1e-12));
 TEST(cp_design_disagreement(&m,&m,&grid,&choice)==CP_UNAVAILABLE);
 grid.lower[0]=NAN;TEST(cp_design_disagreement(&m,&b,&grid,&choice)==CP_INVALID);
 /* Explicit randomized treatment labels: nonrandom labels are refused. */
 uint8_t trt[200];double outcomes[200];
 for(unsigned i=0;i<200;i++){
  trt[i]=(uint8_t)(i%2);
  outcomes[i]=4.0+3.0*trt[i]+0.25*(double)((i/2)%2);
 }
 cp_effect_estimate effect={0};
 TEST(cp_randomized_effect(trt,outcomes,200,0,&effect)==CP_INVALID);
 TEST(cp_randomized_effect(trt,outcomes,200,1,&effect)==CP_OK);
 TEST(effect.control_n==100&&effect.treated_n==100);
 TEST(close_to(effect.difference,3,1e-12));
 TEST(effect.ci95_lower<3&&effect.ci95_upper>3);
 trt[0]=5;TEST(cp_randomized_effect(trt,outcomes,200,1,&effect)==CP_INVALID);trt[0]=0;
 outcomes[1]=NAN;TEST(cp_randomized_effect(trt,outcomes,200,1,&effect)==CP_NUMERIC);
 double pvals[4]={0.03,0.001,0.01,0.3},adj[4]={0};
 TEST(cp_holm_adjust(pvals,4,adj)==CP_OK);
 TEST(close_to(adj[1],0.004,1e-14));TEST(close_to(adj[2],0.03,1e-14));
 TEST(close_to(adj[0],0.06,1e-14));TEST(close_to(adj[3],0.3,1e-14));
 pvals[2]=NAN;TEST(cp_holm_adjust(pvals,4,adj)==CP_INVALID);
 /* Derivative discovery on full native trajectory, with a future time block. */
 const size_t n=1100;
 double *trajectory=malloc(n*2*sizeof(double)),*times=malloc(n*sizeof(double));
 TEST(trajectory&&times);
 for(size_t i=0;i<n;i++){
  times[i]=(double)i*0.01;
  trajectory[i*2]=sin(times[i]);trajectory[i*2+1]=cos(times[i]);
 }
 cp_matrix_view tv={trajectory,n,2,2};cp_law recovered[CP_LAW_INPUT_LIMIT]={{0}};
 TEST(cp_dynamics_discover(tv,times,1,8321,recovered)==CP_OK);
 for(size_t i=800;i<1000;i++){
  double vx=0,vy=0;
  TEST(cp_law_predict(&recovered[0],trajectory+2*i,&vx)==CP_OK);
  TEST(cp_law_predict(&recovered[1],trajectory+2*i,&vy)==CP_OK);
  TEST(close_to(vx,cos(times[i]),0.002));TEST(close_to(vy,-sin(times[i]),0.002));
 }
 TEST(recovered[0].holdout_rmse<0.002&&recovered[1].holdout_rmse<0.002);
 /* Irregular positive grid: nonuniform quadratic differentiation. */
 for(size_t i=0;i<n;i++){
  times[i]=0.01*(double)i+0.0008*sin(0.19*(double)i);
  trajectory[2*i]=sin(times[i]);trajectory[2*i+1]=cos(times[i]);
 }
 TEST(cp_dynamics_discover(tv,times,1,8321,recovered)==CP_OK);
 for(size_t i=800;i<1000;i++){
  double vx=0,vy=0;
  TEST(cp_law_predict(&recovered[0],trajectory+2*i,&vx)==CP_OK);
  TEST(cp_law_predict(&recovered[1],trajectory+2*i,&vy)==CP_OK);
  TEST(close_to(vx,cos(times[i]),0.002));TEST(close_to(vy,-sin(times[i]),0.002));
 }
 /* Future block comes strictly after the last training derivative's input.
    Independently report forecast, inversion from actual future data and the
    numerical midpoint cycle -- they are three different scientific claims. */
 cp_dynamics_audit audited={0};
 TEST(cp_dynamics_audit_holdout(recovered,tv,times,&audited)==CP_OK);
 TEST(audited.anchor_row==n*7/10-1 && audited.forecast_steps==n-1-audited.anchor_row);
 TEST(audited.forward_rmse<0.005 && audited.backward_observed_rmse<0.005);
 TEST(audited.cycle_rmse<1e-11 && audited.cycle_max_absolute_error<1e-10);
 /* Exact known ground truth: a two-state harmonic vector field, x'=y,
    y'=-x. The independently derived Cayley matrix is the midpoint solution. */
 cp_law oscillator[CP_LAW_INPUT_LIMIT]={{0}};
 for(unsigned i=0;i<2;i++){
  oscillator[i].inputs=2;oscillator[i].degree=1;oscillator[i].term_count=2;
  oscillator[i].selection_steps=2;oscillator[i].attempted_candidates=1;
 }
 oscillator[0].powers[1][1]=1;oscillator[0].coefficients[1]=1;
 oscillator[1].powers[1][0]=1;oscillator[1].coefficients[1]=-1;
 double starting[2]={0.8,-0.6},next[2]={NAN,NAN},reversed[2]={NAN,NAN};
 for(unsigned i=0;i<256;i++){
  double h=0.0001+0.031*(double)(i%37)/37.0;
  TEST(cp_dynamics_step_midpoint(oscillator,2,starting,h,next)==CP_OK);
  double ratio=(1-0.25*h*h)/(1+0.25*h*h),a=h/(1+0.25*h*h);
  TEST(close_to(next[0],ratio*starting[0]+a*starting[1],1e-13));
  TEST(close_to(next[1],-a*starting[0]+ratio*starting[1],1e-13));
  TEST(cp_dynamics_step_midpoint(oscillator,2,next,-h,reversed)==CP_OK);
  TEST(close_to(reversed[0],starting[0],1e-13));
  TEST(close_to(reversed[1],starting[1],1e-13));
 }
 const size_t count=1800;
 double *steps=malloc(count*sizeof(double));
 double *forward=malloc((count+1)*2*sizeof(double));
 double *back=malloc((count+1)*2*sizeof(double));
 TEST(steps&&forward&&back);
 for(size_t i=0;i<count;i++)steps[i]=0.001+0.004*(double)(i%7)/7.0;
 TEST(cp_dynamics_trace_midpoint(oscillator,2,starting,steps,count,forward)==CP_OK);
 for(size_t i=0;i<count/2;i++){
  double temp=steps[i];steps[i]=-steps[count-1-i];steps[count-1-i]=-temp;
 }
 if(count%2)steps[count/2]=-steps[count/2];
 TEST(cp_dynamics_trace_midpoint(oscillator,2,forward+2*count,steps,count,back)==CP_OK);
 for(size_t i=0;i<=count;i++){
  size_t j=count-i;
  TEST(close_to(forward[2*i],back[2*j],2e-12));
  TEST(close_to(forward[2*i+1],back[2*j+1],2e-12));
  TEST(close_to(hypot(forward[2*i],forward[2*i+1]),1,3e-12));
 }
 double last_valid[2]={11,22};
 oscillator[0].powers[1][0]=255;
 TEST(cp_dynamics_step_midpoint(oscillator,2,starting,0.01,last_valid)==CP_INVALID);
 TEST(last_valid[0]==11 && last_valid[1]==22);
 oscillator[0].powers[1][0]=0;
 oscillator[0].powers[1][2]=1;
 TEST(cp_dynamics_step_midpoint(oscillator,2,starting,0.01,last_valid)==CP_INVALID);
 TEST(last_valid[0]==11 && last_valid[1]==22);
 oscillator[0].powers[1][2]=0;
 TEST(cp_dynamics_step_midpoint(oscillator,2,starting,NAN,last_valid)==CP_INVALID);
 TEST(last_valid[0]==11 && last_valid[1]==22);
 steps[0]=NAN;
 double sentinel[4]={4,5,6,7};
 TEST(cp_dynamics_trace_midpoint(oscillator,2,starting,steps,count,back)==CP_INVALID);
 TEST(cp_dynamics_step_midpoint(oscillator,2,starting,1e308,last_valid)!=CP_OK);
 TEST(last_valid[0]==11 && last_valid[1]==22);
 TEST(cp_dynamics_trace_midpoint(oscillator,2,starting,steps,1,sentinel)==CP_INVALID);
 TEST(sentinel[0]==4 && sentinel[1]==5 && sentinel[2]==6 && sentinel[3]==7);
 free(steps);free(forward);free(back);
 /* Genuine nonlinear field: Lorenz 63, tiny steps, inverse the numerical
    trajectory. This tests the Jacobian, pivoting, and nonlinear solver. */
 cp_law lorenz[CP_LAW_INPUT_LIMIT]={{0}};
 for(unsigned i=0;i<3;i++){
  lorenz[i].inputs=3;lorenz[i].degree=2;lorenz[i].term_count=4;
  lorenz[i].selection_steps=4;lorenz[i].attempted_candidates=3;
 }
 /* x' = 10*y - 10*x (third zero term simply changes no physics). */
 lorenz[0].powers[1][0]=1;lorenz[0].coefficients[1]=-10;
 lorenz[0].powers[2][1]=1;lorenz[0].coefficients[2]=10;
 lorenz[0].powers[3][2]=1;
 /* y' = 28*x - y - x*z. */
 lorenz[1].powers[1][0]=1;lorenz[1].coefficients[1]=28;
 lorenz[1].powers[2][1]=1;lorenz[1].coefficients[2]=-1;
 lorenz[1].powers[3][0]=1;lorenz[1].powers[3][2]=1;lorenz[1].coefficients[3]=-1;
 /* z' = x*y - 8z/3, plus unused x term. */
 lorenz[2].powers[1][0]=1;
 lorenz[2].powers[2][2]=1;lorenz[2].coefficients[2]=-8.0/3.0;
 lorenz[2].powers[3][0]=1;lorenz[2].powers[3][1]=1;lorenz[2].coefficients[3]=1;
 double point[3]={1,1,1},later[3],earlier[3];
 for(unsigned i=0;i<300;i++){
  TEST(cp_dynamics_step_midpoint(lorenz,3,point,0.0003,later)==CP_OK);
  TEST(cp_dynamics_step_midpoint(lorenz,3,later,-0.0003,earlier)==CP_OK);
  for(unsigned j=0;j<3;j++)TEST(close_to(point[j],earlier[j],1e-10));
  memcpy(point,later,sizeof(point));
 }
 TEST(cp_dynamics_audit_holdout(recovered,tv,times,&audited)==CP_OK);

 times[100]=times[99];TEST(cp_dynamics_discover(tv,times,1,8321,recovered)==CP_INVALID);
 /* No change to outputs on rejected non-finite scientific data. */
 cp_law saved=m;y[0]=INFINITY;
 TEST(cp_law_discover(view,y,2,12345,&m)==CP_NUMERIC);
 TEST(memcmp(&saved,&m,sizeof(m))==0);
 free(trajectory);free(times);free(x);free(y);
 printf("PASS science native checks=%d with 1600 exhaustive capsule bit flips, polynomial/dynamics, effects, Holm, active-grid, ONNX\n",checks);
 return 0;
}
