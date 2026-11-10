#include "caelum_lab.h"
#include <assert.h>
extern uint32_t nondet_uint32_t(void);
int main(void){
 uint32_t c[4],saved[4],key[2],out[4];
 for(unsigned i=0;i<4;i++){c[i]=nondet_uint32_t();saved[i]=c[i];}
 key[0]=nondet_uint32_t();key[1]=nondet_uint32_t();
 cp_philox4x32_10(c,key,out);
 for(unsigned i=0;i<4;i++)assert(c[i]==saved[i]);
 /* Actual C memory/loop/signed-arithmetic safety, including valid aliasing.
    No claim here about output statistics or equivalence to a second RNG. */
 cp_philox4x32_10(c,key,c);
 cp_philox4x32_10((const uint32_t*)0,(const uint32_t*)0,(uint32_t*)0);
 return 0;
}
