#include "caelum_science.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include "bounds.h"
#include "numeric_internal.h"
#include "caelum_lab.h"

/* Stateless SplitMix finalizer; fixed integer semantics, no hidden OS RNG. */
static uint64_t mix64(uint64_t z){
 z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
 return z^(z>>31);
}
cp_status cp_random_init(cp_random_stream *out,const char *source,uint64_t seed,uint64_t stream){
 if(!out||!source)return CP_INVALID;
 cp_random_stream r={0};r.seed=seed;r.stream=stream;
 if(!strcmp(source,"deterministic"))r.source=0;
 else if(!strcmp(source,"recorded"))r.source=1;
 else if(!strcmp(source,"rdseed")||!strcmp(source,"rdrand")){
  r.source=source[2]=='s'?2u:3u;
  cp_status st=cp_native_word(source,&r.seed,&r.hardware_attempts);
  if(st!=CP_OK)return st;
 }else return CP_INVALID;
 *out=r;return CP_OK;
}
uint64_t cp_random_at(const cp_random_stream *r,uint64_t counter){
 if(!r)return 0;
 return mix64(mix64(r->seed ^ UINT64_C(0x243f6a8885a308d3)) ^
   mix64(r->stream ^ UINT64_C(0x13198a2e03707344)) ^
   mix64(counter ^ UINT64_C(0xa4093822299f31d0)));
}
double cp_random_unit(const cp_random_stream *r,uint64_t counter){
 return (double)(cp_random_at(r,counter)>>11)*0x1.0p-53;
}
static int law_valid(const cp_law *m){
 if(!m||m->inputs<1||m->inputs>CP_LAW_INPUT_LIMIT||m->degree<1||m->degree>3||
 m->term_count<1||m->term_count>CP_LAW_TERM_LIMIT||
 !isfinite(m->train_rmse)||m->train_rmse<0||!isfinite(m->holdout_rmse)||m->holdout_rmse<0||
 !isfinite(m->training_bic)||m->selection_steps!=m->term_count||
 m->attempted_candidates<m->term_count-1||m->rank_rejections>m->attempted_candidates)return 0;
 for(unsigned k=0;k<m->term_count;k++){
  if(!isfinite(m->coefficients[k])||!isfinite(m->selection_bic[k]))return 0;
  unsigned d=0;for(unsigned j=0;j<m->inputs;j++)d+=m->powers[k][j];
  if(d>m->degree||(k==0&&d!=0)||(k>0&&d==0))return 0;
  for(unsigned j=m->inputs;j<CP_LAW_INPUT_LIMIT;j++)if(m->powers[k][j]!=0)return 0;
  for(unsigned other=0;other<k;other++)if(memcmp(m->powers[k],m->powers[other],CP_LAW_INPUT_LIMIT)==0)return 0;
 }
 return 1;
}
static double basis(const uint8_t *powers,const double *x,unsigned n){
 double z=1.0;
 for(unsigned j=0;j<n;j++)for(unsigned e=0;e<powers[j];e++)z*=x[j];
 return z;
}
cp_status cp_law_predict(const cp_law *m,const double *x,double *out){
 if(!law_valid(m)||!x||!out)return CP_INVALID;
 for(unsigned j=0;j<m->inputs;j++)if(!isfinite(x[j]))return CP_NUMERIC;
 double result=0;
 for(unsigned k=0;k<m->term_count;k++)result+=m->coefficients[k]*basis(m->powers[k],x,m->inputs);
 if(!isfinite(result))return CP_NUMERIC;
 *out=result;return CP_OK;
}
cp_status cp_law_basis(const cp_law *law,const double *x,double *out){
 if(!law_valid(law)||!x||!out)return CP_INVALID;
 for(unsigned j=0;j<law->inputs;j++)if(!isfinite(x[j]))return CP_NUMERIC;
 double temporary[CP_LAW_TERM_LIMIT]={0};
 for(unsigned k=1;k<law->term_count;k++){
  double z=basis(law->powers[k],x,law->inputs);
  if(!isfinite(z))return CP_NUMERIC;
  temporary[k-1]=z;
 }
 memcpy(out,temporary,(law->term_count-1)*sizeof(double));
 return CP_OK;
}
cp_status cp_law_as_linear(const cp_law *law,cp_linear_model *out){
 if(!law_valid(law)||!out||law->term_count<2)return CP_INVALID;
 cp_linear_model result={0};result.features=law->term_count-1;
 result.fitted_rows=law->train_rows;result.training_rmse=law->train_rmse;
 for(unsigned k=0;k<law->term_count;k++)result.beta[k]=law->coefficients[k];
 *out=result;return CP_OK;
}
/* Enumerate integer multi-indices in a stable degree-lexicographic order. */
static unsigned polynomial_terms(unsigned inputs,unsigned degree,uint8_t out[CP_LAW_TERM_LIMIT][CP_LAW_INPUT_LIMIT]){
 unsigned n=0;
 for(unsigned d=1;d<=degree;d++){
  for(int a=(int)d;a>=0;a--){
   for(int b=(int)(d-(unsigned)a);b>=0;b--){
    unsigned c=d-(unsigned)a-(unsigned)b;
    if(inputs==1 && (b||c))continue;
    if(inputs==2 && c)continue;
    if(n>=CP_LAW_TERM_LIMIT-1)return 0;
    out[n][0]=(uint8_t)a;out[n][1]=(uint8_t)b;out[n][2]=(uint8_t)c;n++;
   }
  }
 }
 return n;
}
/* One candidate least-squares solve, reusing the pre-existing Householder QR.
   A row is either training or held-out; holdout never enters this solver. */
static cp_status candidate_fit(const double *design,const double *y,const uint8_t *train,
 size_t n,unsigned width,const unsigned *term_ids,unsigned chosen,
 double *coef,double *rmse,double *bic,unsigned version){
 if(chosen==0&&version==2){
  double scale=0;size_t count=0;for(size_t i=0;i<n;i++)if(train[i]){scale=fmax(scale,fabs(y[i]));count++;}
  if(count<3)return CP_INVALID;
  if(scale==0)scale=1;
  double sum=0,correction=0;for(size_t i=0;i<n;i++)if(train[i]){double v=y[i]/scale-correction,next=sum+v;correction=(next-sum)-v;sum=next;}
  coef[0]=(sum/(double)count)*scale;
  double norm=0;for(size_t i=0;i<n;i++)if(train[i])norm=hypot(norm,y[i]/scale-coef[0]/scale);
  *rmse=(norm/sqrt((double)count))*scale;
  *bic=(double)count*(2*log(fmax(*rmse,sqrt(DBL_MIN))))+log((double)count);
  return isfinite(*rmse)&&isfinite(*bic)?CP_OK:CP_NUMERIC;
 }
 if(chosen==0){
  double sum=0;size_t cnt=0;
  for(size_t i=0;i<n;i++)if(train[i]){sum+=y[i];cnt++;}
  if(cnt<3)return CP_INVALID;
  coef[0]=sum/(double)cnt;
  double sse=0;for(size_t i=0;i<n;i++)if(train[i]){double e=y[i]-coef[0];sse+=e*e;}
  if(!isfinite(sse))return CP_NUMERIC;
  *rmse=sqrt(sse/(double)cnt);
  *bic=(double)cnt*log(fmax(sse/(double)cnt,DBL_MIN))+log((double)cnt);
  return CP_OK;
 }
 size_t count=0;for(size_t i=0;i<n;i++)if(train[i])count++;
 if(count<=chosen+1)return CP_INVALID;
 if(count>SIZE_MAX/chosen/sizeof(double))return CP_RANGE;
 double *matrix=malloc(count*chosen*sizeof(double));double *target=malloc(count*sizeof(double));
 if(!matrix||!target){free(matrix);free(target);return CP_NO_MEMORY;}
 size_t row=0;
 for(size_t i=0;i<n;i++)if(train[i]){
  for(unsigned j=0;j<chosen;j++)matrix[row*chosen+j]=design[i*width+term_ids[j]];
  target[row++]=y[i];
 }
 cp_linear_model model={0};cp_matrix_view view={matrix,count,chosen,chosen};
 cp_status st=version==1?cp_fit_linear_qr_v1(view,target,&model):cp_fit_linear_qr(view,target,&model);
 if(st==CP_OK&&version==2){
  for(unsigned j=0;j<=chosen;j++)coef[j]=model.beta[j];
  *rmse=model.training_rmse;
  *bic=(double)count*(2*log(fmax(*rmse,sqrt(DBL_MIN))))+(double)(chosen+1)*log((double)count);
  if(!isfinite(*bic))st=CP_NUMERIC;
 }else if(st==CP_OK){
  double sse=0;for(size_t i=0;i<count;i++){
   double pred=model.beta[0];for(unsigned j=0;j<chosen;j++)pred+=model.beta[j+1]*matrix[i*chosen+j];
   double d=target[i]-pred;sse+=d*d;
  }
  if(!isfinite(sse))st=CP_NUMERIC;
  else{
   for(unsigned j=0;j<=chosen;j++)coef[j]=model.beta[j];
   *rmse=sqrt(sse/(double)count);
   *bic=(double)count*log(fmax(sse/(double)count,DBL_MIN))+(double)(chosen+1)*log((double)count);
  }
 }
 free(matrix);free(target);return st;
}
static cp_status discover(cp_matrix_view x,const double *y,unsigned degree,uint64_t seed,cp_law *out,unsigned version){
 if(!out||!x.values||!y||x.columns<1||x.columns>CP_LAW_INPUT_LIMIT||
  degree<1||degree>3||x.rows<30||x.rows>100000||x.stride<x.columns)return CP_INVALID;
 if(!cp_matrix_span_valid(x))return CP_RANGE;
 const size_t n=(size_t)x.rows;
 uint8_t powers[CP_LAW_TERM_LIMIT][CP_LAW_INPUT_LIMIT]={{0}};
 const unsigned nt=polynomial_terms((unsigned)x.columns,degree,powers);
 if(nt==0)return CP_RANGE;
 if(n>SIZE_MAX/nt/sizeof(double))return CP_RANGE;
 double *design=malloc(n*nt*sizeof(double));uint8_t *train=malloc(n);
 if(!design||!train){free(design);free(train);return CP_NO_MEMORY;}
 cp_status st=CP_OK;
 cp_random_stream splitter={0};(void)cp_random_init(&splitter,"deterministic",seed,UINT64_C(0x53434c41424c4142));
 size_t ntrain=0,nheld=0;
 for(size_t i=0;i<n;i++){
  if(!isfinite(y[i])){st=CP_NUMERIC;goto done;}
  for(unsigned j=0;j<x.columns;j++)if(!isfinite(x.values[i*x.stride+j])){st=CP_NUMERIC;goto done;}
  train[i]=(uint8_t)((version==1?cp_random_at(&splitter,(uint64_t)i):cp_random_v2_at(&splitter,(uint64_t)i))%5!=0);
  if(train[i])ntrain++;else nheld++;
  for(unsigned k=0;k<nt;k++){
   double z=basis(powers[k],&x.values[i*x.stride],(unsigned)x.columns);
   if(!isfinite(z)){st=CP_NUMERIC;goto done;}design[i*nt+k]=z;
  }
 }
 if(nheld<5||ntrain<20){st=CP_INVALID;goto done;}
 unsigned selected[CP_LAW_TERM_LIMIT]={0}, chosen=0;
 uint32_t attempted=0,rank_rejections=0,selection_steps=1;
 uint32_t selected_ids[CP_LAW_TERM_LIMIT]={0};
 double selected_bic[CP_LAW_TERM_LIMIT]={0};
 double curcoef[CP_LAW_TERM_LIMIT]={0},cur_rmse=0,cur_bic=0;
 st=candidate_fit(design,y,train,n,nt,selected,0,curcoef,&cur_rmse,&cur_bic,version);
 if(st!=CP_OK)goto done;
 selected_bic[0]=cur_bic;
 for(unsigned step=0;step<nt;step++){
  int best=-1;double bestbic=cur_bic;double bestcoef[CP_LAW_TERM_LIMIT]={0},bestrmse=0;
  for(unsigned k=0;k<nt;k++){
   int used=0;for(unsigned j=0;j<chosen;j++)if(selected[j]==k)used=1;
   if(used)continue;
   unsigned proposed[CP_LAW_TERM_LIMIT];memcpy(proposed,selected,chosen*sizeof(unsigned));proposed[chosen]=k;
   double probe[CP_LAW_TERM_LIMIT]={0},r=0,b=0;
   cp_status s=candidate_fit(design,y,train,n,nt,proposed,chosen+1,probe,&r,&b,version);
   attempted++;
   if(s==CP_RANK_DEFICIENT){rank_rejections++;continue;}
   if(s!=CP_OK){st=s;goto done;}
   if(b<bestbic-1e-8){best=(int)k;bestbic=b;bestrmse=r;memcpy(bestcoef,probe,sizeof(bestcoef));}
  }
  if(best<0)break;
  selected[chosen++]=(unsigned)best;cur_bic=bestbic;cur_rmse=bestrmse;
  selected_ids[selection_steps]=(uint32_t)best;
  selected_bic[selection_steps++]=bestbic;
  memcpy(curcoef,bestcoef,sizeof(curcoef));
 }
 cp_law law={0};law.inputs=(uint32_t)x.columns;law.degree=degree;
 law.term_count=chosen+1;law.train_rows=ntrain;law.holdout_rows=nheld;law.split_seed=seed;
 law.coefficients[0]=curcoef[0];
 law.train_rmse=cur_rmse;law.training_bic=cur_bic;
 law.attempted_candidates=attempted;law.rank_rejections=rank_rejections;
 law.selection_steps=selection_steps;
 memcpy(law.selection_basis_ids,selected_ids,sizeof(selected_ids));
 memcpy(law.selection_bic,selected_bic,sizeof(selected_bic));
 for(unsigned j=0;j<chosen;j++){
  memcpy(law.powers[j+1],powers[selected[j]],sizeof(law.powers[0]));
  law.coefficients[j+1]=curcoef[j+1];
 }
 {
  double held_sse=0;
  for(size_t i=0;i<n;i++)if(!train[i]){
   double p=0;st=cp_law_predict(&law,&x.values[i*x.stride],&p);
   if(st!=CP_OK)goto done;
   if(version==1){double e=y[i]-p;held_sse+=e*e;}
   else held_sse=hypot(held_sse,y[i]/sqrt((double)nheld)-p/sqrt((double)nheld));
  }
  if(!isfinite(held_sse)){st=CP_NUMERIC;goto done;}
  law.holdout_rmse=version==1?sqrt(held_sse/(double)nheld):held_sse;
 }
 *out=law;
 done:free(design);free(train);return st;
}
cp_status cp_law_discover(cp_matrix_view x,const double *y,unsigned degree,uint64_t seed,cp_law *out){return discover(x,y,degree,seed,out,1);}
cp_status cp_law_discover_v2(cp_matrix_view x,const double *y,unsigned degree,uint64_t seed,cp_law *out){return discover(x,y,degree,seed,out,2);}
cp_status cp_design_disagreement(const cp_law *a,const cp_law *b,
 const cp_intervention_design *g,cp_intervention_choice *out){
 if(!law_valid(a)||!law_valid(b)||!g||!out||a->inputs!=b->inputs||
 g->inputs!=a->inputs||g->grid_points<2||g->grid_points>21)return CP_INVALID;
 for(unsigned j=0;j<g->inputs;j++)if(!isfinite(g->lower[j])||
 !isfinite(g->upper[j])||g->upper[j]<=g->lower[j])return CP_INVALID;
 cp_intervention_choice best={0};double x[CP_LAW_INPUT_LIMIT]={0};
 unsigned res=g->grid_points;uint64_t points=1;
 for(unsigned j=0;j<g->inputs;j++)points*=res;
 for(uint64_t i=0;i<points;i++){
  uint64_t v=i;
  for(unsigned j=0;j<g->inputs;j++){
   uint64_t ix=v%res;v/=res;
   double fraction=(double)ix/(double)(res-1);
   x[j]=(1-fraction)*g->lower[j]+fraction*g->upper[j];
  }
  double pa=0,pb=0;cp_status st=cp_law_predict(a,x,&pa);
  if(st!=CP_OK)return st;
  st=cp_law_predict(b,x,&pb);if(st!=CP_OK)return st;
  double delta=fabs(pa-pb);if(!isfinite(delta))return CP_NUMERIC;
  if(i==0||delta>best.disagreement){
   memcpy(best.x,x,sizeof(x));best.prediction_a=pa;best.prediction_b=pb;
   best.disagreement=delta;
  }
 }
 if(best.disagreement==0)return CP_UNAVAILABLE;
 best.enumerated=points;*out=best;return CP_OK;
}
cp_status cp_randomized_effect(const uint8_t *treatment,const double *outcomes,
 uint64_t n,int randomized_assignment,cp_effect_estimate *out){
 if(!treatment||!out||!outcomes||n<4||n>100000000||randomized_assignment!=1)return CP_INVALID;
 double means[2]={0},m2[2]={0},scale[2]={0};uint64_t ns[2]={0};
 for(uint64_t i=0;i<n;i++){
  unsigned t=treatment[i];if(t>1)return CP_INVALID;
  if(!isfinite(outcomes[i]))return CP_NUMERIC;
  scale[t]=fmax(scale[t],fabs(outcomes[i]));
 }
 for(unsigned t=0;t<2;t++)if(scale[t]==0)scale[t]=1;
 for(uint64_t i=0;i<n;i++){
  unsigned t=treatment[i];double value=outcomes[i]/scale[t];
  ns[t]++;double d=value-means[t];means[t]+=d/(double)ns[t];m2[t]+=d*(value-means[t]);
 }
 if(ns[0]<2||ns[1]<2)return CP_INVALID;
 double se0=sqrt(fmax(0,m2[0])/(double)(ns[0]-1)/(double)ns[0])*scale[0];
 double se1=sqrt(fmax(0,m2[1])/(double)(ns[1]-1)/(double)ns[1])*scale[1];
 double se=hypot(se0,se1);means[0]*=scale[0];means[1]*=scale[1];
 cp_effect_estimate a={0};a.control_n=ns[0];a.treated_n=ns[1];
 a.control_mean=means[0];a.treated_mean=means[1];a.difference=means[1]-means[0];
 a.standard_error=se;a.ci95_lower=a.difference-1.95996398454005*se;
 a.ci95_upper=a.difference+1.95996398454005*se;
 if(!isfinite(a.ci95_lower)||!isfinite(a.ci95_upper))return CP_NUMERIC;
 *out=a;return CP_OK;
}
/* Multiple-comparison correction: never sort caller input or output in place.
   O(n^2) stable ordering deliberately keeps the implementation bounded and
   independently auditable; n<=10000. */
cp_status cp_holm_adjust(const double *p,size_t n,double *adjusted){
 if(!p||!adjusted||!n||n>10000)return CP_INVALID;
 if(cp_double_buffers_overlap(p,adjusted,n))return CP_INVALID;
 for(size_t i=0;i<n;i++)if(!isfinite(p[i])||p[i]<0||p[i]>1)return CP_INVALID;
 uint8_t *used=calloc(n,1);if(!used)return CP_NO_MEMORY;
 double prev=0;
 for(size_t rank=0;rank<n;rank++){
  size_t best=n;
  for(size_t i=0;i<n;i++)if(!used[i]&&(best==n||p[i]<p[best]))best=i;
  used[best]=1;
  double current=fmin(1.0,p[best]*(double)(n-rank));
  if(current<prev)current=prev;
  adjusted[best]=current;prev=current;
 }
 free(used);return CP_OK;
}
/* Quadratic-interpolation derivative for arbitrary positive adjacent steps.
   Its first-order terms cancel even on irregular sampling grids. */
static cp_status central_derivative(cp_matrix_view states,const double *times,
 size_t t,size_t state,double *result){
 double h1=times[t]-times[t-1],h2=times[t+1]-times[t];
 if(!isfinite(h1)||!isfinite(h2)||h1<=0||h2<=0)return CP_NUMERIC;
 double prev=states.values[(t-1)*states.stride+state];
 double curr=states.values[t*states.stride+state];
 double next=states.values[(t+1)*states.stride+state];
 double d1=(curr-prev)/h1,d2=(next-curr)/h2;
 double ratio=h1>h2?h2/h1:h1/h2;
 double weight=h1>h2?ratio/(1+ratio):1/(1+ratio);
 double derivative=weight*d1+(1-weight)*d2;
 if(!isfinite(derivative))return CP_NUMERIC;
 *result=derivative;return CP_OK;
}
cp_status cp_dynamics_discover(cp_matrix_view states,const double *times,
 unsigned degree,uint64_t seed,cp_law out_laws[CP_LAW_INPUT_LIMIT]){
 if(!times||!out_laws||!states.values||states.columns<1||
 states.columns>CP_LAW_INPUT_LIMIT||states.rows<80||states.rows>100000||
 states.stride<states.columns)return CP_INVALID;
 if(!cp_matrix_span_valid(states))return CP_RANGE;
 size_t n=(size_t)states.rows,p=(size_t)states.columns;
 for(size_t i=0;i<n;i++){
  if(!isfinite(times[i])||(i>0&&times[i]<=times[i-1]))return CP_INVALID;
  for(size_t j=0;j<p;j++)if(!isfinite(states.values[i*states.stride+j]))return CP_NUMERIC;
 }
 /* Train on early contiguous period, test the final 30% by forecasted derivative.
    There is no temporal leakage from the final block into selection. */
 /* The final training derivative at t=split-2 reads only states up to
    split-1. All states at indices >=split remain strictly out of training.
    A future derivative at t=split+1 uses states from split onward. */
 const size_t split=(n*7)/10,early=split-2,held=n-split-2;
 if(early<35||held<10)return CP_INVALID;
 if(early>SIZE_MAX/p/sizeof(double))return CP_RANGE;
 double *x=malloc(early*p*sizeof(double));double *y=malloc(early*sizeof(double));
 if(!x||!y){free(x);free(y);return CP_NO_MEMORY;}
 cp_status st=CP_OK;cp_law learned[CP_LAW_INPUT_LIMIT]={{0}};
 for(size_t k=0;k<p;k++){
  for(size_t i=0;i<early;i++){
   size_t t=i+1;
   if(k==0)for(size_t j=0;j<p;j++)x[i*p+j]=states.values[t*states.stride+j];
   st=central_derivative(states,times,t,k,&y[i]);
   if(st!=CP_OK)goto done_dyn;
  }
  cp_matrix_view view={x,early,p,p};
  st=cp_law_discover_v2(view,y,degree,seed^(UINT64_C(0x9e3779b97f4a7c15)*(uint64_t)(k+1)),&learned[k]);
  if(st!=CP_OK)goto done_dyn;
  double sse=0;
  for(size_t i=0;i<held;i++){
   size_t t=split+1+i;double prediction=0;
   st=cp_law_predict(&learned[k],&states.values[t*states.stride],&prediction);
   if(st!=CP_OK)goto done_dyn;
   double observed=0;
   st=central_derivative(states,times,t,k,&observed);
   if(st!=CP_OK)goto done_dyn;
   double err=prediction-observed;sse+=err*err;
  }
  if(!isfinite(sse)){st=CP_NUMERIC;goto done_dyn;}
  learned[k].holdout_rmse=sqrt(sse/(double)held);
  learned[k].holdout_rows=held;
 }
 for(size_t k=0;k<p;k++)out_laws[k]=learned[k];
 done_dyn:free(x);free(y);return st;
}
