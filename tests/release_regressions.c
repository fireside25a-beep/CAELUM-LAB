#include "caelum_science.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#define T(v) do {if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);return 1;}}while(0)
int main(void){
 cp_law f[3]={{0}};
 for(unsigned k=0;k<2;k++){f[k].inputs=2;f[k].degree=1;f[k].term_count=1;f[k].selection_steps=1;}
 f[1].term_count=f[1].selection_steps=2;f[1].attempted_candidates=1;
 f[1].powers[1][1]=1;f[1].coefficients[1]=1;
 double x[2]={1e20,1},next[2]={17,19},back[2]={0};
 T(cp_dynamics_step_midpoint(f,2,x,1,next)==CP_OK);
 T(fabs(next[1]-3)<1e-12);
 T(cp_dynamics_step_midpoint(f,2,next,-1,back)==CP_OK);
 T(fabs(back[1]-1)<1e-12);
 for(int exponent=-200;exponent<=200;exponent+=20){
  x[0]=pow(10.,exponent);x[1]=1;
  T(cp_dynamics_step_midpoint(f,2,x,1,next)==CP_OK);
  T(fabs(next[1]-3)<1e-12);
  T(cp_dynamics_step_midpoint(f,2,next,-1,back)==CP_OK);
  T(fabs(back[1]-1)<1e-12);
 }
 for(int exponent=-200;exponent<=200;exponent+=20){
  x[0]=1e20;x[1]=pow(10.,exponent);
  T(cp_dynamics_step_midpoint(f,2,x,1,next)==CP_OK);
  T(fabs(next[1]/x[1]-3)<1e-12);
  T(cp_dynamics_step_midpoint(f,2,next,-1,back)==CP_OK);
  T(fabs(back[1]/x[1]-1)<1e-12);
 }
 puts("PASS mixed-component midpoint/reverse regressions");return 0;
}
