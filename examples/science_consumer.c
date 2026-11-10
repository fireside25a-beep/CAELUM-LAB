/* Actual out-of-library SDK consumer: compiles using only installed headers.
   Program uses native symbols, no interpreter or embedded external ML engine. */
#include "caelum_science.h"
#include <math.h>
#include <stdio.h>
int main(void){
 double values[180]={0},responses[90]={0};
 for(unsigned i=0;i<90;i++){
  double x=sin((double)i*.613),z=cos((double)i*.389);
  values[2*i]=x;values[2*i+1]=z;
  responses[i]=1.25+2*x*x-0.75*x*z;
 }
 cp_random_stream r={0};
 if(cp_random_init(&r,"deterministic",112244,22)!=CP_OK)return 2;
 cp_matrix_view x={values,90,2,2};cp_law law={0};
 if(cp_law_discover(x,responses,2,cp_random_at(&r,0),&law)!=CP_OK)return 3;
 double point[2]={.2,.3},pred=0;
 if(cp_law_predict(&law,point,&pred)!=CP_OK)return 4;
 double truth=1.25+2*.2*.2-.75*.2*.3;
 if(fabs(pred-truth)>1e-9)return 5;
 cp_linear_model m={0};
 if(cp_law_as_linear(&law,&m)!=CP_OK)return 6;
 double basis[CP_LAW_TERM_LIMIT]={0},other=0;
 if(cp_law_basis(&law,point,basis)!=CP_OK)return 7;
 if(cp_predict_linear(&m,basis,&other)!=CP_OK)return 8;
 if(fabs(other-pred)>1e-12)return 9;
 printf("PASS science external C consumer: terms=%u prediction=%.17g\n",law.term_count,pred);
 return 0;
}
