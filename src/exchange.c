#include "caelum_exchange.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

/* Native Arrow C Data Interface producer. Every pointer belongs to its own
 * corresponding release callback: never free exporter memory from importer. */
static void arrow_array_release(struct ArrowArray *a){
 if(!a || !a->release)return;
 a->release=NULL;
 if(a->children){
  for(int64_t j=0;j<a->n_children;j++){
   if(a->children[j]){if(a->children[j]->release)a->children[j]->release(a->children[j]);free(a->children[j]);}
  }
  free(a->children);
 }
 if(a->private_data)free(a->private_data);
 free((void*)a->buffers);
 a->private_data=NULL;a->buffers=NULL;a->children=NULL;
}
static void arrow_schema_release(struct ArrowSchema *s){
 if(!s || !s->release)return;
 s->release=NULL;
 if(s->children){
  for(int64_t j=0;j<s->n_children;j++){
   if(s->children[j]){if(s->children[j]->release)s->children[j]->release(s->children[j]);free(s->children[j]);}
  }
  free(s->children);
 }
 free((void*)s->name);
 s->name=NULL;s->children=NULL;
}
static int valid_table(const cp_dataset *d){
 if(!d||!d->x||!d->y||d->rows==0||d->features==0||d->features>CP_MAX_FEATURES||d->rows>1000000)return 0;
 if(d->rows>SIZE_MAX/(sizeof(double)*d->features))return 0;
 return 1;
}
cp_status cp_arrow_export_dataset(const cp_dataset *d,struct ArrowArray *a,struct ArrowSchema *s){
 if(!a||!s||!valid_table(d))return CP_INVALID;
 struct ArrowArray aa={0};struct ArrowSchema ss={0};
 aa.release=arrow_array_release;ss.release=arrow_schema_release;
 aa.length=(int64_t)d->rows;aa.null_count=0;aa.n_buffers=1;
 aa.n_children=(int64_t)d->features+1;
 ss.format="+s";ss.flags=0;ss.n_children=aa.n_children;
 aa.buffers=calloc(1,sizeof(void*));
 aa.children=calloc((size_t)aa.n_children,sizeof(struct ArrowArray*));
 ss.children=calloc((size_t)ss.n_children,sizeof(struct ArrowSchema*));
 if(!aa.buffers||!aa.children||!ss.children)goto oom;
 for(int64_t j=0;j<aa.n_children;j++){
  struct ArrowArray *child=calloc(1,sizeof(*child));
  struct ArrowSchema *sch=calloc(1,sizeof(*sch));
  aa.children[j]=child;ss.children[j]=sch;
  if(!child||!sch)goto oom;
  child->release=arrow_array_release;sch->release=arrow_schema_release;
  child->length=(int64_t)d->rows;child->n_buffers=2;child->null_count=0;
  child->buffers=calloc(2,sizeof(void*));
  double *values=malloc((size_t)d->rows*sizeof(double));
  if(!child->buffers||!values){free(values);goto oom;}
  child->private_data=values;
  ((const void**)child->buffers)[1]=values;
  for(uint64_t i=0;i<d->rows;i++){
   double v=j==(int64_t)d->features?d->y[i]:d->x[i*d->features+(uint64_t)j];
   if(!isfinite(v)){aa.release(&aa);ss.release(&ss);return CP_NUMERIC;}
   values[i]=v;
  }
  char name[32];
  if(j==(int64_t)d->features){memcpy(name,"y",2);}else{(void)snprintf(name,sizeof(name),"x%lld",(long long)j);}
  size_t n=strlen(name)+1;char *copy=malloc(n);
  if(!copy)goto oom;
  memcpy(copy,name,n);sch->name=copy;sch->format="g";
 }
 *a=aa;*s=ss;return CP_OK;
 oom:aa.release(&aa);ss.release(&ss);return CP_NO_MEMORY;
}
cp_status cp_arrow_import_dataset(const struct ArrowArray *a,const struct ArrowSchema *s,cp_dataset *out){
 if(!a||!s||!out||!s->format||strcmp(s->format,"+s")!=0||
    !a->release||!s->release||a->dictionary||s->dictionary||
    a->n_buffers!=1||!a->buffers||(s->flags&~INT64_C(2))!=0||
    a->offset!=0||a->length<1||a->length>1000000||
    a->null_count!=0||a->n_children<2||a->n_children>CP_MAX_COEFFICIENTS||
    a->n_children!=s->n_children||!a->children||!s->children)return CP_UNSUPPORTED;
 uint64_t rows=(uint64_t)a->length,features=(uint64_t)a->n_children-1;
 cp_dataset d={0};d.rows=rows;d.features=features;
 if(rows>SIZE_MAX/(features*sizeof(double)))return CP_RANGE;
 d.x=calloc((size_t)(rows*features),sizeof(double));d.y=calloc((size_t)rows,sizeof(double));
 if(!d.x||!d.y){cp_dataset_release(&d);return CP_NO_MEMORY;}
 for(uint64_t j=0;j<=features;j++){
  const struct ArrowArray *child=a->children[j];const struct ArrowSchema *sch=s->children[j];
  if(!child||!sch||!child->release||!sch->release||child->dictionary||sch->dictionary||
     child->n_children!=0||sch->n_children!=0||(sch->flags&~INT64_C(2))!=0||
     !sch->format||strcmp(sch->format,"g")||child->null_count!=0||
     child->length!=(int64_t)rows||child->offset<0||child->n_buffers!=2||!child->buffers||!child->buffers[1]){
   cp_dataset_release(&d);return CP_UNSUPPORTED;
  }
  char expected[32];
  if(j==features){memcpy(expected,"y",2);}else{(void)snprintf(expected,sizeof(expected),"x%llu",(unsigned long long)j);}
  if(!sch->name||strcmp(sch->name,expected)){cp_dataset_release(&d);return CP_INVALID;}
  const double *v=(const double*)child->buffers[1];
  if((uint64_t)child->offset>(uint64_t)PTRDIFF_MAX/sizeof(double)-rows||
     (uintptr_t)v%_Alignof(double)!=0||
     ((uint64_t)child->offset+rows)*sizeof(double)>UINTPTR_MAX-(uintptr_t)v){
   cp_dataset_release(&d);return CP_RANGE;
  }
  for(uint64_t i=0;i<rows;i++){
   double val=v[(uint64_t)child->offset+i];
   if(!isfinite(val)){cp_dataset_release(&d);return CP_NUMERIC;}
   if(j==features)d.y[i]=val;else d.x[i*features+j]=val;
  }
 }
 *out=d;return CP_OK;
}
void cp_owned_matrix_release(cp_owned_matrix *m){
 if(!m)return;
 free(m->data);
 memset(m,0,sizeof(*m));
}
/* DLPack ABI v1.0, owned CPU buffer. Headers reproduced under their
 * published C ABI layout, not copied implementation code. */
static void cp_dlpack_delete(cp_dl_managed_tensor_versioned *m){
 if(!m)return;
 free(m->dl_tensor.data);
 free(m->dl_tensor.shape);
 free(m->dl_tensor.strides);
 free(m);
}
cp_status cp_dlpack_export_features(const cp_dataset *d,cp_dl_managed_tensor_versioned **out){
 if(!out||!valid_table(d))return CP_INVALID;
 cp_dl_managed_tensor_versioned *m=calloc(1,sizeof(*m));
 if(!m)return CP_NO_MEMORY;
 m->version.major=1;m->version.minor=0;m->deleter=cp_dlpack_delete;
 m->flags=UINT64_C(2); /* DLPACK_FLAG_BITMASK_IS_COPIED */
 m->dl_tensor.device.device_type=1; /* kDLCPU */
 m->dl_tensor.dtype.code=2; /* kDLFloat */
 m->dl_tensor.dtype.bits=64;m->dl_tensor.dtype.lanes=1;
 m->dl_tensor.ndim=2;
 m->dl_tensor.shape=malloc(2*sizeof(int64_t));
 m->dl_tensor.strides=malloc(2*sizeof(int64_t));
 m->dl_tensor.data=malloc((size_t)(d->rows*d->features*sizeof(double)));
 if(!m->dl_tensor.shape||!m->dl_tensor.strides||!m->dl_tensor.data){m->deleter(m);return CP_NO_MEMORY;}
 m->dl_tensor.shape[0]=(int64_t)d->rows;m->dl_tensor.shape[1]=(int64_t)d->features;
 m->dl_tensor.strides[0]=(int64_t)d->features;m->dl_tensor.strides[1]=1;
 for(uint64_t i=0;i<d->rows*d->features;i++){
  if(!isfinite(d->x[i])){m->deleter(m);return CP_NUMERIC;}
 }
 memcpy(m->dl_tensor.data,d->x,(size_t)(d->rows*d->features*sizeof(double)));
 *out=m;return CP_OK;
}
cp_status cp_dlpack_import_features(const cp_dl_managed_tensor_versioned *m,cp_owned_matrix *out){
 if(!m||!out)return CP_INVALID;
 const cp_dl_tensor *t=&m->dl_tensor;
 if(m->version.major!=1||(m->flags&~UINT64_C(3))!=0||t->ndim!=2||
    t->device.device_type!=1||t->device.device_id!=0||t->dtype.code!=2||
    t->dtype.bits!=64||t->dtype.lanes!=1||!t->data||!t->shape||
    t->shape[0]<1||t->shape[0]>1000000||t->shape[1]<1||t->shape[1]>CP_MAX_FEATURES||
    (t->byte_offset%sizeof(double))!=0)return CP_UNSUPPORTED;
 uint64_t rows=(uint64_t)t->shape[0],cols=(uint64_t)t->shape[1];
 if(rows>SIZE_MAX/(cols*sizeof(double)))return CP_RANGE;
 int64_t sr=t->strides?t->strides[0]:(int64_t)cols;
 int64_t sc=t->strides?t->strides[1]:1;
 if(sr<1||sc<1)return CP_UNSUPPORTED;
 /* Validate every reachable stride offset before performing pointer arithmetic.
  * The producer still owns the memory-capacity contract. */
 uint64_t capacity=(uint64_t)(PTRDIFF_MAX/(ptrdiff_t)sizeof(double));
 if((cols-1)>(capacity-1)/(uint64_t)sc)return CP_RANGE;
 uint64_t coloff=(cols-1)*(uint64_t)sc;
 if((rows-1)>0&&(uint64_t)sr>(capacity-coloff-1)/(rows-1))return CP_RANGE;
 uint64_t maxoff=(rows-1)*(uint64_t)sr+coloff;
 if(t->byte_offset>(uint64_t)PTRDIFF_MAX-(maxoff+1)*sizeof(double)||
    (uintptr_t)t->data%_Alignof(double)!=0||
    t->byte_offset+(maxoff+1)*sizeof(double)>UINTPTR_MAX-(uintptr_t)t->data)return CP_RANGE;
 cp_owned_matrix o={0};o.data=malloc((size_t)(rows*cols*sizeof(double)));
 if(!o.data)return CP_NO_MEMORY;
 o.rows=rows;o.columns=cols;
 const double *v=(const double*)((const uint8_t*)t->data+t->byte_offset);
 for(uint64_t i=0;i<rows;i++)for(uint64_t j=0;j<cols;j++){
  double value=v[i*(uint64_t)sr+j*(uint64_t)sc];
  if(!isfinite(value)){cp_owned_matrix_release(&o);return CP_NUMERIC;}
  o.data[i*cols+j]=value;
 }
 *out=o;return CP_OK;
}
