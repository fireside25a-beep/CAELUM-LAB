#include "engine_internal.h"
#include "io.h"
#include "locale_numeric.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define CP_MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#define CP_MKDIR(p) mkdir(p,0700)
#endif
/* An imported candidate row represents a previously measured, authorized
   intervention. Selection receives only its x and cost; outcomes are consumed
   afterwards. This engine never synthesizes production measurements. */
static cp_status path_join(const char *root,const char *name,char out[CP_RUN_PATH]){
 if(!root||!name||!root[0])return CP_INVALID;
 int n=cp_snprintf_c(out,CP_RUN_PATH,"%s/%s",root,name);return n<0||(unsigned)n>=CP_RUN_PATH?CP_RANGE:CP_OK;
}
static void hex(const uint8_t *bytes,size_t n,char *out){static const char h[]="0123456789abcdef";for(size_t i=0;i<n;i++){out[2*i]=h[bytes[i]>>4];out[2*i+1]=h[bytes[i]&15];}out[2*n]=0;}
static cp_status validate_data(const cp_dataset *d,const cp_run_config *c){
 if(!d->x||!d->y||!d->rows||d->features!=c->inputs)return CP_INVALID;
 for(size_t i=0;i<d->rows;i++)for(unsigned j=0;j<=c->inputs;j++){
  double v=j==c->inputs?d->y[i]:d->x[i*c->inputs+j];
  if(!isfinite(v)||v<c->variables[j].lower||v>c->variables[j].upper)return CP_RANGE;
  if(c->family==2&&v!=0&&v!=1)return CP_INVALID;
 }
 return CP_OK;
}
static cp_status freeze_input(const char *input,const char *bundle,const char *name,cp_dataset *out,uint8_t hash[32]){
 uint8_t *bytes=NULL;size_t size=0;cp_status status=cp_read_bytes(input,32u*1024u*1024u,&bytes,&size);
 if(status!=CP_OK)return status;
 cp_dataset d={0};status=cp_dataset_parse_memory(bytes,size,&d);
 char path[CP_RUN_PATH];if(status==CP_OK)status=path_join(bundle,name,path);
 if(status==CP_OK)status=cp_atomic_write(path,bytes,size);
 if(status==CP_OK){cp_sha256(bytes,size,hash);*out=d;}else cp_dataset_release(&d);
 free(bytes);return status;
}
static cp_status frozen_input(const char *bundle,const char *name,const uint8_t expected[32],cp_dataset *out){
 char path[CP_RUN_PATH];cp_status status=path_join(bundle,name,path);if(status!=CP_OK)return status;
 uint8_t hash[32];cp_dataset d={0};status=cp_csv_snapshot(path,&d,hash);
 if(status==CP_OK&&memcmp(expected,hash,32))status=CP_CORRUPT;
 if(status==CP_OK)*out=d;else cp_dataset_release(&d);return status;
}
static cp_status normalize(cp_run_state *s){
 double max=-INFINITY;for(unsigned k=0;k<s->model_count;k++)if(s->active[k])max=fmax(max,s->log_weight[k]);
 if(!isfinite(max)){memset(s->weights,0,sizeof(s->weights));return CP_UNAVAILABLE;}
 double total=0;for(unsigned k=0;k<s->model_count;k++){s->weights[k]=s->active[k]?exp(s->log_weight[k]-max):0;total+=s->weights[k];}
 if(!isfinite(total)||total<=0)return CP_NUMERIC;
 for(unsigned k=0;k<s->model_count;k++)s->weights[k]/=total;
 return CP_OK;
}
static cp_status probability(const cp_binary_scm *m,double x,int intervention,double *p){
 if(x!=0&&x!=1)return CP_INVALID;
 double joint[16];cp_status status=cp_scm_joint(m,intervention?1u:0u,intervention?(uint32_t)x:0u,joint);if(status!=CP_OK)return status;
 double px=0,py=0;for(unsigned i=0;i<4;i++)if((i&1u)==(unsigned)x){px+=joint[i];if(i&2u)py+=joint[i];}
 if(px<=0)return CP_UNAVAILABLE;
 *p=py/px;return CP_OK;
}
static cp_status finite_initial(cp_run_state *s,const cp_dataset *obs){
 s->model_count=s->config.model_count;
 for(unsigned k=0;k<s->model_count;k++){
  s->active[k]=1;s->log_weight[k]=log(s->config.priors[k]);
  double joint[16];cp_status status=cp_scm_joint(s->config.hypotheses+k,0,0,joint);if(status!=CP_OK)return status;
  for(size_t i=0;i<obs->rows;i++){
   unsigned state=(unsigned)obs->x[i]|((unsigned)obs->y[i]<<1);double likelihood=joint[state];
   if(likelihood==0){s->active[k]=0;break;}s->log_weight[k]+=log(likelihood);
  }
 }
 return normalize(s);
}
static cp_status polynomial_fit(cp_run_state *s,const cp_dataset *obs,const cp_dataset *pool){
 size_t n=(size_t)obs->rows+s->summary.steps;unsigned p=s->config.inputs;
 if(n>100000||n>SIZE_MAX/p/sizeof(double))return CP_RANGE;
 double *x=malloc(n*p*sizeof(double)),*y=malloc(n*sizeof(double));if(!x||!y){free(x);free(y);return CP_NO_MEMORY;}
 memcpy(x,obs->x,(size_t)obs->rows*p*sizeof(double));memcpy(y,obs->y,(size_t)obs->rows*sizeof(double));
 for(unsigned i=0;i<s->summary.steps;i++){
  size_t row=(size_t)obs->rows+i;unsigned chosen=s->chosen[i];
  memcpy(x+row*p,pool->x+(size_t)chosen*p,p*sizeof(double));y[row]=pool->y[chosen];
 }
 cp_matrix_view view={x,n,p,p};s->model_count=3;cp_status status=CP_OK;
 for(unsigned k=0;k<3;k++){
  cp_law fitted={0};cp_status fit=cp_law_discover_v2(view,y,k+1,s->config.rng.seed,&fitted);
  if(fit==CP_RANK_DEFICIENT||fit==CP_NUMERIC||fit==CP_INVALID||fit==CP_RANGE){s->active[k]=0;continue;}
  if(fit!=CP_OK){status=fit;break;}
  s->laws[k]=fitted;s->active[k]=1;
  /* BIC weights are a bounded model-scoring approximation, not an exact
     posterior over a fixed parameterized continuous model class. */
  s->log_weight[k]=-.5*fitted.training_bic;
 }
 free(x);free(y);if(status!=CP_OK)return status;
 status=normalize(s);return status==CP_UNAVAILABLE?CP_RANK_DEFICIENT:status;
}
static cp_status predictions(const cp_run_state *s,const double *x,double p[8],double *mean,double *variance){
 double mu=0,var=0;
 for(unsigned k=0;k<s->model_count;k++){
  p[k]=0;if(!s->active[k])continue;
  cp_status status=s->config.family==2?probability(s->config.hypotheses+k,x[0],1,p+k):cp_law_predict(s->laws+k,x,p+k);
  if(status!=CP_OK)return status;
  mu+=s->weights[k]*p[k];
 }
 for(unsigned k=0;k<s->model_count;k++)if(s->active[k]){double d=p[k]-mu;var+=s->weights[k]*d*d;}
 if(!isfinite(mu)||!isfinite(var))return CP_NUMERIC;
 *mean=mu;*variance=var;return CP_OK;
}
/* Deliberately no outcome or holdout pointer in the selection interface. */
static cp_status select_action(const cp_run_state *s,const double *x,size_t rows,const double *costs,unsigned *chosen){
 double best=-INFINITY;unsigned selected=UINT32_MAX;
 for(unsigned i=0;i<rows;i++){
  if(s->used[i]||costs[i]>s->config.max_cost-s->cost)continue;
  double utility=0;
  if(s->config.policy==0){
   cp_random_stream r=s->config.rng;r.stream^=UINT64_C(0x73656c6563740000)+s->summary.steps;
   utility=(double)(cp_random_v2_at(&r,i)>>11)*0x1p-53;
  }else if(s->config.policy==4)utility=-(double)i;
  else{
   double p[8],mean=0,var=0;cp_status status=predictions(s,x+(size_t)i*s->config.inputs,p,&mean,&var);if(status!=CP_OK)return status;
   utility=var;
   if(s->config.policy==2||s->config.policy==3){
    status=cp_finite_eig(s->config.hypotheses,s->weights,s->model_count,0,1,(unsigned)x[i],&utility);if(status!=CP_OK)return status;
    if(s->config.policy==3)utility/=costs[i];
   }
  }
  if(utility>best){best=utility;selected=i;}
 }
 if(selected==UINT32_MAX)return CP_UNAVAILABLE;
 *chosen=selected;return CP_OK;
}
static cp_status append_event(cp_run_state *s,unsigned row,double pred,double var,double outcome){
 size_t remaining=CP_RUN_EVENTS-s->event_bytes;
 int n=cp_snprintf_c(s->events+s->event_bytes,remaining,
  "{\"step\":%u,\"row\":%u,\"prediction\":%.17g,\"model_variance\":%.17g,\"observed\":%.17g,\"cost\":%.17g,\"weights\":[",
  s->summary.steps,row,pred,var,outcome,s->cost);
 if(n<0||(size_t)n>=remaining)return CP_RANGE;
 s->event_bytes+=(unsigned)n;
 for(unsigned k=0;k<s->model_count;k++){
  remaining=CP_RUN_EVENTS-s->event_bytes;n=cp_snprintf_c(s->events+s->event_bytes,remaining,"%s%.17g",k?",":"",s->weights[k]);
  if(n<0||(size_t)n>=remaining)return CP_RANGE;
  s->event_bytes+=(unsigned)n;
 }
 remaining=CP_RUN_EVENTS-s->event_bytes;n=cp_snprintf_c(s->events+s->event_bytes,remaining,"]}\n");
 if(n<0||(size_t)n>=remaining)return CP_RANGE;
 s->event_bytes+=(unsigned)n;s->event_count++;return CP_OK;
}
static cp_status assess(cp_run_state *s,const cp_dataset *holdout,const cp_dataset *calibration){
 s->summary.model_count=s->model_count;s->summary.cost=s->cost;
 s->summary.rejected_models=0;s->summary.predictive_entropy=0;
 for(unsigned k=0;k<s->model_count;k++){
  if(!s->active[k])s->summary.rejected_models++;
  if(s->weights[k]>0)s->summary.predictive_entropy-=s->weights[k]*log(s->weights[k]);
 }
 if(s->summary.phase==CP_RUN_COUNTEREXAMPLE)return CP_OK;
 double radius=0;
 if(s->config.family==1){
  double *residuals=malloc((size_t)calibration->rows*sizeof(double));if(!residuals)return CP_NO_MEMORY;
  cp_status status=CP_OK;
  for(size_t i=0;i<calibration->rows;i++){
   double ps[8],mean=0,var=0;status=predictions(s,calibration->x+i*s->config.inputs,ps,&mean,&var);if(status!=CP_OK)break;
   residuals[i]=fabs(mean-calibration->y[i]);
  }
  if(status==CP_OK)status=cp_conformal_radius(residuals,(size_t)calibration->rows,.05,&radius);
  free(residuals);if(status!=CP_OK)return status;
 }
 s->summary.interval_radius=radius;
 double norm=0;size_t covered=0;
 for(size_t i=0;i<holdout->rows;i++){
  double p[8],mean=0,var=0;cp_status status=predictions(s,holdout->x+i*s->config.inputs,p,&mean,&var);if(status!=CP_OK)return status;
  norm=hypot(norm,(mean-holdout->y[i])/sqrt((double)holdout->rows));
  if(s->config.family==1&&fabs(mean-holdout->y[i])<=radius)covered++;
 }
 s->summary.holdout_rmse=norm;
 s->summary.coverage=s->config.family==1?(double)covered/(double)holdout->rows:0;
 s->summary.causal_status=0;
 if(s->config.family==2){
  double mean=0,effects[8]={0};
  for(unsigned k=0;k<s->model_count;k++){cp_status status=cp_scm_effect(s->config.hypotheses+k,0,1,effects+k);if(status!=CP_OK)return status;mean+=s->weights[k]*effects[k];}
  double lower=1,upper=-1;
  for(unsigned k=0;k<s->model_count;k++)if(s->active[k]){lower=fmin(lower,effects[k]);upper=fmax(upper,effects[k]);}
  s->summary.effect=mean;s->summary.effect_lower=lower;s->summary.effect_upper=upper;
  /* Bounds are the remaining finite model class's support, not a frequentist
     confidence interval and not a proof of observational identification. */
  s->summary.causal_status=1;
 }
 return CP_OK;
}
static cp_status report(const cp_run_state *s,char *text,size_t capacity,size_t *bytes){
 char digest[65];hex(s->digest,32,digest);
 int n=cp_snprintf_c(text,capacity,
  "{\n\"schema\":1,\"phase\":%u,\"steps\":%u,\"models\":%u,\"rejected_models\":%u,\n"
  "\"cost\":%.17g,\"holdout_rmse\":%.17g,\"ensemble_entropy\":%.17g,\"split_conformal_holdout_coverage\":%.17g,\n"
  "\"causal_status\":%u,\"finite_model_effect\":%.17g,\"finite_model_support\":[%.17g,%.17g],\n"
  "\"source\":\"%s\",\"build\":\"%s\",\"checkpoint_sha256\":\"%s\",\n"
  "\"root_seed\":\"%016llx\",\"root_stream\":\"%016llx\",\"derived_seed\":\"%016llx\",\"entropy_source\":%u,\"hardware_attempts\":%u\n}\n",
  s->summary.phase,s->summary.steps,s->model_count,s->summary.rejected_models,s->cost,s->summary.holdout_rmse,s->summary.predictive_entropy,s->summary.coverage,
  s->summary.causal_status,s->summary.effect,s->summary.effect_lower,s->summary.effect_upper,s->source,s->build,digest,
  (unsigned long long)s->config.root_rng.seed,(unsigned long long)s->config.root_rng.stream,(unsigned long long)s->config.rng.seed,s->config.root_rng.source,s->config.root_rng.hardware_attempts);
 if(n<0||(size_t)n>=capacity)return CP_RANGE;
 *bytes=(size_t)n;return CP_OK;
}
static cp_status checkpoint(cp_run_state *s,const char *bundle){
 cp_sha256((const uint8_t*)s,offsetof(cp_run_state,digest),s->digest);
 char a[CP_RUN_PATH],b[CP_RUN_PATH],r[CP_RUN_PATH],text[4096];size_t bytes=0;
 cp_status status=path_join(bundle,"checkpoint.cpc",a);if(status==CP_OK)status=path_join(bundle,"events.jsonl",b);
 if(status==CP_OK)status=path_join(bundle,"result.json",r);
 if(status==CP_OK)status=report(s,text,sizeof(text),&bytes);
 if(status==CP_OK)status=cp_write_pair(a,s,sizeof(*s),b,s->events,s->event_bytes);
 if(status==CP_OK)status=cp_atomic_write(r,text,bytes);
 return status;
}
static cp_status valid_state(const cp_run_state *s){
 const cp_run_config *c=&s->config;
 if(c->schema!=1||s->summary.schema!=1||s->summary.phase>CP_RUN_COUNTEREXAMPLE||c->family<1||c->family>2||c->policy>4||c->model_count>8||c->costs_count>CP_RUN_POOL||!c->interventions||!c->iid||!isfinite(c->max_cost)||c->max_cost<=0||!isfinite(c->noise)||c->noise<=0||!isfinite(s->cost)||s->cost<0||s->cost>c->max_cost||s->summary.cost!=s->cost)return CP_CORRUPT;
 for(unsigned j=0;j<=c->inputs;j++)if(cp_variable_validate(c->variables+j)!=CP_OK)return CP_CORRUPT;
 for(unsigned j=0;j<c->inputs;j++)if(!c->variables[j].controllable)return CP_CORRUPT;
 if(c->family==2){if(c->inputs!=1||c->model_count<2||!c->fully_observed)return CP_CORRUPT;for(unsigned k=0;k<c->model_count;k++)if(cp_scm_validate(c->hypotheses+k)!=CP_OK)return CP_CORRUPT;}
 for(unsigned k=0;k<c->costs_count;k++)if(!isfinite(c->costs[k])||c->costs[k]<=0)return CP_CORRUPT;
 if(!c->costs_count&&(!isfinite(c->cost_per_action)||c->cost_per_action<=0))return CP_CORRUPT;
 double values[]={s->summary.holdout_rmse,s->summary.predictive_entropy,s->summary.effect,s->summary.effect_lower,s->summary.effect_upper,s->summary.coverage,s->summary.interval_radius};
 for(unsigned k=0;k<sizeof(values)/sizeof(values[0]);k++)if(!isfinite(values[k]))return CP_CORRUPT;
 uint8_t used[CP_RUN_POOL]={0};for(unsigned k=0;k<s->summary.steps;k++){
  unsigned row=s->chosen[k];if(row>=CP_RUN_POOL||used[row]||!isfinite(s->predicted[k])||!isfinite(s->observed[k])||!isfinite(s->variance[k])||s->variance[k]<0)return CP_CORRUPT;used[row]=1;
 }
 if(memcmp(used,s->used,sizeof(used)))return CP_CORRUPT;
 return CP_OK;
}
static cp_status load_state(const char *bundle,cp_run_state *s){
 char path[CP_RUN_PATH];cp_status status=path_join(bundle,"checkpoint.cpc",path);if(status!=CP_OK)return status;
 char event_path[CP_RUN_PATH];status=path_join(bundle,"events.jsonl",event_path);if(status!=CP_OK)return status;
 uint8_t *data=NULL,*events=NULL;size_t size=0,event_size=0;
 status=cp_read_pair(path,sizeof(*s),&data,&size,event_path,CP_RUN_EVENTS,&events,&event_size);if(status!=CP_OK)return status;
 if(size!=sizeof(*s)){free(data);free(events);return CP_CORRUPT;}memcpy(s,data,size);free(data);
 if(s->event_bytes>=CP_RUN_EVENTS||event_size!=s->event_bytes||memcmp(events,s->events,event_size)){free(events);return CP_CORRUPT;}free(events);
 uint8_t hash[32];cp_sha256((const uint8_t*)s,offsetof(cp_run_state,digest),hash);
 if(memcmp(s->magic,"CPRUN01",8)||memcmp(hash,s->digest,32)||s->event_bytes>=CP_RUN_EVENTS||s->event_count!=s->summary.steps||s->summary.steps>CP_RUN_ACTIONS||s->model_count>8||!s->model_count||s->config.inputs<1||s->config.inputs>3||s->config.max_actions>CP_RUN_ACTIONS||s->summary.steps>s->config.max_actions)return CP_CORRUPT;
 if(memcmp(s->source,cp_source_identity(),65)||memcmp(s->build,cp_build_identity(),65))return CP_ABI_MISMATCH;
 for(unsigned k=0;k<s->summary.steps;k++)if(s->chosen[k]>=CP_RUN_POOL)return CP_CORRUPT;
 for(unsigned k=0;k<s->model_count;k++)if(!isfinite(s->weights[k])||s->weights[k]<0||s->active[k]>1)return CP_CORRUPT;
 return valid_state(s);
}
static cp_status execute(cp_run_state *s,const char *bundle,unsigned limit,int persist,int replay){
 char protocol_path[CP_RUN_PATH];cp_status status=path_join(bundle,"protocol.json",protocol_path);if(status!=CP_OK)return status;
 uint8_t *protocol=NULL;size_t protocol_size=0;status=cp_read_bytes(protocol_path,262144,&protocol,&protocol_size);if(status!=CP_OK)return status;
 uint8_t protocol_hash[32];cp_sha256(protocol,protocol_size,protocol_hash);free(protocol);if(memcmp(protocol_hash,s->config.protocol_hash,32))return CP_CORRUPT;
 cp_dataset obs={0},pool={0},hold={0},calibration={0};status=frozen_input(bundle,"inputs/observations.csv",s->observations_hash,&obs);
 if(status==CP_OK)status=frozen_input(bundle,"inputs/candidates.csv",s->pool_hash,&pool);
 if(status==CP_OK)status=frozen_input(bundle,"inputs/holdout.csv",s->holdout_hash,&hold);
 if(status==CP_OK&&s->config.family==1)status=frozen_input(bundle,"inputs/calibration.csv",s->calibration_hash,&calibration);
 if(status!=CP_OK)goto done;
 if(s->config.family==1){status=validate_data(&calibration,&s->config);if(status!=CP_OK)goto done;}
 status=validate_data(&obs,&s->config);if(status==CP_OK)status=validate_data(&pool,&s->config);if(status==CP_OK)status=validate_data(&hold,&s->config);if(status!=CP_OK)goto done;
 if(pool.rows>CP_RUN_POOL||(s->config.costs_count&&s->config.costs_count!=pool.rows)){status=CP_RANGE;goto done;}
 for(unsigned k=0;k<s->summary.steps;k++)if(s->chosen[k]>=pool.rows){status=CP_CORRUPT;goto done;}
 double costs[CP_RUN_POOL];for(size_t i=0;i<pool.rows;i++)costs[i]=s->config.costs_count?s->config.costs[i]:s->config.cost_per_action;
 if(s->summary.phase==CP_RUN_READY){
  status=s->config.family==2?finite_initial(s,&obs):polynomial_fit(s,&obs,&pool);
  if(status==CP_UNAVAILABLE){s->summary.phase=CP_RUN_COUNTEREXAMPLE;}else if(status!=CP_OK)goto done;
 }
 if(s->summary.phase!=CP_RUN_COUNTEREXAMPLE&&s->summary.phase!=CP_RUN_SEALED)s->summary.phase=CP_RUN_RUNNING;
 unsigned performed=0;
 while(s->summary.phase==CP_RUN_RUNNING&&s->summary.steps<s->config.max_actions){
  if((limit||replay)&&performed>=limit){s->summary.phase=CP_RUN_STOPPED;break;}
  unsigned row=0;status=select_action(s,pool.x,(size_t)pool.rows,costs,&row);
  if(status==CP_UNAVAILABLE){s->summary.phase=CP_RUN_SEALED;break;}if(status!=CP_OK)goto done;
  double ps[8],prediction=0,var=0;status=predictions(s,pool.x+(size_t)row*s->config.inputs,ps,&prediction,&var);if(status!=CP_OK)goto done;
  double outcome=pool.y[row];unsigned step=s->summary.steps;
  s->used[row]=1;s->chosen[step]=row;s->predicted[step]=prediction;s->observed[step]=outcome;s->variance[step]=var;s->cost+=costs[row];s->summary.steps++;performed++;
  if(s->config.family==2){
   for(unsigned k=0;k<s->model_count;k++)if(s->active[k]){
    double likelihood=outcome==1?ps[k]:1-ps[k];
    if(likelihood==0)s->active[k]=0;else s->log_weight[k]+=log(likelihood);
   }
   status=normalize(s);
  }else status=polynomial_fit(s,&obs,&pool);
  if(status==CP_UNAVAILABLE){s->summary.phase=CP_RUN_COUNTEREXAMPLE;}else if(status!=CP_OK)goto done;
  status=append_event(s,row,prediction,var,outcome);if(status!=CP_OK)goto done;
  if(s->summary.phase==CP_RUN_RUNNING&&s->summary.steps==s->config.max_actions)s->summary.phase=CP_RUN_SEALED;
  status=assess(s,&hold,&calibration);if(status!=CP_OK)goto done;
  if(persist){status=checkpoint(s,bundle);if(status!=CP_OK)goto done;}
 }
 if(s->summary.phase==CP_RUN_RUNNING)s->summary.phase=CP_RUN_SEALED;
 status=assess(s,&hold,&calibration);if(status==CP_OK&&persist)status=checkpoint(s,bundle);
 done:cp_dataset_release(&obs);cp_dataset_release(&pool);cp_dataset_release(&hold);cp_dataset_release(&calibration);return status;
}
cp_status cp_engine_from_state(cp_run_state *s,const char *bundle,unsigned limit){return execute(s,bundle,limit,1,0);}
#ifdef _WIN32
typedef HANDLE run_lock;
#define NO_RUN_LOCK INVALID_HANDLE_VALUE
static run_lock acquire_run(const char *bundle){char p[CP_RUN_PATH];if(path_join(bundle,"authority.lock",p)!=CP_OK)return NO_RUN_LOCK;return CreateFileA(p,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);}
static void release_run(run_lock lock){if(lock!=NO_RUN_LOCK)CloseHandle(lock);}
#else
typedef int run_lock;
#define NO_RUN_LOCK (-1)
static run_lock acquire_run(const char *bundle){
 char p[CP_RUN_PATH];if(path_join(bundle,"authority.lock",p)!=CP_OK)return NO_RUN_LOCK;
 int fd=open(p,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return NO_RUN_LOCK;
 if(flock(fd,LOCK_EX|LOCK_NB)!=0){close(fd);return NO_RUN_LOCK;}return fd;
}
static void release_run(run_lock lock){if(lock!=NO_RUN_LOCK){flock(lock,LOCK_UN);close(lock);}}
#endif
cp_status cp_lab_run(const char *protocol,const char *bundle,unsigned limit,int resume,cp_run_summary *out){
 if(!bundle||!out||(resume!=0&&resume!=1)||(!resume&&!protocol))return CP_INVALID;
 if(!cp_rounding_supported())return CP_NUMERIC;
 cp_run_state *s=calloc(1,sizeof(*s));if(!s)return CP_NO_MEMORY;cp_status status=CP_OK;run_lock lock=NO_RUN_LOCK;
 if(resume){lock=acquire_run(bundle);if(lock==NO_RUN_LOCK){status=CP_UNAVAILABLE;goto done;}status=load_state(bundle,s);if(status!=CP_OK)goto done;}
 else{
  status=cp_protocol_read(protocol,&s->config);if(status!=CP_OK)goto done;
  if(!s->config.interventions||!s->config.iid){status=CP_UNSUPPORTED;goto done;}
  for(unsigned j=0;j<s->config.inputs;j++)if(!s->config.variables[j].controllable){status=CP_UNSUPPORTED;goto done;}
  if(CP_MKDIR(bundle)!=0){status=CP_IO;goto done;}
  lock=acquire_run(bundle);if(lock==NO_RUN_LOCK){status=CP_UNAVAILABLE;goto done;}
  char path[CP_RUN_PATH];status=path_join(bundle,"inputs",path);if(status!=CP_OK)goto done;
  if(CP_MKDIR(path)!=0){status=CP_IO;goto done;}
  cp_dataset d={0};status=freeze_input(s->config.observations,bundle,"inputs/observations.csv",&d,s->observations_hash);if(status==CP_OK)status=validate_data(&d,&s->config);cp_dataset_release(&d);if(status!=CP_OK)goto done;
  status=freeze_input(s->config.pool,bundle,"inputs/candidates.csv",&d,s->pool_hash);if(status==CP_OK)status=validate_data(&d,&s->config);cp_dataset_release(&d);if(status!=CP_OK)goto done;
  status=freeze_input(s->config.holdout,bundle,"inputs/holdout.csv",&d,s->holdout_hash);if(status==CP_OK)status=validate_data(&d,&s->config);cp_dataset_release(&d);if(status!=CP_OK)goto done;
  if(s->config.family==1){status=freeze_input(s->config.calibration,bundle,"inputs/calibration.csv",&d,s->calibration_hash);if(status==CP_OK)status=validate_data(&d,&s->config);cp_dataset_release(&d);if(status!=CP_OK)goto done;}
  uint8_t *bytes=NULL;size_t size=0;status=cp_read_bytes(protocol,262144,&bytes,&size);
  if(status==CP_OK){uint8_t hash[32];cp_sha256(bytes,size,hash);if(memcmp(hash,s->config.protocol_hash,32))status=CP_CORRUPT;}
  if(status==CP_OK)status=path_join(bundle,"protocol.json",path);
  if(status==CP_OK)status=cp_atomic_write(path,bytes,size);
  free(bytes);if(status!=CP_OK)goto done;
  memcpy(s->magic,"CPRUN01",8);memcpy(s->source,cp_source_identity(),65);memcpy(s->build,cp_build_identity(),65);
  s->summary.schema=1;s->summary.phase=CP_RUN_READY;s->summary.seed=s->config.root_rng.seed;
 }
 status=execute(s,bundle,limit,1,0);if(status==CP_OK)*out=s->summary;
 done:release_run(lock);free(s);return status;
}
cp_status cp_lab_run_summary(const char *bundle,cp_run_summary *out){
 if(!bundle||!out)return CP_INVALID;
 cp_run_state *s=calloc(1,sizeof(*s));if(!s)return CP_NO_MEMORY;cp_status status=load_state(bundle,s);
 if(status==CP_OK)*out=s->summary;
 free(s);return status;
}
cp_status cp_lab_run_replay(const char *bundle){
 if(!bundle)return CP_INVALID;
 if(!cp_rounding_supported())return CP_NUMERIC;
 cp_run_state *old=calloc(1,sizeof(*old)),*fresh=calloc(1,sizeof(*fresh));if(!old||!fresh){free(old);free(fresh);return CP_NO_MEMORY;}
 cp_status status=load_state(bundle,old);if(status!=CP_OK)goto done;
 memcpy(fresh->magic,old->magic,8);memcpy(fresh->source,old->source,65);memcpy(fresh->build,old->build,65);fresh->config=old->config;
 fresh->summary.schema=1;fresh->summary.phase=CP_RUN_READY;fresh->summary.seed=old->summary.seed;
 memcpy(fresh->observations_hash,old->observations_hash,32);memcpy(fresh->pool_hash,old->pool_hash,32);memcpy(fresh->holdout_hash,old->holdout_hash,32);memcpy(fresh->calibration_hash,old->calibration_hash,32);
 status=execute(fresh,bundle,(old->summary.phase==CP_RUN_SEALED||old->summary.phase==CP_RUN_COUNTEREXAMPLE)?0:old->summary.steps,0,old->summary.phase!=CP_RUN_SEALED&&old->summary.phase!=CP_RUN_COUNTEREXAMPLE);if(status!=CP_OK)goto done;
 if(fresh->summary.steps!=old->summary.steps||fresh->event_bytes!=old->event_bytes||memcmp(fresh->events,old->events,old->event_bytes)||memcmp(fresh->weights,old->weights,sizeof(old->weights))||memcmp(fresh->laws,old->laws,sizeof(old->laws)))status=CP_CORRUPT;
 done:free(old);free(fresh);return status;
}

cp_status cp_lab_run_point(const char *bundle,unsigned index,cp_run_point *out){
 if(!bundle||!out)return CP_INVALID;
 cp_run_state *s=calloc(1,sizeof(*s));if(!s)return CP_NO_MEMORY;
 cp_status status=load_state(bundle,s);
 if(status==CP_OK&&index>=s->summary.steps)status=CP_RANGE;
 if(status==CP_OK){cp_run_point p={s->chosen[index],s->predicted[index],s->observed[index],s->variance[index]};*out=p;}
 free(s);return status;
}
