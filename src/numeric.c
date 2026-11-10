#include "caelum_lab.h"
#include "numeric_internal.h"
#include "locale_numeric.h"
#include "bounds.h"
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
static double compensated_mean(const double *a,size_t n,size_t stride,double scale){
 double sum=0,correction=0;
 for(size_t i=0;i<n;i++){double value=a[i*stride]/scale-correction,next=sum+value;correction=(next-sum)-value;sum=next;}
 return sum/(double)n;
}
cp_status cp_fit_linear_checked(cp_matrix_view x,const double *y,cp_linear_model *out,cp_fit_diagnostics *diag){
 if(!out||!x.values||!y||x.columns<1||x.columns>CP_MAX_FEATURES||x.rows<x.columns+1||x.rows>1000000||x.stride<x.columns)return CP_INVALID;
 if(!cp_matrix_span_valid(x))return CP_RANGE;
 if(!cp_rounding_supported())return CP_NUMERIC;
 size_t n=(size_t)x.rows,p=(size_t)x.columns;
 double scale[CP_MAX_FEATURES]={0},mean[CP_MAX_FEATURES]={0},response=0;
 for(size_t i=0;i<n;i++){
  if(!isfinite(y[i]))return CP_NUMERIC;
  response=fmax(response,fabs(y[i]));
  for(size_t j=0;j<p;j++){
   double value=x.values[i*x.stride+j];if(!isfinite(value))return CP_NUMERIC;
   scale[j]=fmax(scale[j],fabs(value));
  }
 }
 if(response==0)response=1;
 for(size_t j=0;j<p;j++){
  if(scale[j]==0)return CP_RANK_DEFICIENT;
  mean[j]=compensated_mean(x.values+j,n,(size_t)x.stride,scale[j]);
 }
 if(n>SIZE_MAX/p/sizeof(double))return CP_RANGE;
 double *normalized=malloc(n*p*sizeof(double)),*target=malloc(n*sizeof(double));
 if(!normalized||!target){free(normalized);free(target);return CP_NO_MEMORY;}
 for(size_t i=0;i<n;i++){
  target[i]=y[i]/response;
  for(size_t j=0;j<p;j++)normalized[i*p+j]=x.values[i*x.stride+j]/scale[j]-mean[j];
 }
 cp_linear_model fitted={0};cp_matrix_view view={normalized,n,p,p};
 cp_status status=cp_fit_linear_qr_v1(view,target,&fitted);
 free(normalized);free(target);if(status!=CP_OK)return status;
 cp_linear_model result={0};result.features=x.columns;result.fitted_rows=x.rows;
 long double intercept=(long double)fitted.beta[0]*response;
 for(size_t j=0;j<p;j++){
  long double coefficient=(long double)fitted.beta[j+1]*response/scale[j];
  if(fabsl(coefficient)>DBL_MAX)return CP_NUMERIC;
  result.beta[j+1]=(double)coefficient;intercept-=(long double)fitted.beta[j+1]*response*mean[j];
 }
 if(fabsl(intercept)>DBL_MAX)return CP_NUMERIC;
 result.beta[0]=(double)intercept;
 double residual=0,ynorm=0;
 for(size_t i=0;i<n;i++){
  long double pred=result.beta[0];for(size_t j=0;j<p;j++)pred+=(long double)result.beta[j+1]*x.values[i*x.stride+j];
  long double difference=pred-y[i];if(fabsl(difference/response)>DBL_MAX)return CP_NUMERIC;
  residual=hypot(residual,(double)(difference/response));ynorm=hypot(ynorm,y[i]/response);
 }
 result.training_rmse=(residual/sqrt((double)n))*response;if(!isfinite(result.training_rmse))return CP_NUMERIC;
 cp_fit_diagnostics d={0};d.response_scale=response;d.relative_residual=residual/fmax(DBL_MIN,ynorm);d.algorithm_version=2;d.iterations=1;
 for(size_t j=0;j<p;j++){d.feature_scale[j]=scale[j];d.coefficient_norm=hypot(d.coefficient_norm,result.beta[j+1]);}
 *out=result;if(diag)*diag=d;return CP_OK;
}
cp_status cp_fit_linear_qr(cp_matrix_view x,const double *y,cp_linear_model *out){return cp_fit_linear_checked(x,y,out,NULL);}

/* Householder least squares on normalized coordinates. No normal matrix is
   formed. Rank decisions use the largest normalized column norm. */
static cp_status dense_qr(double *a,double *b,size_t n,size_t p,double *beta){
 double reference=0;
 for(size_t j=0;j<p;j++){double norm=0;for(size_t i=0;i<n;i++)norm=hypot(norm,a[i*p+j]);reference=fmax(reference,norm);}
 if(!isfinite(reference)||reference==0)return CP_RANK_DEFICIENT;
 double *v=malloc(n*sizeof(double));if(!v)return CP_NO_MEMORY;
 cp_status status=CP_OK;
 for(size_t k=0;k<p;k++){
  double norm=0;for(size_t i=k;i<n;i++)norm=hypot(norm,a[i*p+k]);
  if(!isfinite(norm)){status=CP_NUMERIC;break;}
  if(norm<=64*DBL_EPSILON*reference){status=CP_RANK_DEFICIENT;break;}
  double alpha=-copysign(norm,a[k*p+k]);
  /* Divide before subtracting to avoid overflow of the reflector head. */
  v[k]=a[k*p+k]/norm-alpha/norm;
  double vn=fabs(v[k]);for(size_t i=k+1;i<n;i++){v[i]=a[i*p+k]/norm;vn=hypot(vn,v[i]);}
  for(size_t i=k;i<n;i++)v[i]/=vn;
  for(size_t j=k;j<p;j++){
   double dot=0;for(size_t i=k;i<n;i++)dot+=v[i]*a[i*p+j];
   for(size_t i=k;i<n;i++)a[i*p+j]-=2*dot*v[i];
  }
  double dot=0;for(size_t i=k;i<n;i++)dot+=v[i]*b[i];
  for(size_t i=k;i<n;i++)b[i]-=2*dot*v[i];
  a[k*p+k]=alpha;
 }
 if(status==CP_OK)for(size_t k=p;k-->0;){
  double value=b[k];for(size_t j=k+1;j<p;j++)value-=a[k*p+j]*beta[j];
  beta[k]=value/a[k*p+k];if(!isfinite(beta[k])){status=CP_NUMERIC;break;}
 }
 free(v);return status;
}
static cp_status scales(cp_matrix_view x,const double *y,double scale[16],double mean[16],double *response,double *ym){
 *response=0;
 for(size_t i=0;i<x.rows;i++){
  if(!isfinite(y[i]))return CP_NUMERIC;
  *response=fmax(*response,fabs(y[i]));
  for(size_t j=0;j<x.columns;j++){double v=x.values[i*x.stride+j];if(!isfinite(v))return CP_NUMERIC;scale[j]=fmax(scale[j],fabs(v));}
 }
 if(*response==0)*response=1;
 *ym=compensated_mean(y,(size_t)x.rows,1,*response);
 for(size_t j=0;j<x.columns;j++){if(scale[j]==0)scale[j]=1;mean[j]=compensated_mean(x.values+j,(size_t)x.rows,(size_t)x.stride,scale[j]);}
 return CP_OK;
}
static cp_status restore_model(cp_matrix_view x,const double *y,const double *beta,const double *scale,const double *mean,double response,cp_linear_model *out){
 cp_linear_model m={0};m.features=x.columns;m.fitted_rows=x.rows;
 long double intercept=(long double)beta[0]*response;
 for(size_t j=0;j<x.columns;j++){
  long double v=(long double)beta[j+1]*response/scale[j];if(fabsl(v)>DBL_MAX)return CP_NUMERIC;
  m.beta[j+1]=(double)v;intercept-=(long double)beta[j+1]*response*mean[j];
 }
 if(fabsl(intercept)>DBL_MAX)return CP_NUMERIC;
 m.beta[0]=(double)intercept;double norm=0;
 for(size_t i=0;i<x.rows;i++){
  long double pred=m.beta[0];for(size_t j=0;j<x.columns;j++)pred+=(long double)m.beta[j+1]*x.values[i*x.stride+j];
  long double e=(pred-y[i])/response;if(fabsl(e)>DBL_MAX)return CP_NUMERIC;
  norm=hypot(norm,(double)e);
 }
 m.training_rmse=(norm/sqrt((double)x.rows))*response;if(!isfinite(m.training_rmse))return CP_NUMERIC;
 *out=m;return CP_OK;
}
/* Ridge minimizes sum(residual^2)+lambda*sum(original-unit slope^2).
   The intercept is unpenalized. Augmented QR preserves this objective even
   when features have different units and scales. */
cp_status cp_fit_ridge(cp_matrix_view x,const double *y,double lambda,cp_linear_model *out){
 if(!out||!x.values||!y||x.columns<1||x.columns>CP_MAX_FEATURES||x.rows<x.columns+1||x.rows>100000||x.stride<x.columns||!isfinite(lambda)||lambda<0)return CP_INVALID;
 if(!cp_matrix_span_valid(x))return CP_RANGE;
 if(!cp_rounding_supported())return CP_NUMERIC;
 if(lambda==0)return cp_fit_linear_qr(x,y,out);
 size_t n=(size_t)x.rows,p=(size_t)x.columns,total=n+p;
 double scale[16]={0},mean[16]={0},response=0,ym=0;
 cp_status status=scales(x,y,scale,mean,&response,&ym);if(status!=CP_OK)return status;
 double *a=calloc(total*p,sizeof(double)),*b=calloc(total,sizeof(double));if(!a||!b){free(a);free(b);return CP_NO_MEMORY;}
 for(size_t i=0;i<n;i++){b[i]=y[i]/response-ym;for(size_t j=0;j<p;j++)a[i*p+j]=x.values[i*x.stride+j]/scale[j]-mean[j];}
 double column[16]={0};
 for(size_t j=0;j<p;j++){
  double penalty=sqrt(lambda)/scale[j];
  if(!isfinite(penalty)){status=CP_NUMERIC;goto done;}
  column[j]=fmax(1,penalty);a[(n+j)*p+j]=penalty;
  for(size_t i=0;i<total;i++)a[i*p+j]/=column[j];
 }
 double fit[17]={0};status=dense_qr(a,b,total,p,fit+1);if(status!=CP_OK)goto done;
 fit[0]=ym;for(size_t j=0;j<p;j++)fit[j+1]/=column[j];
 status=restore_model(x,y,fit,scale,mean,response,out);
 done:free(a);free(b);return status;
}
/* Huber IRLS uses a weighted QR solve in normalized coordinates on every
   iteration. Nonconvergence is reported and leaves caller outputs unchanged. */
cp_status cp_fit_huber(cp_matrix_view x,const double *y,double delta,unsigned iterations,cp_linear_model *out,cp_fit_diagnostics *diag){
 if(!out||!y||!x.values||x.columns<1||x.columns>CP_MAX_FEATURES||x.rows<x.columns+1||x.rows>100000||x.stride<x.columns||!isfinite(delta)||delta<=0||iterations<1||iterations>1000)return CP_INVALID;
 if(!cp_matrix_span_valid(x))return CP_RANGE;
 if(!cp_rounding_supported())return CP_NUMERIC;
 size_t n=(size_t)x.rows,p=(size_t)x.columns,q=p+1;
 double scale[16]={0},mean[16]={0},response=0,ym=0;
 cp_status status=scales(x,y,scale,mean,&response,&ym);if(status!=CP_OK)return status;
 double *a=malloc(n*q*sizeof(double)),*b=malloc(n*sizeof(double));if(!a||!b){free(a);free(b);return CP_NO_MEMORY;}
 double beta[17]={0};unsigned step=0;int converged=0;
 for(step=0;step<iterations;step++){
  for(size_t i=0;i<n;i++){
   double pred=beta[0];for(size_t j=0;j<p;j++)pred+=beta[j+1]*(x.values[i*x.stride+j]/scale[j]-mean[j]);
   double error=fabs(y[i]/response-pred),weight=1;
   if(step&&error>0){long double ratio=(long double)delta/response/error;if(ratio<1)weight=(double)sqrtl(ratio);}
   a[i*q]=weight;b[i]=(y[i]/response)*weight;
   for(size_t j=0;j<p;j++)a[i*q+j+1]=(x.values[i*x.stride+j]/scale[j]-mean[j])*weight;
  }
  double next[17]={0};status=dense_qr(a,b,n,q,next);if(status!=CP_OK)goto done;
  double change=0;for(size_t j=0;j<q;j++)change=fmax(change,fabs(next[j]-beta[j])/fmax(1,fabs(beta[j])));
  memcpy(beta,next,q*sizeof(double));if(step&&change<1e-10){converged=1;break;}
 }
 if(!converged){status=CP_UNAVAILABLE;goto done;}
 status=restore_model(x,y,beta,scale,mean,response,out);
 if(status==CP_OK&&diag){cp_fit_diagnostics d={0};d.algorithm_version=2;d.iterations=step+1;d.response_scale=response;for(size_t j=0;j<p;j++)d.feature_scale[j]=scale[j];*diag=d;}
 done:free(a);free(b);return status;
}
