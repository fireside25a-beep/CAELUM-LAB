/* Native C11 conformance for CAELUM's implicit-midpoint model integrator.
 * Known analytical fields are test fixtures, not simulated physical evidence.
 * These tests intentionally distinguish model error from numerical inversion.
 */
#include "caelum_science.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static unsigned checks=0;
#define TEST(x) do {if(!(x)){fprintf(stderr,"DYNAMICS FAIL line %d: %s\n",__LINE__,#x);return 1;}++checks;}while(0)
static int near(double a,double b,double tol){return isfinite(a)&&isfinite(b)&&fabs(a-b)<=tol;}
static cp_law base(unsigned dims,unsigned terms){
 cp_law m={0};m.inputs=dims;m.degree=2;m.term_count=terms;
 m.selection_steps=terms;m.attempted_candidates=terms-1;
 m.train_rows=100;m.holdout_rows=40;return m;
}
static void oscillator(cp_law *laws){
 laws[0]=base(2,2);laws[1]=base(2,2);
 laws[0].powers[1][1]=1;laws[0].coefficients[1]=1;
 laws[1].powers[1][0]=1;laws[1].coefficients[1]=-1;
}
static double maxerr(const double *a,const double *b,size_t count){
 double result=0;for(size_t i=0;i<count;i++)if(fabs(a[i]-b[i])>result)result=fabs(a[i]-b[i]);return result;
}
int main(void){
 cp_law laws[CP_LAW_INPUT_LIMIT]={{0}};oscillator(laws);
 double x[2]={0.8,-0.6},next[2]={0},old[2]={0};
 for(unsigned i=0;i<512;i++){
  double dt=0.0001+0.032*(double)(i%41)/41.0;
  TEST(cp_dynamics_step_midpoint(laws,2,x,dt,next)==CP_OK);
  double a=(1-0.25*dt*dt)/(1+0.25*dt*dt),b=dt/(1+0.25*dt*dt);
  TEST(near(next[0],a*x[0]+b*x[1],5e-14));
  TEST(near(next[1],-b*x[0]+a*x[1],5e-14));
  TEST(cp_dynamics_step_midpoint(laws,2,next,-dt,old)==CP_OK);
  TEST(maxerr(x,old,2)<8e-14);
 }
 const size_t steps=2600;
 double *dt=malloc(steps*sizeof(double));
 double *forward=malloc((steps+1)*2*sizeof(double));
 double *backward=malloc((steps+1)*2*sizeof(double));
 TEST(dt&&forward&&backward);
 for(size_t i=0;i<steps;i++)dt[i]=0.0002+0.009*(double)(i%23)/23;
 TEST(cp_dynamics_trace_midpoint(laws,2,x,dt,steps,forward)==CP_OK);
 for(size_t i=0;i<steps/2;i++){
  double v=dt[i];dt[i]=-dt[steps-1-i];dt[steps-1-i]=-v;
 }
 TEST(cp_dynamics_trace_midpoint(laws,2,forward+2*steps,dt,steps,backward)==CP_OK);
 for(size_t i=0;i<=steps;i++){
  TEST(maxerr(forward+2*i,backward+2*(steps-i),2)<2e-11);
  TEST(near(hypot(forward[2*i],forward[2*i+1]),1.0,3e-11));
 }
 /* A failed multistep operation must not partially commit to caller memory. */
 double sentinel[8]={11,12,13,14,15,16,17,18};
 double invalid_dt[3]={0.01,INFINITY,0.01};
 TEST(cp_dynamics_trace_midpoint(laws,2,x,invalid_dt,3,sentinel)==CP_INVALID);
 for(unsigned i=0;i<8;i++)TEST(sentinel[i]==11+(double)i);
 TEST(cp_dynamics_step_midpoint(laws,2,x,0,old)==CP_INVALID);
 TEST(cp_dynamics_step_midpoint(laws,2,x,NAN,old)==CP_INVALID);
 laws[0].powers[1][0]=255;
 TEST(cp_dynamics_step_midpoint(laws,2,x,0.01,old)==CP_INVALID);
 laws[0].powers[1][0]=0;
 TEST(cp_dynamics_step_midpoint(laws,2,x,1e308,old)!=CP_OK);
 /* Externally observed state and learned model inversion are DIFFERENT tests. */
 const size_t n=1200;
 double *times=malloc(n*sizeof(double)),*states=malloc(n*2*sizeof(double));
 TEST(times&&states);
 for(size_t i=0;i<n;i++){
  double t=0.01*(double)i+0.0008*sin(0.19*(double)i);
  times[i]=t;states[2*i]=sin(t);states[2*i+1]=cos(t);
 }
 cp_matrix_view view={states,n,2,2};
 cp_law learned[CP_LAW_INPUT_LIMIT]={{0}},changed[CP_LAW_INPUT_LIMIT]={{0}};
 TEST(cp_dynamics_discover(view,times,1,8321,learned)==CP_OK);
 cp_dynamics_audit audit={0};
 TEST(cp_dynamics_audit_holdout(learned,view,times,&audit)==CP_OK);
 TEST(audit.anchor_row==n*7/10-1&&audit.forecast_steps==n-1-audit.anchor_row);
 TEST(audit.forward_rmse<0.01&&audit.backward_observed_rmse<0.01);
 TEST(audit.cycle_rmse<2e-10&&audit.cycle_max_absolute_error<2e-9);
 printf("Measured forward=%.12g backward_observed=%.12g cycle=%.12g peak=%.12g\n",
  audit.forward_rmse,audit.backward_observed_rmse,audit.cycle_rmse,audit.cycle_max_absolute_error);
 /* Holdout-only tampering cannot affect training coefficients or term selection. */
 size_t split=n*7/10;
 states[2*(split+4)]+=0.04;
 TEST(cp_dynamics_discover(view,times,1,8321,changed)==CP_OK);
 for(unsigned k=0;k<2;k++){
  TEST(learned[k].term_count==changed[k].term_count);
  for(unsigned j=0;j<learned[k].term_count;j++){
   TEST(memcmp(learned[k].powers[j],changed[k].powers[j],CP_LAW_INPUT_LIMIT)==0);
   TEST(learned[k].coefficients[j]==changed[k].coefficients[j]);
  }
  TEST(learned[k].holdout_rmse!=changed[k].holdout_rmse);
 }
 states[2*(split+4)]-=0.04;
 times[100]=times[99];
 TEST(cp_dynamics_discover(view,times,1,8321,changed)==CP_INVALID);
 TEST(cp_dynamics_audit_holdout(learned,view,times,&audit)==CP_INVALID);
 times[100]=0.01*100+0.0008*sin(0.19*100);
 states[2*(n-1)]=NAN;
 TEST(cp_dynamics_audit_holdout(learned,view,times,&audit)==CP_NUMERIC);
 states[2*(n-1)]=sin(times[n-1]);
 /* Nonlinear 3D field checks analytical Jacobian, pivoting and invertibility. */
 cp_law lorenz[CP_LAW_INPUT_LIMIT]={{0}};
 for(unsigned i=0;i<3;i++)lorenz[i]=base(3,4);
 lorenz[0].powers[1][0]=1;lorenz[0].coefficients[1]=-10;
 lorenz[0].powers[2][1]=1;lorenz[0].coefficients[2]=10;
 lorenz[0].powers[3][2]=1;
 lorenz[1].powers[1][0]=1;lorenz[1].coefficients[1]=28;
 lorenz[1].powers[2][1]=1;lorenz[1].coefficients[2]=-1;
 lorenz[1].powers[3][0]=1;lorenz[1].powers[3][2]=1;lorenz[1].coefficients[3]=-1;
 lorenz[2].powers[1][0]=1;
 lorenz[2].powers[2][2]=1;lorenz[2].coefficients[2]=-8.0/3.0;
 lorenz[2].powers[3][0]=1;lorenz[2].powers[3][1]=1;lorenz[2].coefficients[3]=1;
 double point[3]={1,1,1},later[3]={0},earlier[3]={0};
 for(unsigned i=0;i<300;i++){
  TEST(cp_dynamics_step_midpoint(lorenz,3,point,0.0003,later)==CP_OK);
  TEST(cp_dynamics_step_midpoint(lorenz,3,later,-0.0003,earlier)==CP_OK);
  TEST(maxerr(point,earlier,3)<1e-10);
  memcpy(point,later,sizeof(point));
 }
 free(dt);free(forward);free(backward);free(times);free(states);
 printf("PASS midpoint inverse and strict future-block checks=%u\n",checks);
 return 0;
}
