#ifndef CP_INTERNAL_BOUNDS_H
#define CP_INTERNAL_BOUNDS_H
#include "caelum_port.h"
#include <stdint.h>
/* Caller owns buffer capacity. This guard proves index/byte representability;
   it cannot establish that an arbitrary pointer refers to a live allocation. */
static inline int cp_matrix_span_valid(cp_matrix_view x){
 const uint64_t limit=(uint64_t)(PTRDIFF_MAX/(ptrdiff_t)sizeof(double));
 if(!x.rows||!x.columns||!x.stride||x.columns>limit||x.stride<x.columns)return 0;
 if(x.rows-1>(limit-x.columns)/x.stride)return 0;
 uint64_t bytes=((x.rows-1)*x.stride+x.columns)*sizeof(double);
 return (uintptr_t)x.values%_Alignof(double)==0 &&
        bytes<=UINTPTR_MAX-(uintptr_t)x.values;
}
static inline int cp_double_buffers_overlap(const double *a,const double *b,size_t n){
 uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
 uintptr_t distance=aa>bb?aa-bb:bb-aa;
 return distance<n*sizeof(double);
}
#endif
