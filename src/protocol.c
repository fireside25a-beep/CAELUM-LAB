#include "engine_internal.h"
#include "json.h"
#include "io.h"
#include "locale_numeric.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
static int hex_word(const char *s,uint64_t *out){
 size_t n=strlen(s);if(!n||n>16)return 0;
 for(size_t i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return 0;
 char *end=NULL;errno=0;unsigned long long v=strtoull(s,&end,16);if(errno||!end||*end)return 0;*out=(uint64_t)v;return 1;
}
static cp_status get_number(const cp_json *j,unsigned object,const char *key,double *out){int token=cp_json_get(j,object,key);return token<0?CP_INVALID:cp_json_number(j,(unsigned)token,out);}
static cp_status get_unsigned(const cp_json *j,unsigned object,const char *key,unsigned max,uint32_t *out){double v=0;cp_status s=get_number(j,object,key,&v);if(s!=CP_OK)return s;if(v<0||v>max||floor(v)!=v)return CP_RANGE;*out=(uint32_t)v;return CP_OK;}
static cp_status get_string(const cp_json *j,unsigned object,const char *key,char *out,size_t size){int token=cp_json_get(j,object,key);return token<0?CP_INVALID:cp_json_string(j,(unsigned)token,out,size);}
static cp_status get_bool(const cp_json *j,unsigned object,const char *key,uint32_t *out){int token=cp_json_get(j,object,key),v=0;if(token<0)return CP_INVALID;cp_status s=cp_json_boolean(j,(unsigned)token,&v);if(s==CP_OK)*out=(uint32_t)v;return s;}
static cp_status resolved_path(const char *protocol,const char *input,char out[CP_RUN_PATH]){
 if(!input[0])return CP_INVALID;
#ifdef _WIN32
 int absolute=input[0]=='/'||input[0]=='\\'||(strlen(input)>2&&input[1]==':');
#else
 int absolute=input[0]=='/';
#endif
 if(absolute){if(strlen(input)>=CP_RUN_PATH)return CP_RANGE;memcpy(out,input,strlen(input)+1);return CP_OK;}
 const char *slash=strrchr(protocol,'/');
#ifdef _WIN32
 const char *back=strrchr(protocol,'\\');if(back&&(!slash||back>slash))slash=back;
#endif
 size_t prefix=slash?(size_t)(slash-protocol)+1:0,length=strlen(input);if(prefix+length>=CP_RUN_PATH)return CP_RANGE;
 if(prefix)memcpy(out,protocol,prefix);
 memcpy(out+prefix,input,length+1);return CP_OK;
}
static cp_status read_variables(const cp_json *j,unsigned array,cp_run_config *c){
 if(j->tokens[array].type!='['||j->tokens[array].children<2||j->tokens[array].children>4)return CP_INVALID;
 c->inputs=j->tokens[array].children-1;
 for(unsigned i=0;i<=c->inputs;i++){
  int t=cp_json_at(j,array,i);if(t<0)return CP_CORRUPT;unsigned obj=(unsigned)t;
  const char *keys[]={"name","unit","lower","upper","role","controllable"};
  cp_status s=cp_json_keys(j,obj,keys,6);if(s!=CP_OK)return s;
  cp_variable *v=c->variables+i;char role[32];
  if(get_string(j,obj,"name",v->name,sizeof(v->name))!=CP_OK||get_string(j,obj,"role",role,sizeof(role))!=CP_OK||
     get_number(j,obj,"lower",&v->lower)!=CP_OK||get_number(j,obj,"upper",&v->upper)!=CP_OK||get_bool(j,obj,"controllable",&v->controllable)!=CP_OK)return CP_INVALID;
  if(!strcmp(role,"input"))v->role=CP_ROLE_INPUT;else if(!strcmp(role,"response"))v->role=CP_ROLE_RESPONSE;else if(!strcmp(role,"treatment"))v->role=CP_ROLE_TREATMENT;else return CP_UNSUPPORTED;
  if((i==c->inputs&&v->role!=CP_ROLE_RESPONSE)||(i<c->inputs&&v->role==CP_ROLE_RESPONSE))return CP_INVALID;
  for(unsigned k=0;k<i;k++)if(!strcmp(v->name,c->variables[k].name))return CP_INVALID;
  int u=cp_json_get(j,obj,"unit");if(u<0||j->tokens[u].type!='['||j->tokens[u].children!=7)return CP_INVALID;
  for(unsigned axis=0;axis<7;axis++){int token=cp_json_at(j,(unsigned)u,axis);double power=0;if(token<0||cp_json_number(j,(unsigned)token,&power)!=CP_OK||floor(power)!=power||power<-32||power>32)return CP_INVALID;v->unit.exponent[axis]=(int8_t)power;}
  s=cp_variable_validate(v);if(s!=CP_OK)return s;
 }
 return CP_OK;
}
static cp_status read_hypotheses(const cp_json *j,unsigned array,cp_run_config *c){
 if(j->tokens[array].type!='['||j->tokens[array].children<2||j->tokens[array].children>8||c->inputs!=1)return CP_INVALID;
 c->model_count=j->tokens[array].children;
 for(unsigned k=0;k<c->model_count;k++){
  int h=cp_json_at(j,array,k);if(h<0)return CP_CORRUPT;unsigned obj=(unsigned)h;
  const char *keys[]={"parents","probability_one","prior"};cp_status status=cp_json_keys(j,obj,keys,3);if(status!=CP_OK)return status;
  cp_binary_scm *m=c->hypotheses+k;m->variables=2;
  int parents=cp_json_get(j,obj,"parents"),probs=cp_json_get(j,obj,"probability_one");
  if(parents<0||probs<0||j->tokens[parents].type!='['||j->tokens[parents].children!=2||j->tokens[probs].type!='['||j->tokens[probs].children!=2)return CP_INVALID;
  if(get_number(j,obj,"prior",c->priors+k)!=CP_OK||c->priors[k]<=0)return CP_INVALID;
  for(unsigned v=0;v<2;v++){
   double mask=0;int t=cp_json_at(j,(unsigned)parents,v);if(t<0||cp_json_number(j,(unsigned)t,&mask)!=CP_OK||mask<0||mask>3||floor(mask)!=mask)return CP_INVALID;m->parent_mask[v]=(uint32_t)mask;
   unsigned count=0;for(unsigned b=0;b<2;b++)if(m->parent_mask[v]&(1u<<b))count++;
   int table=cp_json_at(j,(unsigned)probs,v);if(table<0||j->tokens[table].type!='['||j->tokens[table].children!=(1u<<count))return CP_INVALID;
   for(unsigned state=0;state<4;state++){
    unsigned compact=0,bit=0;for(unsigned b=0;b<2;b++)if(m->parent_mask[v]&(1u<<b))compact|=((state>>b)&1u)<<bit++;
    t=cp_json_at(j,(unsigned)table,compact);if(t<0||cp_json_number(j,(unsigned)t,&m->probability_one[v][state])!=CP_OK)return CP_INVALID;
   }
  }
  status=cp_scm_validate(m);if(status!=CP_OK)return status;
 }
 return CP_OK;
}
cp_status cp_protocol_read(const char *path,cp_run_config *out){
 if(!path||!out)return CP_INVALID;
 uint8_t *bytes=NULL;size_t size=0;cp_status status=cp_read_bytes(path,262144,&bytes,&size);if(status!=CP_OK)return status;
 cp_json *j=malloc(sizeof(*j));if(!j){free(bytes);return CP_NO_MEMORY;}
 status=cp_json_parse(j,(const char*)bytes,size);if(status!=CP_OK)goto done;
 const char *keys[]={"schema_version","experiment_kind","family","policy","observations","candidates","holdout","variables","entropy","assumptions","budget","measurement_noise","cost_per_action","costs","hypotheses","calibration"};
 status=cp_json_keys(j,0,keys,16);if(status!=CP_OK)goto done;
 cp_run_config c;memset(&c,0,sizeof(c));char value[CP_RUN_PATH];
 status=get_unsigned(j,0,"schema_version",1,&c.schema);if(status!=CP_OK||c.schema!=1){status=CP_ABI_MISMATCH;goto done;}
 if(get_string(j,0,"experiment_kind",value,sizeof(value))!=CP_OK||strcmp(value,"caelum.discovery.v1")){status=CP_UNSUPPORTED;goto done;}
 if(get_string(j,0,"family",value,sizeof(value))!=CP_OK){status=CP_INVALID;goto done;}
 if(!strcmp(value,"polynomial"))c.family=1;else if(!strcmp(value,"finite_scm"))c.family=2;else{status=CP_UNSUPPORTED;goto done;}
 if(get_string(j,0,"policy",value,sizeof(value))!=CP_OK){status=CP_INVALID;goto done;}
 if(!strcmp(value,"random"))c.policy=0;else if(!strcmp(value,"disagreement"))c.policy=1;else if(!strcmp(value,"eig"))c.policy=2;else if(!strcmp(value,"cost_eig"))c.policy=3;else if(!strcmp(value,"round_robin"))c.policy=4;else{status=CP_UNSUPPORTED;goto done;}
 if(c.family==1&&c.policy>1&&c.policy!=4){status=CP_UNSUPPORTED;goto done;}
 const char *file_keys[]={"observations","candidates","holdout"};char *destinations[]={c.observations,c.pool,c.holdout};
 for(unsigned k=0;k<3;k++){status=get_string(j,0,file_keys[k],value,sizeof(value));if(status!=CP_OK)goto done;status=resolved_path(path,value,destinations[k]);if(status!=CP_OK)goto done;}
 if(c.family==1){status=get_string(j,0,"calibration",value,sizeof(value));if(status!=CP_OK)goto done;status=resolved_path(path,value,c.calibration);if(status!=CP_OK)goto done;}
 else if(cp_json_get(j,0,"calibration")>=0){status=CP_UNSUPPORTED;goto done;}
 int token=cp_json_get(j,0,"variables");if(token<0){status=CP_INVALID;goto done;}status=read_variables(j,(unsigned)token,&c);if(status!=CP_OK)goto done;
 int entropy=cp_json_get(j,0,"entropy"),assumptions=cp_json_get(j,0,"assumptions"),budget=cp_json_get(j,0,"budget");
 if(entropy<0||assumptions<0||budget<0){status=CP_INVALID;goto done;}
 const char *ek[]={"mode","seed","stream","purpose"};status=cp_json_keys(j,(unsigned)entropy,ek,4);if(status!=CP_OK)goto done;
 char mode[32],seed_text[32],stream_text[32],purpose[129];
 if(get_string(j,(unsigned)entropy,"mode",mode,sizeof(mode))!=CP_OK||get_string(j,(unsigned)entropy,"stream",stream_text,sizeof(stream_text))!=CP_OK||get_string(j,(unsigned)entropy,"purpose",purpose,sizeof(purpose))!=CP_OK){status=CP_INVALID;goto done;}
 uint64_t seed=0,stream=0;if(!hex_word(stream_text,&stream)){status=CP_INVALID;goto done;}
 if(!strcmp(mode,"deterministic")||!strcmp(mode,"recorded")){
  if(get_string(j,(unsigned)entropy,"seed",seed_text,sizeof(seed_text))!=CP_OK){status=CP_INVALID;goto done;}
  if(!hex_word(seed_text,&seed)){status=CP_INVALID;goto done;}
 }else if(cp_json_get(j,(unsigned)entropy,"seed")>=0){status=CP_INVALID;goto done;}
 cp_random_stream root={0};status=cp_random_init(&root,mode,seed,stream);if(status!=CP_OK)goto done;
 status=cp_random_named(&root,purpose,0,&c.rng);if(status!=CP_OK)goto done;
 c.root_rng=root;
 /* Preserve hardware provenance in both root and derived stream. */
 c.rng.source=root.source;c.rng.hardware_attempts=root.hardware_attempts;
 const char *ak[]={"interventions_authorized","fully_observed","iid_measurement_noise"};status=cp_json_keys(j,(unsigned)assumptions,ak,3);if(status!=CP_OK)goto done;
 if(get_bool(j,(unsigned)assumptions,ak[0],&c.interventions)!=CP_OK||get_bool(j,(unsigned)assumptions,ak[1],&c.fully_observed)!=CP_OK||get_bool(j,(unsigned)assumptions,ak[2],&c.iid)!=CP_OK){status=CP_INVALID;goto done;}
 const char *bk[]={"max_interventions","max_cost"};status=cp_json_keys(j,(unsigned)budget,bk,2);if(status!=CP_OK)goto done;
 status=get_unsigned(j,(unsigned)budget,bk[0],CP_RUN_ACTIONS,&c.max_actions);if(status!=CP_OK)goto done;
 if(get_number(j,(unsigned)budget,bk[1],&c.max_cost)!=CP_OK||c.max_cost<=0||get_number(j,0,"measurement_noise",&c.noise)!=CP_OK||c.noise<=0){status=CP_INVALID;goto done;}
 int costs=cp_json_get(j,0,"costs");
 if(costs>=0){
  if(cp_json_get(j,0,"cost_per_action")>=0||j->tokens[costs].type!='['||!j->tokens[costs].children||j->tokens[costs].children>CP_RUN_POOL){status=CP_INVALID;goto done;}
  c.costs_count=j->tokens[costs].children;
  for(unsigned k=0;k<c.costs_count;k++){int t=cp_json_at(j,(unsigned)costs,k);if(t<0||cp_json_number(j,(unsigned)t,c.costs+k)!=CP_OK||c.costs[k]<=0){status=CP_INVALID;goto done;}}
 }else if(get_number(j,0,"cost_per_action",&c.cost_per_action)!=CP_OK||c.cost_per_action<=0){status=CP_INVALID;goto done;}
 if(c.family==2){
  if(c.inputs!=1||!c.fully_observed){status=CP_UNSUPPORTED;goto done;}
  token=cp_json_get(j,0,"hypotheses");if(token<0){status=CP_INVALID;goto done;}
  status=read_hypotheses(j,(unsigned)token,&c);if(status!=CP_OK)goto done;
  cp_unit zero={{0}};for(unsigned i=0;i<2;i++)if(memcmp(&c.variables[i].unit,&zero,sizeof(zero))){status=CP_INVALID;goto done;}
 }
 cp_sha256(bytes,size,c.protocol_hash);*out=c;status=CP_OK;
 done:free(j);free(bytes);return status;
}
