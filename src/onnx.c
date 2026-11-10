/* Restricted, real ONNX protobuf interchange for double-precision affine
 * predictors. No framework dependency; strict allowlist of Gemm semantics.
 * Export: ONNX IR 9, ai.onnx opset 13, input X[N,F], output Y[N,1],
 * W[F,1], B[1]. Import rejects ambiguous operator graphs and attributes.
 * This intentionally is NOT a general ONNX interpreter or converter. */
#include "caelum_port.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <errno.h>
#include "io.h"
#include "locale_numeric.h"

typedef struct {uint8_t *p;size_t n,cap;int err;} writer;
static void wr(writer *w,const void *p,size_t n){if(w->err)return;if(n>w->cap-w->n){w->err=1;return;}memcpy(w->p+w->n,p,n);w->n+=n;}
static void vi(writer *w,uint64_t n){while(n>=128){uint8_t v=(uint8_t)((n&127)|128);wr(w,&v,1);n>>=7;}uint8_t v=(uint8_t)n;wr(w,&v,1);}
static void key(writer *w,unsigned field,unsigned wire){vi(w,(uint64_t)field*8+wire);}
static void var(writer *w,unsigned field,uint64_t x){key(w,field,0);vi(w,x);}
static void bytes(writer *w,unsigned field,const void *p,size_t n){key(w,field,2);vi(w,n);wr(w,p,n);}
static void str(writer *w,unsigned field,const char *s){bytes(w,field,s,strlen(s));}
static void msg(writer *w,unsigned field,writer *v){if(v->err){w->err=1;return;}bytes(w,field,v->p,v->n);}
static void le64(uint8_t out[8],uint64_t n){for(int i=0;i<8;i++)out[i]=(uint8_t)(n>>(8*i));}
static void tensor(writer *g,const char *name,const double *values,uint64_t n,int weight){
 uint8_t raw[CP_MAX_FEATURES*8];uint8_t tmp[1536];writer t={tmp,0,sizeof(tmp),0};
 if(weight){var(&t,1,n);var(&t,1,1);}else{var(&t,1,1);}
 var(&t,2,11);str(&t,8,name);
 for(uint64_t j=0;j<n;j++){uint64_t b;memcpy(&b,&values[j],8);le64(raw+j*8,b);}
 bytes(&t,9,raw,(size_t)n*8);msg(g,5,&t);
}
static void dim(writer *shape,uint64_t n,const char *symbol){uint8_t b[128];writer d={b,0,sizeof(b),0};if(symbol)str(&d,2,symbol);else var(&d,1,n);msg(shape,1,&d);}
static void value_info(writer *graph,unsigned field,const char *name,uint64_t features,int output){
 uint8_t sm[256],tm[512],ty[768],vi_buf[1024];
 writer s={sm,0,sizeof(sm),0},t={tm,0,sizeof(tm),0},type={ty,0,sizeof(ty),0},info={vi_buf,0,sizeof(vi_buf),0};
 dim(&s,0,"N");dim(&s,output?1:features,NULL);
 var(&t,1,11);msg(&t,2,&s);msg(&type,1,&t);
 str(&info,1,name);msg(&info,2,&type);msg(graph,field,&info);
}
static void metadata(writer *model,const char *name,const char *value){uint8_t x[256];writer w={x,0,sizeof(x),0};str(&w,1,name);str(&w,2,value);msg(model,14,&w);}
cp_status cp_onnx_export_linear(const char *path,const cp_linear_model *m){
 if(!path||!m||m->features==0||m->features>CP_MAX_FEATURES||!isfinite(m->training_rmse)||m->training_rmse<0)return CP_INVALID;
 for(unsigned j=0;j<=CP_MAX_FEATURES;j++)if(!isfinite(m->beta[j])||(j>m->features&&m->beta[j]!=0))return CP_INVALID;
 uint8_t node_buf[1024],graph_buf[4096],model_buf[8192],opset_buf[256];
 writer nd={node_buf,0,sizeof(node_buf),0},g={graph_buf,0,sizeof(graph_buf),0},b={model_buf,0,sizeof(model_buf),0},op={opset_buf,0,sizeof(opset_buf),0};
 str(&nd,1,"X");str(&nd,1,"W");str(&nd,1,"B");str(&nd,2,"Y");str(&nd,3,"caelum_affine");str(&nd,4,"Gemm");
 msg(&g,1,&nd);str(&g,2,"caelum_linear_predictor");
 double weights[CP_MAX_FEATURES];for(uint64_t j=0;j<m->features;j++)weights[j]=m->beta[j+1];
 tensor(&g,"W",weights,m->features,1);tensor(&g,"B",m->beta,1,0);
 value_info(&g,11,"X",m->features,0);value_info(&g,12,"Y",m->features,1);
 var(&b,1,9);str(&b,2,"CAELUM");str(&b,3,"1.0");msg(&b,7,&g);
 var(&op,2,13);msg(&b,8,&op);
 char rows[48],rmse[64];(void)cp_snprintf_c(rows,sizeof(rows),"%" PRIu64,m->fitted_rows);(void)cp_snprintf_c(rmse,sizeof(rmse),"%.17g",m->training_rmse);
 metadata(&b,"caelum.fitted_rows",rows);metadata(&b,"caelum.training_rmse",rmse);
 if(b.err)return CP_RANGE;
 return cp_atomic_write(path,b.p,b.n);
}

typedef struct {const uint8_t *p;size_t n,at;int bad;} reader;
typedef struct {uint32_t field,wire;uint64_t val;reader sub;} item;
static uint64_t getvar(reader *r){uint64_t x=0;for(unsigned i=0;i<10;i++){if(r->at>=r->n){r->bad=1;return 0;}uint8_t c=r->p[r->at++];if(i==9&&c>1){r->bad=1;return 0;}x|=((uint64_t)(c&127))<<(i*7);if(!(c&128))return x;}r->bad=1;return 0;}
static int next(reader *r,item *v){
 if(r->bad||r->at>=r->n)return 0;
 memset(v,0,sizeof(*v));uint64_t k=getvar(r);if(r->bad||k>>3==0||k>>3>UINT32_C(0x1fffffff)){r->bad=1;return 0;}
 v->field=(uint32_t)(k>>3);v->wire=(uint32_t)(k&7);
 if(v->wire==0){v->val=getvar(r);}else if(v->wire==2){uint64_t n=getvar(r);if(r->bad||n>r->n-r->at){r->bad=1;return 0;}v->sub=(reader){r->p+r->at,(size_t)n,0,0};r->at+=(size_t)n;}
 else if(v->wire==1||v->wire==5){size_t z=v->wire==1?8:4;if(z>r->n-r->at){r->bad=1;return 0;}r->at+=z;}
 else{r->bad=1;return 0;}
 return !r->bad;
}
static int eq(reader r,const char *s){size_t n=strlen(s);return r.n==n&&memcmp(r.p,s,n)==0;}
static uint64_t ule64(const uint8_t *p){uint64_t v=0;for(int i=7;i>=0;i--)v=v*256+p[i];return v;}
typedef struct {char name[16];uint64_t dims[2],n_dims;double values[CP_MAX_FEATURES];size_t values_n;int dtype,raw; } tensor_info;
static int tensor_value(tensor_info *t,const uint8_t *p){
 if(t->values_n>=CP_MAX_FEATURES)return 0;
 uint64_t bits=ule64(p);double value;memcpy(&value,&bits,8);
 if(!isfinite(value))return 0;
 t->values[t->values_n++]=value;return 1;
}
static int read_tensor(reader r,tensor_info *t){
 item it;int named=0,storage=0;memset(t,0,sizeof(*t));
 while(next(&r,&it)){
  if(it.field==1){
   if(it.wire==0){if(t->n_dims>=2||it.val==0)return 0;t->dims[t->n_dims++]=it.val;}
   else if(it.wire==2){
    reader dims=it.sub;
    while(dims.at<dims.n){uint64_t n=getvar(&dims);if(dims.bad||!n||t->n_dims>=2)return 0;t->dims[t->n_dims++]=n;}
   }else return 0;
  }else if(it.field==2){if(it.wire!=0||t->dtype||it.val!=11)return 0;t->dtype=11;}
  else if(it.field==8){
   if(it.wire!=2||named++||it.sub.n==0||it.sub.n>15||memchr(it.sub.p,0,it.sub.n))return 0;
   memcpy(t->name,it.sub.p,it.sub.n);
  }else if(it.field==9){
   if(it.wire!=2||storage||it.sub.n==0||it.sub.n%8)return 0;
   storage=1;for(size_t j=0;j<it.sub.n;j+=8)if(!tensor_value(t,it.sub.p+j))return 0;
  }else if(it.field==10){
   if(storage==1)return 0;
   storage=2;
   if(it.wire==1){if(!tensor_value(t,r.p+r.at-8))return 0;}
   else if(it.wire==2){
    if(it.sub.n==0||it.sub.n%8)return 0;
    for(size_t j=0;j<it.sub.n;j+=8)if(!tensor_value(t,it.sub.p+j))return 0;
   }else return 0;
  }else if(it.field==12){if(it.wire!=2)return 0;} /* inert doc string */
  else if(it.field==14){if(it.wire!=0||it.val!=0)return 0;} /* DEFAULT only */
  else return 0; /* no segment, external data, mixed dtype or new semantics */
 }
 return !r.bad&&t->dtype==11&&storage&&named==1&&t->n_dims>0;
}
static int read_node(reader r){
 item it;int ix=0,oy=0,op=0;while(next(&r,&it)){
  if(it.field==1&&it.wire==2){const char *names[]={"X","W","B"};if(ix>=3||!eq(it.sub,names[ix++]))return 0;}
  else if(it.field==2&&it.wire==2){if(++oy!=1||!eq(it.sub,"Y"))return 0;}
  else if(it.field==4&&it.wire==2){if(++op!=1||!eq(it.sub,"Gemm"))return 0;}
  else if(it.field==5||it.field==7)return 0; /* attributes and custom domains unsafe */
 }
 return !r.bad&&ix==3&&oy==1&&op==1;
}
static int read_dimension(reader r,int axis,uint64_t *dimension){
 item d;int count=0;
 while(next(&r,&d)){
  if(++count!=1)return 0;
  if(axis==0){if(d.field!=2||d.wire!=2||!eq(d.sub,"N"))return 0;}
  else{if(d.field!=1||d.wire!=0||d.val==0)return 0;*dimension=d.val;}
 }
 return !r.bad&&count==1;
}
static int read_vi(reader r,const char *name,uint64_t *dim){
 item it;int named=0,typed=0;
 while(next(&r,&it)){
  if(it.field==1){if(it.wire!=2||named++||!eq(it.sub,name))return 0;}
  else if(it.field==2){
   if(it.wire!=2||typed++)return 0;
   reader type=it.sub;item tensor;if(!next(&type,&tensor)||tensor.field!=1||tensor.wire!=2||type.at!=type.n)return 0;
   reader rt=tensor.sub;item t;int elem=0,shape=0;
   while(next(&rt,&t)){
    if(t.field==1){if(t.wire!=0||elem++||t.val!=11)return 0;}
    else if(t.field==2){
     if(t.wire!=2||shape++)return 0;
     reader rs=t.sub;item axis;int count=0;
     while(next(&rs,&axis)){
      if(axis.field!=1||axis.wire!=2||count>=2||!read_dimension(axis.sub,count,dim))return 0;
      count++;
     }
     if(rs.bad||count!=2)return 0;
    }else return 0;
   }
   if(rt.bad||elem!=1||shape!=1)return 0;
  }else if(it.field!=3||it.wire!=2)return 0; /* doc string is inert */
 }
 return !r.bad&&named==1&&typed==1;
}
static int read_graph(reader r,cp_linear_model *m){
 item it;int nodes=0,weights=0,biases=0,inputs=0,outputs=0;
 uint64_t fd=0,od=0;tensor_info w={0},b={0};while(next(&r,&it)){
  if(it.field==1&&it.wire==2){if(++nodes>1||!read_node(it.sub))return 0;}
  else if(it.field==5&&it.wire==2){tensor_info t;if(!read_tensor(it.sub,&t))return 0;
   if(!strcmp(t.name,"W")){if(weights++)return 0;w=t;}
   else if(!strcmp(t.name,"B")){if(biases++)return 0;b=t;}else return 0;
  }
  else if(it.field==11&&it.wire==2){if(++inputs>1||!read_vi(it.sub,"X",&fd))return 0;}
  else if(it.field==12&&it.wire==2){if(++outputs>1||!read_vi(it.sub,"Y",&od))return 0;}
  else if(it.field==13||it.field==14||it.field==15)return 0; /* no value-info/or sparse initializers */
 }
 if(r.bad||nodes!=1||weights!=1||biases!=1||inputs!=1||outputs!=1||fd==0||fd>CP_MAX_FEATURES||od!=1)return 0;
 if(w.n_dims!=2||w.dims[0]!=fd||w.dims[1]!=1||w.values_n!=fd||b.n_dims!=1||b.dims[0]!=1||b.values_n!=1)return 0;
 m->features=fd;m->beta[0]=b.values[0];for(uint64_t i=0;i<fd;i++)m->beta[1+i]=w.values[i];return 1;
}
static int parse_meta(reader r,cp_linear_model *m,unsigned *seen){
 item it;reader keydata={0},vdata={0};unsigned keys=0,values=0;
 while(next(&r,&it)){
  if(it.field==1&&it.wire==2){if(keys++)return 0;keydata=it.sub;}
  else if(it.field==2&&it.wire==2){if(values++)return 0;vdata=it.sub;}
  else return 0;
 }
 if(r.bad||keys!=1||values!=1||!keydata.n||
    memchr(keydata.p,0,keydata.n)||memchr(vdata.p,0,vdata.n))return 0;
 char buf[128];if(vdata.n>=sizeof(buf))return 0;
 if(vdata.n)memcpy(buf,vdata.p,vdata.n);
 buf[vdata.n]='\0';
 if(eq(keydata,"caelum.fitted_rows")){
  if((*seen&1u)||!vdata.n)return 0;
  for(size_t i=0;i<vdata.n;i++)if(buf[i]<'0'||buf[i]>'9')return 0;
  char *end;errno=0;unsigned long long n=strtoull(buf,&end,10);
  if(errno||*end)return 0;
  m->fitted_rows=(uint64_t)n;*seen|=1u;
 }else if(eq(keydata,"caelum.training_rmse")){
  if((*seen&2u)||!vdata.n)return 0;
  char *end;errno=0;double d=cp_strtod_c(buf,&end);
  if(errno||end==buf||*end||!isfinite(d)||d<0)return 0;
  m->training_rmse=d;*seen|=2u;
 }
 return 1;
}
cp_status cp_onnx_import_linear(const char *path,cp_linear_model *out){
 if(!path||!out)return CP_INVALID;
 FILE *f=fopen(path,"rb");if(!f)return CP_IO;
 if(fseek(f,0,SEEK_END)){fclose(f);return CP_IO;}
 long size=ftell(f);
 if(size<0){fclose(f);return CP_IO;}
 if(size==0||size>1048576L){fclose(f);return CP_RANGE;}
 if(fseek(f,0,SEEK_SET)!=0){fclose(f);return CP_IO;}
 uint8_t *buf=malloc((size_t)size);if(!buf){fclose(f);return CP_NO_MEMORY;}
 size_t got=fread(buf,1,(size_t)size,f);int close=fclose(f);if(got!=(size_t)size||close){free(buf);return CP_IO;}
 reader r={buf,(size_t)size,0,0};item it;cp_linear_model m={0};int graph=0,version=0,opset=0;unsigned seen_metadata=0;
 while(next(&r,&it)){
  if(it.field==1&&it.wire==0){if(version++||it.val!=9){free(buf);return CP_UNSUPPORTED;}}
  else if(it.field==7&&it.wire==2){if(graph++||!read_graph(it.sub,&m)){free(buf);return CP_UNSUPPORTED;}}
  else if(it.field==8&&it.wire==2){reader a=it.sub;item z;int ver=0;while(next(&a,&z)){
   if(z.field==1&&z.wire==2&&z.sub.n){free(buf);return CP_UNSUPPORTED;}
   if(z.field==2&&z.wire==0&&z.val==13)ver=1;
   else if(z.field==2){free(buf);return CP_UNSUPPORTED;}
  }if(a.bad||!ver||opset++){free(buf);return CP_UNSUPPORTED;}}
  else if(it.field==14&&it.wire==2){if(!parse_meta(it.sub,&m,&seen_metadata)){free(buf);return CP_CORRUPT;}}
 }
 int ok=!r.bad&&version==1&&graph==1&&opset==1;
 free(buf);
 if(!ok)return CP_CORRUPT;
 *out=m;return CP_OK;
}
