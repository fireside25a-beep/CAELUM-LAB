#include "caelum_lab.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define PID _getpid
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define PID getpid
#define MKDIR(p) mkdir(p,0700)
#endif
static unsigned checks=0;
#define T(v) do{checks++;if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);return 1;}}while(0)
static int rng_tests(void){
 T(cp_write_atomic_file(NULL,NULL,0)==CP_INVALID);
 T(cp_write_atomic_file("unwritten.bin",NULL,64u*1024u*1024u+1)==CP_RANGE);
 const uint32_t ctr[3][4]={{0,0,0,0},{0xffffffff,0xffffffff,0xffffffff,0xffffffff},{0x243f6a88,0x85a308d3,0x13198a2e,0x03707344}};
 const uint32_t keys[3][2]={{0,0},{0xffffffff,0xffffffff},{0xa4093822,0x299f31d0}};
 const uint32_t expected[3][4]={{0x6627e8d5,0xe169c58d,0xbc57ac4c,0x9b00dbd8},{0x408f276d,0x41c83b0e,0xa20bc7c6,0x6d5451fd},{0xd16cfe09,0x94fdcceb,0x5001e420,0x24126ea1}};
 for(unsigned k=0;k<3;k++){uint32_t actual[4];cp_philox4x32_10(ctr[k],keys[k],actual);T(!memcmp(actual,expected[k],sizeof(actual)));}
 cp_random_stream root={1,2,0,0},a={0},b={0},c={0};
 T(cp_random_named(&root,"split",0,&a)==CP_OK);T(cp_random_named(&root,"selection",0,&b)==CP_OK);T(cp_random_named(&root,"split",1,&c)==CP_OK);
 T(a.seed!=b.seed&&a.stream!=b.stream);T(a.seed!=c.seed&&a.stream!=c.stream);
 cp_random_stream swap={2,1,0,0};T(cp_random_named(&swap,"split",0,&c)==CP_OK);T(a.seed!=c.seed);
 for(unsigned i=0;i<100;i++)T(cp_random_v2_at(&a,i)==cp_random_v2_at(&a,i));
 return 0;
}
static int expression_tests(void){
 cp_expression e={0};e.inputs=1;e.count=2;strcpy(e.variables[0].name,"length");e.variables[0].lower=-10;e.variables[0].upper=10;e.variables[0].role=CP_ROLE_INPUT;e.variables[0].unit.exponent[0]=1;
 e.nodes[0]=(cp_expr_node){.op=CP_E_VAR};e.nodes[1]=(cp_expr_node){.op=CP_E_POW,.left=0,.power=2};
 cp_unit unit={{0}};T(cp_expression_unit(&e,&unit)==CP_OK);T(unit.exponent[0]==2);
 double x=3,y=0,g=0;T(cp_expression_evaluate(&e,&x,&y,&g)==CP_OK);T(y==9&&g==6);
 uint8_t a[32],b[32];T(cp_expression_hash(&e,a)==CP_OK);e.variables[0].upper=11;T(cp_expression_hash(&e,b)==CP_OK);T(memcmp(a,b,32));
 x=12;y=123;T(cp_expression_evaluate(&e,&x,&y,&g)==CP_RANGE);T(y==123);
 e.nodes[1].op=CP_E_EXP;T(cp_expression_unit(&e,&unit)==CP_INVALID);
 e.variables[0].unit.exponent[0]=0;e.nodes[1].op=CP_E_LOG;x=0;y=123;T(cp_expression_evaluate(&e,&x,&y,&g)==CP_NUMERIC);T(y==123);
 return 0;
}
static void scm(cp_binary_scm *m,double p0,double p1){
 memset(m,0,sizeof(*m));m->variables=2;m->parent_mask[1]=1;
 for(unsigned s=0;s<4;s++){m->probability_one[0][s]=.5;m->probability_one[1][s]=(s&1)?p1:p0;}
}
static int causal_tests(void){
 cp_binary_scm models[2];scm(models,.2,.8);scm(models+1,.8,.2);double joint[16],effect=0,utility=0,weights[2]={.5,.5};
 T(cp_scm_validate(models)==CP_OK);T(cp_scm_joint(models,0,0,joint)==CP_OK);T(fabs(joint[0]-.4)<1e-15);
 T(cp_scm_effect(models,0,1,&effect)==CP_OK);T(fabs(effect-.6)<1e-15);
 T(cp_causal_adjustment(joint,2,0,1,0,&effect)==CP_OK);T(fabs(effect-.6)<1e-15);
 T(cp_finite_eig(models,weights,2,0,1,0,&utility)==CP_OK);T(fabs(utility-(log(2)+.2*log(.2)+.8*log(.8)))<1e-14);
 models[0].parent_mask[0]=2;T(cp_scm_validate(models)==CP_INVALID);
 cp_binary_scm tiny={0};tiny.variables=2;
 for(unsigned state=0;state<4;state++){tiny.probability_one[0][state]=1e-250;tiny.probability_one[1][state]=1e-250;}
 joint[0]=77;T(cp_scm_joint(&tiny,0,0,joint)==CP_NUMERIC);T(joint[0]==77);
 uint8_t independent[4000];for(unsigned i=0;i<2000;i++){independent[2*i]=(uint8_t)(i%2);independent[2*i+1]=(uint8_t)((i/2)%2);}
 cp_causal_graph graph={0};T(cp_binary_pc(independent,2000,2,.01,&graph)==CP_OK);T(graph.removed_edges==1&&graph.undirected[0]==0);
 uint8_t *collider=malloc(30000);T(collider);size_t rows=0;
 const unsigned counts[8]={2250,1750,1750,250,250,750,750,2250};
 for(unsigned state=0;state<8;state++)for(unsigned repeat=0;repeat<counts[state];repeat++){for(unsigned v=0;v<3;v++)collider[rows*3+v]=(uint8_t)((state>>v)&1u);rows++;}
 T(rows==10000);T(cp_binary_pc(collider,rows,3,.01,&graph)==CP_OK);free(collider);
 T(graph.directed[0]==4&&graph.directed[1]==4&&graph.undirected[0]==0&&graph.undirected[1]==0);
 return 0;
}
static int fitting_tests(void){
 double residuals[99];for(unsigned i=0;i<99;i++)residuals[i]=(double)i;double radius=-1;
 T(cp_conformal_radius(residuals,99,.05,&radius)==CP_OK);T(radius==94);
 radius=77;T(cp_conformal_radius(residuals,3,.05,&radius)==CP_UNAVAILABLE);T(radius==77);
 double x[40],y[40];for(unsigned i=0;i<40;i++){x[i]=((double)i-20)*1e100;y[i]=1+2*x[i]/1e100;}
 cp_matrix_view view={x,40,1,1};cp_linear_model m={0};cp_fit_diagnostics diag={0};
 T(cp_fit_linear_checked(view,y,&m,&diag)==CP_OK);T(fabs(m.beta[0]-1)<1e-12);T(fabs(m.beta[1]/2e-100-1)<1e-12);
 for(unsigned i=0;i<40;i++)x[i]/=1e100;
 T(cp_fit_ridge(view,y,2,&m)==CP_OK);double xm=-.5,var=0;for(unsigned i=0;i<40;i++)var+=(x[i]-xm)*(x[i]-xm);
 T(fabs(m.beta[1]-2*var/(var+2))<1e-12);
 y[20]+=1000;T(cp_fit_huber(view,y,1,1000,&m,&diag)==CP_OK);T(fabs(m.beta[1]-2)<.01);T(fabs(m.beta[0]-1)<.1);
 cp_linear_model sentinel=m;T(cp_fit_huber(view,y,1,1,&m,NULL)==CP_UNAVAILABLE);T(!memcmp(&m,&sentinel,sizeof(m)));
 return 0;
}
static int equal_file(const char *a,const char *b){FILE *fa=fopen(a,"rb"),*fb=fopen(b,"rb");if(!fa||!fb){if(fa)fclose(fa);if(fb)fclose(fb);return 0;}int same=1,c,d;do{c=fgetc(fa);d=fgetc(fb);if(c!=d)same=0;}while(c!=EOF&&d!=EOF);fclose(fa);fclose(fb);return same;}
static int run_tests(const char *root){
 char dir[4096],obs[4096],pool[4096],hold[4096],protocol[4096],a[4096],b[4096],ea[4096],eb[4096];
 int created=0;for(unsigned attempt=0;attempt<10000;attempt++){
  snprintf(dir,sizeof(dir),"%s/lab-test-%ld-%u",root,(long)PID(),attempt);
  if(MKDIR(dir)==0){created=1;break;}if(errno!=EEXIST)break;
 }T(created);
 snprintf(obs,sizeof(obs),"%s/observations.csv",dir);snprintf(pool,sizeof(pool),"%s/candidates.csv",dir);snprintf(hold,sizeof(hold),"%s/holdout.csv",dir);snprintf(protocol,sizeof(protocol),"%s/protocol.json",dir);
 const char *files[]={obs,pool,hold};for(unsigned k=0;k<3;k++){FILE *f=fopen(files[k],"wb");T(f);for(unsigned i=0;i<40;i++){unsigned x=i%2,y=(i*7%10)<(x?8:2);fprintf(f,"%u,%u\n",x,y);}T(fclose(f)==0);}
 FILE *f=fopen(protocol,"wb");T(f);fputs(
 "{\"schema_version\":1,\"experiment_kind\":\"caelum.discovery.v1\",\"family\":\"finite_scm\",\"policy\":\"eig\","
 "\"observations\":\"observations.csv\",\"candidates\":\"candidates.csv\",\"holdout\":\"holdout.csv\","
 "\"variables\":[{\"name\":\"x\",\"unit\":[0,0,0,0,0,0,0],\"lower\":0,\"upper\":1,\"role\":\"treatment\",\"controllable\":true},"
 "{\"name\":\"y\",\"unit\":[0,0,0,0,0,0,0],\"lower\":0,\"upper\":1,\"role\":\"response\",\"controllable\":false}],"
 "\"entropy\":{\"mode\":\"deterministic\",\"seed\":\"12345678\",\"stream\":\"1\",\"purpose\":\"study/selection\"},"
 "\"assumptions\":{\"interventions_authorized\":true,\"fully_observed\":true,\"iid_measurement_noise\":true},"
 "\"budget\":{\"max_interventions\":10,\"max_cost\":10},\"measurement_noise\":0.1,\"cost_per_action\":1,"
 "\"hypotheses\":[{\"parents\":[0,1],\"probability_one\":[[0.5],[0.2,0.8]],\"prior\":1},"
 "{\"parents\":[0,1],\"probability_one\":[[0.5],[0.8,0.2]],\"prior\":1}]}",f);T(fclose(f)==0);
 snprintf(a,sizeof(a),"%s/resumed",dir);snprintf(b,sizeof(b),"%s/uninterrupted",dir);
 cp_run_summary sa={0},sb={0};T(cp_lab_run(protocol,a,3,0,&sa)==CP_OK);T(sa.phase==CP_RUN_STOPPED&&sa.steps==3);
 T(cp_lab_run_replay(a)==CP_OK);T(cp_lab_run(NULL,a,0,1,&sa)==CP_OK);T(sa.phase==CP_RUN_SEALED&&sa.steps==10&&sa.cost==10);
 T(cp_lab_run_replay(a)==CP_OK);T(cp_lab_run(protocol,b,0,0,&sb)==CP_OK);T(!memcmp(&sa,&sb,sizeof(sa)));
 snprintf(ea,sizeof(ea),"%s/events.jsonl",a);snprintf(eb,sizeof(eb),"%s/events.jsonl",b);T(equal_file(ea,eb));
 snprintf(ea,sizeof(ea),"%s/inputs/candidates.csv",a);f=fopen(ea,"ab");T(f);fputs("0,0\n",f);T(fclose(f)==0);T(cp_lab_run_replay(a)==CP_CORRUPT);
 return 0;
}
int main(int argc,char **argv){if(argc!=2)return 2;if(rng_tests()||expression_tests()||causal_tests()||fitting_tests()||run_tests(argv[1]))return 1;printf("PASS %u lab/random/units/causal/learning/checkpoint assertions\n",checks);return 0;}
