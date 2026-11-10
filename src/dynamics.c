/* CAELUM: numerical reversal of learned continuous-time vector fields.
 * Implicit midpoint is self-adjoint: reversing a successful step with -dt
 * recovers the original state to nonlinear-solver and floating-point accuracy.
 * This is NOT proof of physical reversibility or identification of a true law.
 */
#include "caelum_science.h"
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "bounds.h"

static cp_status field(const cp_law *laws,unsigned n,const double x[CP_LAW_INPUT_LIMIT],
                       double f[CP_LAW_INPUT_LIMIT],double jac[CP_LAW_INPUT_LIMIT][CP_LAW_INPUT_LIMIT],int gradients){
 for(unsigned k=0;k<n;k++){
  if(laws[k].inputs!=n)return CP_INVALID;
  cp_status s=cp_law_predict(&laws[k],x,&f[k]);if(s!=CP_OK)return s;
  if(gradients){
   for(unsigned j=0;j<n;j++){
    double d=0;
    for(unsigned term=0;term<laws[k].term_count;term++){
     unsigned power=laws[k].powers[term][j];if(!power)continue;
     double value=laws[k].coefficients[term]*(double)power;
     for(unsigned v=0;v<n;v++){
      unsigned count=laws[k].powers[term][v]-(unsigned)(v==j);
      for(unsigned e=0;e<count;e++)value*=x[v];
     }
     d+=value;
    }
    if(!isfinite(d))return CP_NUMERIC;
    jac[k][j]=d;
   }
  }
 }
 return CP_OK;
}
/* Each residual has the units and scale of its OWN state coordinate. */
static double scaled_error(unsigned n,const double *error,const double *a,const double *b){
 double norm=0;
 for(unsigned k=0;k<n;k++){
  double scale=fmax(DBL_MIN,fmax(fabs(a[k]),fabs(b[k])));
  double value=fabs(error[k])/scale;
  if(!isfinite(value))return INFINITY;
  norm=fmax(norm,value);
 }
 return norm;
}
static cp_status residual(const cp_law *laws,unsigned n,const double *x,const double *z,double h,
                          double r[CP_LAW_INPUT_LIMIT],double j[CP_LAW_INPUT_LIMIT][CP_LAW_INPUT_LIMIT],int grad){
 double mid[CP_LAW_INPUT_LIMIT]={0},f[CP_LAW_INPUT_LIMIT]={0};
 for(unsigned k=0;k<n;k++)mid[k]=0.5*x[k]+0.5*z[k];
 cp_status s=field(laws,n,mid,f,j,grad);if(s!=CP_OK)return s;
 for(unsigned k=0;k<n;k++){
  r[k]=(z[k]-x[k])-h*f[k];
  if(!isfinite(r[k]))return CP_NUMERIC;
 }
 if(grad){
  for(unsigned k=0;k<n;k++)for(unsigned v=0;v<n;v++){
   j[k][v]=(k==v?1.0:0.0)-0.5*h*j[k][v];
   if(!isfinite(j[k][v]))return CP_NUMERIC;
  }
 }
 return CP_OK;
}
/* Pivoted Gaussian elimination for the maximum 3x3 implicit Jacobian. */
static cp_status solve(unsigned n,double a[CP_LAW_INPUT_LIMIT][CP_LAW_INPUT_LIMIT],
                       const double residual_in[CP_LAW_INPUT_LIMIT],double dx[CP_LAW_INPUT_LIMIT]){
 double b[CP_LAW_INPUT_LIMIT],column[CP_LAW_INPUT_LIMIT]={0};
 /* Row and column equilibration avoid treating a small-unit coordinate as
    singular solely because another Jacobian entry has a large unit scale. */
 for(unsigned i=0;i<n;i++){
  double row=0;for(unsigned j=0;j<n;j++)row=fmax(row,fabs(a[i][j]));
  if(!isfinite(row)||row==0)return CP_RANK_DEFICIENT;
  b[i]=-residual_in[i]/row;if(!isfinite(b[i]))return CP_NUMERIC;
  for(unsigned j=0;j<n;j++){a[i][j]/=row;column[j]=fmax(column[j],fabs(a[i][j]));}
 }
 double norm=0;
 for(unsigned j=0;j<n;j++){
  if(column[j]==0)return CP_RANK_DEFICIENT;
  for(unsigned i=0;i<n;i++){a[i][j]/=column[j];norm=fmax(norm,fabs(a[i][j]));}
 }
 for(unsigned k=0;k<n;k++){
  unsigned pivot=k;for(unsigned i=k+1;i<n;i++)if(fabs(a[i][k])>fabs(a[pivot][k]))pivot=i;
  if(fabs(a[pivot][k])<=64*DBL_EPSILON*norm)return CP_RANK_DEFICIENT;
  if(pivot!=k){
   for(unsigned j=0;j<n;j++){double temp=a[k][j];a[k][j]=a[pivot][j];a[pivot][j]=temp;}
   double temp=b[k];b[k]=b[pivot];b[pivot]=temp;
  }
  for(unsigned i=k+1;i<n;i++){
   double factor=a[i][k]/a[k][k];a[i][k]=0;
   for(unsigned j=k+1;j<n;j++)a[i][j]-=factor*a[k][j];
   b[i]-=factor*b[k];
  }
 }
 for(unsigned k=n;k-->0;){
  double value=b[k];for(unsigned j=k+1;j<n;j++)value-=a[k][j]*dx[j];
  dx[k]=value/a[k][k];if(!isfinite(dx[k]))return CP_NUMERIC;
 }
 for(unsigned k=0;k<n;k++){dx[k]/=column[k];if(!isfinite(dx[k]))return CP_NUMERIC;}
 return CP_OK;
}
static cp_status midpoint_solve(const cp_law *laws,unsigned dimensions,
 const double *initial,double dt,double *next){
 if(!laws||!initial||!next||dimensions==0||dimensions>CP_LAW_INPUT_LIMIT||
    !isfinite(dt)||dt==0)return CP_INVALID;
 double x[CP_LAW_INPUT_LIMIT]={0},z[CP_LAW_INPUT_LIMIT]={0},f[CP_LAW_INPUT_LIMIT]={0};
 double j[CP_LAW_INPUT_LIMIT][CP_LAW_INPUT_LIMIT]={{0}};
 for(unsigned k=0;k<dimensions;k++){
  if(!isfinite(initial[k]))return CP_NUMERIC;
  x[k]=initial[k];
 }
 cp_status s=field(laws,dimensions,x,f,j,0);if(s!=CP_OK)return s;
 for(unsigned k=0;k<dimensions;k++){
  z[k]=x[k]+dt*f[k];if(!isfinite(z[k]))return CP_NUMERIC;
 }
 for(unsigned iter=0;iter<32;iter++){
  double r[CP_LAW_INPUT_LIMIT]={0},dx[CP_LAW_INPUT_LIMIT]={0};
  s=residual(laws,dimensions,x,z,dt,r,j,1);if(s!=CP_OK)return s;
  if(scaled_error(dimensions,r,x,z)<=64*DBL_EPSILON){
   for(unsigned k=0;k<dimensions;k++)next[k]=z[k];
   return CP_OK;
  }
  s=solve(dimensions,j,r,dx);if(s!=CP_OK)return s;
  double prev=scaled_error(dimensions,r,x,z);int accepted=0;
  for(unsigned damping=0;damping<20;damping++){
   double factor=ldexp(1.0,-(int)damping),trial[CP_LAW_INPUT_LIMIT]={0};
   double new_r[CP_LAW_INPUT_LIMIT]={0},unused[CP_LAW_INPUT_LIMIT][CP_LAW_INPUT_LIMIT]={{0}};
   int finite=1;
   for(unsigned k=0;k<dimensions;k++){
    trial[k]=z[k]+factor*dx[k];if(!isfinite(trial[k]))finite=0;
   }
   if(!finite)continue;
   s=residual(laws,dimensions,x,trial,dt,new_r,unused,0);
   if(s==CP_OK&&scaled_error(dimensions,new_r,x,trial)<prev){
    for(unsigned k=0;k<dimensions;k++)z[k]=trial[k];
    accepted=1;break;
   }
   if(s!=CP_OK&&s!=CP_NUMERIC)return s;
  }
  if(!accepted)return CP_NUMERIC;
 }
 return CP_NUMERIC;
}
cp_status cp_dynamics_step_midpoint(const cp_law *laws,unsigned dimensions,
 const double *initial,double dt,double *next){
 if(!next)return CP_INVALID;
 double candidate[CP_LAW_INPUT_LIMIT]={0},recovered[CP_LAW_INPUT_LIMIT]={0};
 double repeated[CP_LAW_INPUT_LIMIT]={0};
 cp_status status=midpoint_solve(laws,dimensions,initial,dt,candidate);
 if(status!=CP_OK)return status;
 status=midpoint_solve(laws,dimensions,candidate,-dt,recovered);
 if(status!=CP_OK)return status;
 /* The equation is self-adjoint but Newton can select another nonlinear
    root. Certify the actual paired solve before committing any output. */
 for(unsigned k=0;k<dimensions;k++)
  if(fabs(recovered[k]-initial[k])/fmax(DBL_MIN,fmax(fabs(candidate[k]),fmax(fabs(initial[k]),fabs(recovered[k]))))>256*DBL_EPSILON)return CP_NUMERIC;
 /* A small residual at a nearly double root can hide a large state error.
    Repeating from the recovered state must select the same forward root. */
 status=midpoint_solve(laws,dimensions,recovered,dt,repeated);
 if(status!=CP_OK)return status;
 for(unsigned k=0;k<dimensions;k++)
  if(fabs(repeated[k]-candidate[k])/fmax(DBL_MIN,fmax(fabs(initial[k]),fmax(fabs(candidate[k]),fabs(repeated[k]))))>256*DBL_EPSILON)return CP_NUMERIC;
 memcpy(next,candidate,dimensions*sizeof(double));return CP_OK;
}
cp_status cp_dynamics_trace_midpoint(const cp_law *laws,unsigned dimensions,
 const double *initial,const double *dt,size_t steps,double *trace){
 if(!laws||!initial||!dt||!trace||dimensions==0||dimensions>CP_LAW_INPUT_LIMIT||
    steps==0||steps>100000||steps>(SIZE_MAX/sizeof(double)/dimensions)-1)return CP_INVALID;
 size_t length=(steps+1)*dimensions;
 double *temporary=malloc(length*sizeof(double));if(!temporary)return CP_NO_MEMORY;
 cp_status status=CP_OK;
 for(unsigned k=0;k<dimensions;k++)temporary[k]=initial[k];
 for(size_t i=0;i<steps;i++){
  status=cp_dynamics_step_midpoint(laws,dimensions,temporary+i*dimensions,dt[i],
                                   temporary+(i+1)*dimensions);
  if(status!=CP_OK)break;
 }
 if(status==CP_OK)memcpy(trace,temporary,length*sizeof(double));
 free(temporary);return status;
}
cp_status cp_dynamics_audit_holdout(const cp_law *laws,cp_matrix_view states,
 const double *times,cp_dynamics_audit *out){
 if(!laws||!out||!states.values||!times||states.columns==0||
    states.columns>CP_LAW_INPUT_LIMIT||states.rows<80||states.rows>100000||
    states.stride<states.columns)return CP_INVALID;
 if(!cp_matrix_span_valid(states))return CP_RANGE;
 const size_t n=(size_t)states.rows,p=(size_t)states.columns;
 const size_t split=n*7/10,anchor=split-1,steps=n-1-anchor;
 if(split<5||steps<3||steps>SIZE_MAX/p/sizeof(double)-1)return CP_RANGE;
 double *dt=malloc(steps*sizeof(double));
 double *forward=malloc((steps+1)*p*sizeof(double));
 double *backward=malloc((steps+1)*p*sizeof(double));
 double *observed=malloc((steps+1)*p*sizeof(double));
 if(!dt||!forward||!backward||!observed){free(dt);free(forward);free(backward);free(observed);return CP_NO_MEMORY;}
 cp_status st=CP_OK;double first[CP_LAW_INPUT_LIMIT]={0},last[CP_LAW_INPUT_LIMIT]={0};
 for(size_t i=0;i<n;i++){
  if(!isfinite(times[i])||(i>0&&times[i]<=times[i-1])){st=CP_INVALID;goto done;}
  for(size_t k=0;k<p;k++)if(!isfinite(states.values[i*states.stride+k])){st=CP_NUMERIC;goto done;}
 }
 for(size_t i=0;i<steps;i++){
  dt[i]=times[anchor+i+1]-times[anchor+i];
  if(!isfinite(dt[i])||dt[i]<=0){st=CP_NUMERIC;goto done;}
 }
 for(size_t j=0;j<p;j++){
  first[j]=states.values[anchor*states.stride+j];
  last[j]=states.values[(n-1)*states.stride+j];
 }
 st=cp_dynamics_trace_midpoint(laws,(unsigned)p,first,dt,steps,forward);
 if(st!=CP_OK)goto done;
 for(size_t i=0;i<steps/2;i++){double t=dt[i];dt[i]=-dt[steps-1-i];dt[steps-1-i]=-t;}
 if(steps%2)dt[steps/2]=-dt[steps/2];
 st=cp_dynamics_trace_midpoint(laws,(unsigned)p,forward+steps*p,dt,steps,backward);
 if(st!=CP_OK)goto done;
 st=cp_dynamics_trace_midpoint(laws,(unsigned)p,last,dt,steps,observed);
 if(st!=CP_OK)goto done;
 double forward_sse=0,cycle_sse=0,reverse_sse=0,peak=0;
 for(size_t i=0;i<steps;i++)for(size_t k=0;k<p;k++){
  double d=forward[(i+1)*p+k]-states.values[(anchor+i+1)*states.stride+k];
  forward_sse+=d*d;
  double cyc=backward[(steps-i-1)*p+k]-forward[(i+1)*p+k];
  cycle_sse+=cyc*cyc;peak=fmax(peak,fabs(cyc));
  double measured=observed[(steps-i)*p+k]-states.values[(anchor+i)*states.stride+k];
  reverse_sse+=measured*measured;
 }
 double base=0;for(size_t k=0;k<p;k++){
  double d=backward[steps*p+k]-first[k];base+=d*d;
  peak=fmax(peak,fabs(d));
 }
 cp_dynamics_audit report={0};
 report.anchor_row=anchor;report.forecast_steps=steps;
 report.forward_rmse=sqrt(forward_sse/(double)(steps*p));
 report.backward_observed_rmse=sqrt(reverse_sse/(double)(steps*p));
 report.cycle_rmse=sqrt((cycle_sse+base)/(double)((steps+1)*p));
 report.cycle_max_absolute_error=peak;
 if(!isfinite(report.forward_rmse)||!isfinite(report.backward_observed_rmse)||
    !isfinite(report.cycle_rmse)||!isfinite(peak))st=CP_NUMERIC;
 else *out=report;
 done:free(dt);free(forward);free(backward);free(observed);return st;
}
