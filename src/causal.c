#include "caelum_lab.h"
#include <math.h>
#include <string.h>
#include <float.h>
static unsigned bits(uint32_t n){unsigned c=0;while(n){c+=n&1u;n>>=1;}return c;}
cp_status cp_scm_validate(const cp_binary_scm *m){
 if(!m||!m->variables||m->variables>CP_CAUSAL_LIMIT)return CP_INVALID;
 uint32_t all=(1u<<m->variables)-1,done=0;
 for(unsigned i=0;i<m->variables;i++){
  if((m->parent_mask[i]&~all)||(m->parent_mask[i]&(1u<<i)))return CP_INVALID;
  for(unsigned s=0;s<=all;s++){
   double p=m->probability_one[i][s];if(!isfinite(p)||p<0||p>1||p!=m->probability_one[i][s&m->parent_mask[i]])return CP_INVALID;
  }
 }
 for(unsigned round=0;round<m->variables;round++)for(unsigned i=0;i<m->variables;i++)if((m->parent_mask[i]&~done)==0)done|=1u<<i;
 return done==all?CP_OK:CP_INVALID;
}
cp_status cp_scm_joint(const cp_binary_scm *m,uint32_t mask,uint32_t values,double out[1u<<CP_CAUSAL_LIMIT]){
 if(!out)return CP_INVALID;
 cp_status status=cp_scm_validate(m);if(status!=CP_OK)return status;
 uint32_t all=(1u<<m->variables)-1;if((mask&~all)||(values&~mask))return CP_INVALID;
 double probabilities[16]={0},sum=0;
 for(unsigned state=0;state<=all;state++){
  double p=1;int all_positive=1;
  for(unsigned v=0;v<m->variables;v++){
   unsigned bit=(state>>v)&1u;
   if(mask&(1u<<v)){if(bit!=((values>>v)&1u)){p=0;all_positive=0;break;}}
   else{double one=m->probability_one[v][state&m->parent_mask[v]],factor=bit?one:1-one;if(factor==0)all_positive=0;p*=factor;}
  }
  /* Underflow of positive factors is not a logically impossible event. */
  if(p==0&&all_positive)return CP_NUMERIC;
  probabilities[state]=p;sum+=p;
 }
 if(!isfinite(sum)||fabs(sum-1)>128*DBL_EPSILON)return CP_NUMERIC;
 memcpy(out,probabilities,sizeof(probabilities));return CP_OK;
}
cp_status cp_scm_effect(const cp_binary_scm *m,unsigned treatment,unsigned outcome,double *effect){
 if(!m||!effect||treatment>=m->variables||outcome>=m->variables||treatment==outcome)return CP_INVALID;
 double means[2]={0};
 for(unsigned value=0;value<2;value++){
  double joint[16];cp_status status=cp_scm_joint(m,1u<<treatment,value<<treatment,joint);if(status!=CP_OK)return status;
  for(unsigned state=0;state<(1u<<m->variables);state++)if(state&(1u<<outcome))means[value]+=joint[state];
 }
 *effect=means[1]-means[0];return CP_OK;
}
cp_status cp_causal_adjustment(const double *joint,unsigned n,unsigned treatment,unsigned outcome,uint32_t mask,double *effect){
 if(!joint||!effect||n<2||n>4||treatment>=n||outcome>=n||outcome==treatment||(mask&((1u<<treatment)|(1u<<outcome)))||(mask>>n))return CP_INVALID;
 double total=0;for(unsigned s=0;s<(1u<<n);s++){if(!isfinite(joint[s])||joint[s]<0||joint[s]>1)return CP_INVALID;total+=joint[s];}
 if(fabs(total-1)>1e-10)return CP_INVALID;
 double means[2]={0};
 for(unsigned z=0;z<(1u<<n);z++)if((z&~mask)==0){
  double pz=0,px[2]={0},pyx[2]={0};
  for(unsigned s=0;s<(1u<<n);s++)if((s&mask)==z){unsigned x=(s>>treatment)&1u;pz+=joint[s];px[x]+=joint[s];if(s&(1u<<outcome))pyx[x]+=joint[s];}
  if(pz==0)continue;
  if(px[0]<=0||px[1]<=0)return CP_UNAVAILABLE;
  for(unsigned x=0;x<2;x++)means[x]+=pz*pyx[x]/px[x];
 }
 *effect=means[1]-means[0];return CP_OK;
}
static double chi_square_tail(double statistic,unsigned df){
 double z=fmax(0,statistic)*.5;
 if(df==1)return erfc(sqrt(z));
 if(df==2)return exp(-z);
 if(df==3)return erfc(sqrt(z))+2*sqrt(z/3.14159265358979323846)*exp(-z);
 if(df==4)return (1+z)*exp(-z);
 return NAN;
}
static cp_status independent(const uint8_t *rows,size_t n,unsigned vars,unsigned a,unsigned b,uint32_t cond,double alpha,int *result){
 double statistic=0;unsigned df=0;
 for(unsigned z=0;z<(1u<<vars);z++)if((z&~cond)==0){
  double table[2][2]={{0}},marg_a[2]={0},marg_b[2]={0},total=0;
  for(size_t i=0;i<n;i++){
   unsigned state=0;for(unsigned j=0;j<vars;j++)state|=(unsigned)rows[i*vars+j]<<j;
   if((state&cond)!=z)continue;
   unsigned av=rows[i*vars+a],bv=rows[i*vars+b];table[av][bv]++;marg_a[av]++;marg_b[bv]++;total++;
  }
  if(total==0||marg_a[0]==0||marg_a[1]==0||marg_b[0]==0||marg_b[1]==0)return CP_UNAVAILABLE;
  for(unsigned i=0;i<2;i++)for(unsigned j=0;j<2;j++){
   double expected=marg_a[i]*marg_b[j]/total;if(expected<5)return CP_UNAVAILABLE;
   if(table[i][j]>0)statistic+=2*table[i][j]*log(table[i][j]/expected);
  }
  df++;
 }
 double p=chi_square_tail(statistic,df);if(!isfinite(p))return CP_NUMERIC;
 *result=p>alpha;return CP_OK;
}
static int adjacent(const cp_causal_graph *g,unsigned a,unsigned b){return ((g->directed[a]|g->undirected[a])&(1u<<b))!=0||((g->directed[b]>>a)&1u);}
static int path(const cp_causal_graph *g,unsigned from,unsigned to){
 uint32_t reached=1u<<from;
 for(unsigned k=0;k<g->variables;k++)for(unsigned i=0;i<g->variables;i++)if(reached&(1u<<i))reached|=g->directed[i];
 return (reached&(1u<<to))!=0;
}
static int orient(cp_causal_graph *g,unsigned a,unsigned b){
 if(!(g->undirected[a]&(1u<<b))){if(g->directed[b]&(1u<<a))g->ambiguous=1;return 0;}
 if(path(g,b,a)){g->ambiguous=1;return 0;}
 g->undirected[a]&=~(1u<<b);g->undirected[b]&=~(1u<<a);g->directed[a]|=1u<<b;return 1;
}
cp_status cp_binary_pc(const uint8_t *data,size_t rows,unsigned n,double alpha,cp_causal_graph *out){
 if(!data||!out||rows<30||rows>1000000||n<2||n>4||!isfinite(alpha)||alpha<=0||alpha>=1)return CP_INVALID;
 for(size_t i=0;i<rows*n;i++)if(data[i]>1)return CP_INVALID;
 uint32_t adjacency[4]={0},separators[4][4]={{0}};cp_causal_graph g={0};g.variables=n;
 for(unsigned i=0;i<n;i++)adjacency[i]=((1u<<n)-1)^(1u<<i);
 for(unsigned size=0;size<n-1;size++){
  uint32_t previous[4];memcpy(previous,adjacency,sizeof(previous));
  for(unsigned a=0;a<n;a++)for(unsigned b=a+1;b<n;b++)if(previous[a]&(1u<<b)){
   for(unsigned z=0;z<(1u<<n);z++){
    if(bits(z)!=size||(z&((1u<<a)|(1u<<b))))continue;
    if((z&~previous[a])&&(z&~previous[b]))continue;
    int yes=0;g.conditional_tests++;cp_status status=independent(data,rows,n,a,b,z,alpha,&yes);
    if(status==CP_UNAVAILABLE){g.ambiguous=1;continue;}
    if(status!=CP_OK)return status;
    if(yes){adjacency[a]&=~(1u<<b);adjacency[b]&=~(1u<<a);separators[a][b]=separators[b][a]=z;g.removed_edges++;break;}
   }
  }
 }
 memcpy(g.undirected,adjacency,sizeof(adjacency));
 for(unsigned a=0;a<n;a++)for(unsigned b=a+1;b<n;b++)if(!(adjacency[a]&(1u<<b)))
  for(unsigned c=0;c<n;c++)if(c!=a&&c!=b&&(adjacency[a]&(1u<<c))&&(adjacency[b]&(1u<<c))&&!(separators[a][b]&(1u<<c))){orient(&g,a,c);orient(&g,b,c);}
 for(unsigned iteration=0;iteration<n*n;iteration++){
  int changed=0;
  for(unsigned a=0;a<n;a++)for(unsigned b=0;b<n;b++)if(a!=b&&(g.undirected[a]&(1u<<b))){
   int rule=path(&g,a,b);
   for(unsigned c=0;c<n;c++)if(c!=a&&c!=b&&(g.directed[c]&(1u<<a))&&!adjacent(&g,c,b))rule=1;
   for(unsigned c=0;c<n;c++)for(unsigned d=c+1;d<n;d++)
    if(c!=a&&c!=b&&d!=a&&d!=b&&(g.undirected[a]&(1u<<c))&&(g.undirected[a]&(1u<<d))&&
      (g.directed[c]&(1u<<b))&&(g.directed[d]&(1u<<b))&&!adjacent(&g,c,d))rule=1;
   if(rule)changed|=orient(&g,a,b);
  }
  if(!changed)break;
 }
 for(unsigned a=0;a<n;a++)if(g.undirected[a])g.ambiguous=1;
 *out=g;return CP_OK;
}
static double entropy(double p){if(p<=0||p>=1)return 0;return -p*log(p)-(1-p)*log1p(-p);}
cp_status cp_finite_eig(const cp_binary_scm *models,const double *weights,unsigned count,unsigned treatment,unsigned outcome,unsigned value,double *utility){
 if(!models||!weights||!utility||count<1||count>CP_HYPOTHESIS_LIMIT||value>1)return CP_INVALID;
 double ps[CP_HYPOTHESIS_LIMIT]={0},mass=0,mixture=0,conditional=0;
 for(unsigned k=0;k<count;k++){
  if(!isfinite(weights[k])||weights[k]<0||treatment>=models[k].variables||outcome>=models[k].variables||treatment==outcome)return CP_INVALID;
  double joint[16];cp_status status=cp_scm_joint(models+k,1u<<treatment,value<<treatment,joint);if(status!=CP_OK)return status;
  for(unsigned s=0;s<(1u<<models[k].variables);s++)if(s&(1u<<outcome))ps[k]+=joint[s];
  mass+=weights[k];
 }
 if(!isfinite(mass)||mass<=0)return CP_INVALID;
 for(unsigned k=0;k<count;k++){double w=weights[k]/mass;mixture+=w*ps[k];conditional+=w*entropy(ps[k]);}
 double information=entropy(mixture)-conditional;if(!isfinite(information)||information<-1e-12)return CP_NUMERIC;
 *utility=fmax(0,information);return CP_OK;
}
