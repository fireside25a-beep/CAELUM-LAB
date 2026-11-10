#include "caelum_lab.h"
#include <math.h>
#include <string.h>
#include <float.h>
static int same_unit(cp_unit a,cp_unit b){return memcmp(a.exponent,b.exponent,CP_UNIT_AXES)==0;}
static int dimensionless(cp_unit u){cp_unit zero={{0}};return same_unit(u,zero);}
static cp_status combine(cp_unit a,cp_unit b,int sign,cp_unit *out){
 cp_unit result={{0}};for(unsigned j=0;j<CP_UNIT_AXES;j++){int value=a.exponent[j]+sign*b.exponent[j];if(value<-32||value>32)return CP_RANGE;result.exponent[j]=(int8_t)value;}*out=result;return CP_OK;
}
cp_status cp_variable_validate(const cp_variable *v){
 if(!v||!memchr(v->name,0,sizeof(v->name))||!v->name[0]||!isfinite(v->lower)||!isfinite(v->upper)||v->lower>=v->upper||v->role<CP_ROLE_INPUT||v->role>CP_ROLE_TREATMENT||v->controllable>1)return CP_INVALID;
 for(unsigned j=0;j<CP_UNIT_AXES;j++)if(v->unit.exponent[j]<-32||v->unit.exponent[j]>32)return CP_RANGE;
 return CP_OK;
}
static cp_status units(const cp_expression *e,cp_unit result[CP_EXPR_LIMIT]){
 if(!e||!e->count||e->count>CP_EXPR_LIMIT||!e->inputs||e->inputs>CP_LAW_INPUT_LIMIT)return CP_INVALID;
 for(unsigned j=0;j<e->inputs;j++){cp_status s=cp_variable_validate(e->variables+j);if(s!=CP_OK)return s;}
 for(unsigned k=0;k<e->count;k++){
  const cp_expr_node *n=e->nodes+k;cp_unit zero={{0}};cp_status status=CP_OK;
  if(n->op==CP_E_CONST){if(!isfinite(n->value))return CP_NUMERIC;result[k]=n->constant_unit;for(unsigned j=0;j<7;j++)if(result[k].exponent[j]<-32||result[k].exponent[j]>32)return CP_RANGE;}
  else if(n->op==CP_E_VAR){if(n->variable>=e->inputs)return CP_INVALID;result[k]=e->variables[n->variable].unit;}
  else{
   if(n->left>=k)return CP_INVALID;
   cp_unit a=result[n->left];
   if(n->op>=CP_E_ADD&&n->op<=CP_E_DIV){
    if(n->right>=k)return CP_INVALID;
    cp_unit b=result[n->right];
    if(n->op==CP_E_ADD||n->op==CP_E_SUB){if(!same_unit(a,b))return CP_INVALID;result[k]=a;}
    else status=combine(a,b,n->op==CP_E_MUL?1:-1,result+k);
   }else if(n->op==CP_E_POW){
    if(n->power<-8||n->power>8)return CP_RANGE;
    result[k]=zero;for(unsigned j=0;j<7;j++){int power=a.exponent[j]*n->power;if(power<-32||power>32)return CP_RANGE;result[k].exponent[j]=(int8_t)power;}
   }else if(n->op>=CP_E_EXP&&n->op<=CP_E_COS){if(!dimensionless(a))return CP_INVALID;result[k]=zero;}
   else return CP_UNSUPPORTED;
  }
  if(status!=CP_OK)return status;
 }
 return CP_OK;
}
cp_status cp_expression_unit(const cp_expression *e,cp_unit *out){
 if(!out)return CP_INVALID;
 cp_unit results[CP_EXPR_LIMIT];cp_status status=units(e,results);if(status==CP_OK)*out=results[e->count-1];return status;
}
cp_status cp_expression_evaluate(const cp_expression *e,const double *x,double *value,double *gradient){
 if(!x||!value)return CP_INVALID;
 cp_unit result[CP_EXPR_LIMIT];cp_status status=units(e,result);if(status!=CP_OK)return status;
 for(unsigned j=0;j<e->inputs;j++)if(!isfinite(x[j])||x[j]<e->variables[j].lower||x[j]>e->variables[j].upper)return CP_RANGE;
 double values[CP_EXPR_LIMIT]={0},d[CP_EXPR_LIMIT][CP_LAW_INPUT_LIMIT]={{0}};
 for(unsigned k=0;k<e->count;k++){
  const cp_expr_node *n=e->nodes+k;double a=0,b=0,da=0,db=0;
  if(n->op==CP_E_CONST)values[k]=n->value;
  else if(n->op==CP_E_VAR){values[k]=x[n->variable];d[k][n->variable]=1;}
  else{
   a=values[n->left];if(n->op>=CP_E_ADD&&n->op<=CP_E_DIV)b=values[n->right];
   switch(n->op){
    case CP_E_ADD:values[k]=a+b;da=1;db=1;break;
    case CP_E_SUB:values[k]=a-b;da=1;db=-1;break;
    case CP_E_MUL:values[k]=a*b;da=b;db=a;break;
    case CP_E_DIV:if(b==0)return CP_NUMERIC;values[k]=a/b;da=1/b;db=-values[k]/b;break;
    case CP_E_POW:if(a==0&&n->power<0)return CP_NUMERIC;values[k]=pow(a,n->power);da=n->power==0?0:(double)n->power*pow(a,n->power-1);break;
    case CP_E_EXP:values[k]=exp(a);da=values[k];break;
    case CP_E_LOG:if(a<=0)return CP_NUMERIC;values[k]=log(a);da=1/a;break;
    case CP_E_SIN:values[k]=sin(a);da=cos(a);break;
    case CP_E_COS:values[k]=cos(a);da=-sin(a);break;
    default:return CP_UNSUPPORTED;
   }
   for(unsigned j=0;j<e->inputs;j++){
    d[k][j]=da*d[n->left][j];if(n->op>=CP_E_ADD&&n->op<=CP_E_DIV)d[k][j]+=db*d[n->right][j];
    if(!isfinite(d[k][j]))return CP_NUMERIC;
   }
  }
  if(!isfinite(values[k]))return CP_NUMERIC;
 }
 double final=values[e->count-1];if(gradient)memcpy(gradient,d[e->count-1],e->inputs*sizeof(double));*value=final;return CP_OK;
}
cp_status cp_expression_hash(const cp_expression *e,uint8_t hash[32]){
 if(!hash)return CP_INVALID;
 cp_unit u[CP_EXPR_LIMIT];cp_status status=units(e,u);if(status!=CP_OK)return status;
 uint8_t hashes[CP_EXPR_LIMIT][32];
 for(unsigned k=0;k<e->count;k++){
  const cp_expr_node *n=e->nodes+k;uint8_t bytes[128]={0};size_t used=0;
  bytes[used++]=(uint8_t)n->op;memcpy(bytes+used,u[k].exponent,7);used+=7;
  if(n->op==CP_E_CONST){uint64_t bits=0;double v=n->value==0?0:n->value;memcpy(&bits,&v,8);for(unsigned b=0;b<8;b++)bytes[used++]=(uint8_t)(bits>>(8*b));}
  else if(n->op==CP_E_VAR){bytes[used++]=(uint8_t)n->variable;size_t len=strlen(e->variables[n->variable].name);memcpy(bytes+used,e->variables[n->variable].name,len);used+=len;}
  else{
   const uint8_t *a=hashes[n->left],*b=n->op>=CP_E_ADD&&n->op<=CP_E_DIV?hashes[n->right]:NULL;
   if(b&&(n->op==CP_E_ADD||n->op==CP_E_MUL)&&memcmp(a,b,32)>0){const uint8_t *t=a;a=b;b=t;}
   memcpy(bytes+used,a,32);used+=32;if(b){memcpy(bytes+used,b,32);used+=32;}
   if(n->op==CP_E_POW)bytes[used++]=(uint8_t)(n->power+8);
  }
  cp_sha256(bytes,used,hashes[k]);
 }
 /* Bind the evaluation contract as well as the canonical operation tree. */
 uint8_t contract[32+CP_LAW_INPUT_LIMIT*64]={0};size_t used=32;
 memcpy(contract,hashes[e->count-1],32);
 for(unsigned j=0;j<e->inputs;j++){
  const cp_variable *v=e->variables+j;memcpy(contract+used,v->name,strlen(v->name));used+=32;
  memcpy(contract+used,v->unit.exponent,7);used+=7;
  contract[used++]=(uint8_t)v->role;contract[used++]=(uint8_t)v->controllable;
  double limits[2]={v->lower,v->upper};
  for(unsigned k=0;k<2;k++){uint64_t bits=0;double value=limits[k]==0?0:limits[k];memcpy(&bits,&value,8);for(unsigned b=0;b<8;b++)contract[used++]=(uint8_t)(bits>>(8*b));}
 }
 cp_sha256(contract,used,hash);return CP_OK;
}
cp_status cp_law_expression(const cp_law *law,const cp_variable *variables,cp_unit response,cp_expression *out){
 if(!law||!variables||!out||law->inputs<1||law->inputs>3||!law->term_count||law->term_count>16)return CP_INVALID;
 double probe[3]={0},test=0;cp_status status=cp_law_predict(law,probe,&test);if(status!=CP_OK)return status;
 cp_expression e={0};e.inputs=law->inputs;memcpy(e.variables,variables,e.inputs*sizeof(cp_variable));
 unsigned vars[3]={0};for(unsigned j=0;j<e.inputs;j++){vars[j]=e.count;e.nodes[e.count++]=(cp_expr_node){.op=CP_E_VAR,.variable=j};}
 unsigned sum=0;
 for(unsigned k=0;k<law->term_count;k++){
  cp_unit coefficient=response;
  for(unsigned j=0;j<e.inputs;j++)for(unsigned axis=0;axis<7;axis++){
   int v=coefficient.exponent[axis]-(int)law->powers[k][j]*variables[j].unit.exponent[axis];if(v<-32||v>32)return CP_RANGE;coefficient.exponent[axis]=(int8_t)v;
  }
  unsigned term=e.count;e.nodes[e.count++]=(cp_expr_node){.op=CP_E_CONST,.value=law->coefficients[k],.constant_unit=coefficient};
  for(unsigned j=0;j<e.inputs;j++)if(law->powers[k][j]){
   unsigned factor=vars[j];if(law->powers[k][j]!=1){factor=e.count;e.nodes[e.count++]=(cp_expr_node){.op=CP_E_POW,.left=vars[j],.power=law->powers[k][j]};}
   unsigned product=e.count;e.nodes[e.count++]=(cp_expr_node){.op=CP_E_MUL,.left=term,.right=factor};term=product;
  }
  if(k==0)sum=term;else{unsigned addition=e.count;e.nodes[e.count++]=(cp_expr_node){.op=CP_E_ADD,.left=sum,.right=term};sum=addition;}
  if(e.count>CP_EXPR_LIMIT-10)return CP_RANGE;
 }
 cp_unit actual;status=cp_expression_unit(&e,&actual);if(status!=CP_OK||!same_unit(actual,response))return CP_INVALID;
 *out=e;return CP_OK;
}
