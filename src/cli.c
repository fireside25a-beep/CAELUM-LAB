#include "caelum_port.h"
#include "caelum_science.h"
#include "caelum_lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <errno.h>
#include "file_identity.h"
static int error(const char *action,cp_status st){fprintf(stderr,"FAIL %s: %s\n",action,cp_status_string(st));return (int)st? (int)st:1;}
static void usage(void){
 puts("CAELUM native scientific laboratory / SDK 0.4.0 (ABI 1.3)\n"
      "  caelum-port lab-run <protocol.json> <new-bundle> [step-limit]\n"
      "  caelum-port lab-resume <bundle> [step-limit]\n"
      "  caelum-port lab-run-replay <bundle>\n"
      "  caelum-port lab-status <bundle>\n"
      "  caelum-port lab-predict-export <model.cpm> <basis.txt> <raw-inputs...>\n"
      "  caelum-port probe\n"
      "  caelum-port seed rdseed|rdrand\n"
      "  caelum-port lab-discover <numeric.csv> <degree:1..3> <hex-seed|rdseed|rdrand> <receipt.cpr>\n"
      "  caelum-port lab-replay <numeric.csv> <receipt.cpr>\n"
      "  caelum-port lab-export <numeric.csv> <receipt.cpr> <model.cpm> <basis.txt>\n"
      "  caelum-port lab-dynamics <states_and_time.csv> <degree> <hex-seed>\n"
      "  caelum-port lab-effect <treatment_outcome.csv> --randomized\n"
      "  caelum-port lab-disagree <csvA> <receiptA> <csvB> <receiptB> <grid> <low0> <high0> [<low1> <high1> ...]\n"
      "  caelum-port fit <numeric.csv> <model.cpm>\n"
      "  caelum-port inspect <model.cpm>\n"
      "  caelum-port onnx-export <model.cpm> <model.onnx>\n"
      "  caelum-port onnx-import <model.onnx> <model.cpm>\n"
      "  caelum-port predict <model.cpm> <feature-1> [... feature-N]\n"
      "  caelum-port export <adapter-library> <model.cpm> <external-model>\n"
      "  caelum-port import <adapter-library> <external-model> <model.cpm>\n"
      "CSV has no header, final column is y. Model format: hashed binary v1.");
}
static void show(const cp_linear_model *m){
 printf("features=%" PRIu64 " rows=%" PRIu64 " train_rmse=%.17g\n",m->features,m->fitted_rows,m->training_rmse);
 for(uint64_t j=0;j<=m->features;j++)printf("beta[%" PRIu64 "]=%.17g\n",j,m->beta[j]);
}
static int read_hex_seed(const char *text,uint64_t *out){
 if(!text||!out)return 0;
 if(text[0]=='0'&&(text[1]=='x'||text[1]=='X'))text+=2;
 size_t length=strlen(text);if(!length||length>16)return 0;
 for(size_t i=0;i<length;i++)if(!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')||(text[i]>='A'&&text[i]<='F')))return 0;
 errno=0;char *end=NULL;unsigned long long value=strtoull(text,&end,16);if(errno||!end||*end)return 0;
 *out=(uint64_t)value;return 1;
}
static int read_finite(const char *source,double *v){
 if(!source||!v)return 0;
 errno=0;char *end=NULL;double d=strtod(source,&end);
 if(errno||end==source||*end||!isfinite(d))return 0;
 *v=d;return 1;
}
static void print_law(const cp_law *m){
 printf("model inputs=%u degree=%u terms=%u train_rows=%" PRIu64 " holdout_rows=%" PRIu64
        " train_rmse=%.17g holdout_rmse=%.17g bic=%.17g\n",
  m->inputs,m->degree,m->term_count,m->train_rows,m->holdout_rows,
  m->train_rmse,m->holdout_rmse,m->training_bic);
 printf("candidate_evaluations=%u rank_rejections=%u selection_steps=%u\n",
   m->attempted_candidates,m->rank_rejections,m->selection_steps);
 for(unsigned k=0;k<m->term_count;k++)printf("term.%u powers=%u,%u,%u coefficient=%.17g\n",k,
  (unsigned)m->powers[k][0],(unsigned)m->powers[k][1],
  (unsigned)m->powers[k][2],m->coefficients[k]);
}
/* Refuse overwriting any source, receipt or adapter by any output alias.
   This is a local file identity check, not a hostile-filesystem race sandbox. */
static int outputs_conflict(int argc,char **argv){
 int paths[4]={0},output[4]={0};unsigned count=0;
 if(argc==6&&!strcmp(argv[1],"lab-export")){
  count=4;for(unsigned i=0;i<count;i++)paths[i]=(int)i+2;
  output[2]=output[3]=1;
 }else if(argc==6&&!strcmp(argv[1],"lab-discover")){
  count=2;paths[0]=2;paths[1]=5;output[1]=1;
 }else if(argc==4&&(!strcmp(argv[1],"fit")||!strcmp(argv[1],"onnx-import")||!strcmp(argv[1],"onnx-export"))){
  count=2;paths[0]=2;paths[1]=3;output[1]=1;
 }else if(argc==5&&(!strcmp(argv[1],"import")||!strcmp(argv[1],"export"))){
  count=3;paths[0]=2;paths[1]=3;paths[2]=4;output[2]=1;
 }
 for(unsigned i=0;i<count;i++)for(unsigned j=i+1;j<count;j++)
  if((output[i]||output[j])&&cp_same_file(argv[paths[i]],argv[paths[j]]))return 1;
 return 0;
}
int main(int argc,char **argv){
 if(outputs_conflict(argc,argv))return error("output aliases an input or another output",CP_INVALID);

 if(argc>=2&&(!strcmp(argv[1],"lab-run")||!strcmp(argv[1],"lab-resume"))){
  int resume=!strcmp(argv[1],"lab-resume");unsigned limit=0;
  if(argc!=(resume?3:4)&&argc!=(resume?4:5)){usage();return 2;}
  if(argc==(resume?4:5)){char *end=NULL;errno=0;unsigned long n=strtoul(argv[argc-1],&end,10);if(errno||!argv[argc-1][0]||*end||n>256)return 2;limit=(unsigned)n;}
  cp_run_summary summary={0};cp_status st=cp_lab_run(resume?NULL:argv[2],resume?argv[2]:argv[3],limit,resume,&summary);
  if(st==CP_OK)printf("phase=%u steps=%u cost=%.17g holdout_rmse=%.17g\n",summary.phase,summary.steps,summary.cost,summary.holdout_rmse);
  else fprintf(stderr,"lab-run: %s\n",cp_status_string(st));
  return st==CP_OK?0:1;
 }
 if(argc==3&&!strcmp(argv[1],"lab-run-replay")){
  cp_status st=cp_lab_run_replay(argv[2]);printf("lab-run-replay: %s\n",cp_status_string(st));return st==CP_OK?0:1;
 }
 if(argc==3&&!strcmp(argv[1],"lab-status")){
  cp_run_summary summary={0};cp_status st=cp_lab_run_summary(argv[2],&summary);
  if(st==CP_OK)printf("phase=%u steps=%u cost=%.17g holdout_rmse=%.17g\n",summary.phase,summary.steps,summary.cost,summary.holdout_rmse);
  else fprintf(stderr,"lab-status: %s\n",cp_status_string(st));
  return st==CP_OK?0:1;
 }
 if(argc==6&&!strcmp(argv[1],"lab-discover")){
  char *tail=NULL;errno=0;unsigned long degree=strtoul(argv[3],&tail,10);
  if(errno||!tail||*tail||degree<1||degree>3)return error("invalid polynomial degree",CP_INVALID);
  cp_random_stream r={0};cp_status st;
  if(!strcmp(argv[4],"rdseed")||!strcmp(argv[4],"rdrand")){
   st=cp_random_init(&r,argv[4],0,UINT64_C(0x6361656c756d2d41));
  }else{
   uint64_t seed=0;if(!read_hex_seed(argv[4],&seed))return error("invalid hex seed",CP_INVALID);
   st=cp_random_init(&r,"deterministic",(uint64_t)seed,UINT64_C(0x6361656c756d2d41));
  }
  if(st!=CP_OK)return error("entropy source",st);
  st=cp_lab_discover_file(argv[2],(unsigned)degree,&r,argv[5]);
  if(st!=CP_OK)return error("scientific discovery",st);
  printf("lab=experiment_a source=%s seed=%016" PRIx64 " attempts=%u receipt=%s\n",
   argv[4],r.seed,r.hardware_attempts,argv[5]);return 0;
 }
 if(argc==4&&!strcmp(argv[1],"lab-replay")){
  cp_status st=cp_lab_replay_file(argv[2],argv[3]);
  if(st!=CP_OK)return error("experiment replay mismatch",st);
  puts("experiment_a_replay=byte_exact_sha256_and_source_rebuild_verified");return 0;
 }
 if(argc==6&&!strcmp(argv[1],"lab-export")){
  cp_status st=cp_lab_export_file(argv[2],argv[3],argv[4],argv[5]);
  if(st!=CP_OK)return error("bound model/basis export",st);
  puts("law_export=hash_bound_transformed_basis transaction=committed");return 0;
 }
 if(argc>=5&&!strcmp(argv[1],"lab-predict-export")){
  unsigned count=(unsigned)(argc-4);if(count>3)return error("raw dimension",CP_INVALID);
  double raw[3]={0},prediction=0;for(unsigned j=0;j<count;j++)if(!read_finite(argv[4+j],raw+j))return error("raw feature",CP_INVALID);
  cp_status status=cp_lab_predict_export(argv[2],argv[3],raw,count,&prediction);
  if(status!=CP_OK)return error("bound raw-input prediction",status);
  printf("prediction=%.17g basis=hash_verified\n",prediction);return 0;
 }
 if(argc==5&&!strcmp(argv[1],"lab-dynamics")){
  errno=0;char *end=NULL;unsigned long degree=strtoul(argv[3],&end,10);
  if(errno||!argv[3][0]||*end||degree<1||degree>3)return error("invalid degree",CP_INVALID);
  uint64_t seed=0;if(!read_hex_seed(argv[4],&seed))return error("invalid seed",CP_INVALID);
  cp_dataset ds={0};cp_status st=cp_dataset_read_csv(argv[2],&ds);
  if(st!=CP_OK)return error("load trajectory",st);
  cp_matrix_view states={ds.x,ds.rows,ds.features,ds.features};
  cp_law law[CP_LAW_INPUT_LIMIT]={{0}};
  st=cp_dynamics_discover(states,ds.y,(unsigned)degree,(uint64_t)seed,law);
  if(st==CP_OK){
   puts("trajectory_results=central_difference_with_strict_future_block; not causal identification");
   for(unsigned j=0;j<ds.features;j++){printf("derivative_target=%u\n",j);print_law(&law[j]);}
   cp_dynamics_audit audit={0};
   st=cp_dynamics_audit_holdout(law,states,ds.y,&audit);
   if(st==CP_OK)printf("future_block_anchor=%" PRIu64 " forecast_steps=%" PRIu64
    " forward_rmse=%.17g reverse_observed_rmse=%.17g"
    " solver_cycle_rmse=%.17g solver_cycle_max_abs=%.17g\n",
    audit.anchor_row,audit.forecast_steps,audit.forward_rmse,
    audit.backward_observed_rmse,audit.cycle_rmse,audit.cycle_max_absolute_error);
  }
  cp_dataset_release(&ds);return st==CP_OK?0:error("trajectory discovery",st);
 }
 if(argc==4&&!strcmp(argv[1],"lab-effect")){
  if(strcmp(argv[3],"--randomized"))return error("randomized assignment declaration required",CP_INVALID);
  cp_dataset d={0};cp_status st=cp_dataset_read_csv(argv[2],&d);
  if(st!=CP_OK)return error("load controlled experiment",st);
  if(d.features!=1){cp_dataset_release(&d);return error("trial requires one treatment column",CP_INVALID);}
  uint8_t *trt=malloc((size_t)d.rows);
  if(!trt){cp_dataset_release(&d);return error("trial allocation",CP_NO_MEMORY);}
  int good=1;
  for(uint64_t j=0;j<d.rows;j++){
   if(d.x[j]!=0.0&&d.x[j]!=1.0){good=0;break;}
   trt[j]=(uint8_t)d.x[j];
  }
  if(!good)st=CP_INVALID;
  cp_effect_estimate e={0};
  if(st==CP_OK)st=cp_randomized_effect(trt,d.y,d.rows,1,&e);
  free(trt);cp_dataset_release(&d);
  if(st!=CP_OK)return error("randomized treatment effect",st);
  printf("assignment=declared_randomized (not independently verified) control_n=%" PRIu64
         " treated_n=%" PRIu64 " effect=%.17g standard_error=%.17g ci95=[%.17g,%.17g]\n",
    e.control_n,e.treated_n,e.difference,e.standard_error,e.ci95_lower,e.ci95_upper);
  return 0;
 }
 if(argc>=9&&!strcmp(argv[1],"lab-disagree")){
  cp_law a={0},b={0};cp_status st=cp_lab_receipt_law(argv[2],argv[3],&a);
  if(st==CP_OK)st=cp_lab_receipt_law(argv[4],argv[5],&b);
  if(st!=CP_OK)return error("read and reverify two scientific receipts",st);
  if(a.inputs!=b.inputs||argc!=(int)(7+2*a.inputs))return error("domain dimension/arguments",CP_INVALID);
  char *end=NULL;errno=0;unsigned long resolution=strtoul(argv[6],&end,10);
  if(errno||!argv[6][0]||*end||resolution<2||resolution>21)return error("grid",CP_INVALID);
  cp_intervention_design grid={0};grid.inputs=a.inputs;grid.grid_points=(uint32_t)resolution;
  for(unsigned j=0;j<a.inputs;j++)if(!read_finite(argv[7+j*2],&grid.lower[j])||
    !read_finite(argv[8+j*2],&grid.upper[j]))return error("invalid domain",CP_INVALID);
  cp_intervention_choice choice={0};st=cp_design_disagreement(&a,&b,&grid,&choice);
  if(st!=CP_OK)return error("distinguishable intervention",st);
  printf("kind=proposed_only enumerated=%" PRIu64 " maximum_disagreement=%.17g modelA=%.17g modelB=%.17g",
   choice.enumerated,choice.disagreement,choice.prediction_a,choice.prediction_b);
  for(unsigned j=0;j<a.inputs;j++)printf(" x%u=%.17g",j,choice.x[j]);
  puts("");return 0;
 }
 if(argc==2&&!strcmp(argv[1],"probe")){
  cp_cpu_report r;cp_status s=cp_cpu_query(&r);
  if(s==CP_UNSUPPORTED){puts("platform=non-x86-64 native entropy and x86 register probe unsupported; no fallback");return 0;}
  if(s!=CP_OK)return error("cpuid",s);
  printf("abi=%u arch_x86_64=%u rdrand=%u rdseed=%u rdtscp=%u osxsave=%u avx=%u invariant_tsc=%u max_leaf=%u extended_leaf=%u\n",cp_abi_version(),r.arch_x86_64,r.has_rdrand,r.has_rdseed,r.has_rdtscp,r.has_osxsave,r.has_avx,r.has_invariant_tsc,r.max_basic_leaf,r.max_extended_leaf);
  uintptr_t rip=0;s=cp_native_rip(&rip);if(s==CP_OK)printf("rip_native_instruction_address=0x%" PRIxPTR " (PIE/ASLR-dependent, not entropy)\n",rip);
  else printf("rip_status=%s\n",cp_status_string(s));
  uint64_t ticks=0;uint32_t aux=0;s=cp_native_tsc(&ticks,&aux);
  if(s==CP_OK)printf("rdtscp_ticks=%" PRIu64 " aux=%u (telemetry, not entropy)\n",ticks,aux);
  else printf("rdtscp_status=%s\n",cp_status_string(s));
  uint64_t flags=0;s=cp_native_rflags(&flags);
  if(s==CP_OK)printf("rflags=0x%016" PRIx64 " (status, not entropy)\n",flags);
  else printf("rflags_status=%s\n",cp_status_string(s));
  uint64_t xcr0=0;s=cp_native_xcr0(&xcr0);
  if(s==CP_OK)printf("xcr0=0x%016" PRIx64 " (OS-enabled extended state)\n",xcr0);
  else printf("xcr0_status=%s\n",cp_status_string(s));
  return 0;
 }
 if(argc==3&&!strcmp(argv[1],"seed")){
  uint64_t word=0;unsigned attempts=0;cp_status s=cp_native_word(argv[2],&word,&attempts);
  if(s!=CP_OK)return error("native entropy instruction",s);
  printf("source=%s value=0x%016" PRIx64 " attempts=%u\n",argv[2],word,attempts);return 0;
 }
 if(argc==4&&!strcmp(argv[1],"fit")){
  cp_dataset d={0};cp_status s=cp_dataset_read_csv(argv[2],&d);if(s!=CP_OK)return error("load numeric table",s);
  cp_matrix_view x={d.x,d.rows,d.features,d.features};cp_linear_model m={0};s=cp_fit_linear_qr(x,d.y,&m);cp_dataset_release(&d);
  if(s!=CP_OK)return error("householder fitting",s);
  s=cp_capsule_save(argv[3],&m);if(s!=CP_OK)return error("write capsule",s);
  cp_linear_model b={0};s=cp_capsule_load(argv[3],&b);if(s!=CP_OK)return error("readback",s);
  if(memcmp(&m,&b,sizeof(m))!=0)return error("readback mismatch",CP_CORRUPT);
  show(&b);puts("capsule_roundtrip=verified");return 0;
 }
 if(argc==4&&(!strcmp(argv[1],"onnx-export")||!strcmp(argv[1],"onnx-import"))){
  cp_linear_model m={0};cp_status s;
  int out=!strcmp(argv[1],"onnx-export");
  if(out){s=cp_capsule_load(argv[2],&m);if(s==CP_OK)s=cp_onnx_export_linear(argv[3],&m);}
  else{s=cp_onnx_import_linear(argv[2],&m);if(s==CP_OK)s=cp_capsule_save(argv[3],&m);}
  if(s!=CP_OK)return error("ONNX conversion",s);
  printf("onnx_direction=%s model_kind=double_affine features=%" PRIu64 " status=ok\n",out?"export":"import",m.features);
  return 0;
 }
 if(argc==3&&!strcmp(argv[1],"inspect")){
  cp_linear_model m={0};cp_status s=cp_capsule_load(argv[2],&m);if(s!=CP_OK)return error("inspect capsule",s);
  show(&m);return 0;
 }
 if(argc>=4&&!strcmp(argv[1],"predict")){
  cp_linear_model m={0};cp_status s=cp_capsule_load(argv[2],&m);if(s!=CP_OK)return error("read capsule",s);
  if((uint64_t)(argc-3)!=m.features)return error("feature count",CP_INVALID);
  double x[CP_MAX_FEATURES];for(uint64_t j=0;j<m.features;j++){
   char *end;errno=0;x[j]=strtod(argv[3+j],&end);
   if(errno||end==argv[3+j]||*end||!isfinite(x[j]))return error("invalid feature",CP_INVALID);
  }
  double prediction;s=cp_predict_linear(&m,x,&prediction);if(s!=CP_OK)return error("predict",s);
  printf("prediction=%.17g\n",prediction);return 0;
 }
 if(argc==5&&(!strcmp(argv[1],"export")||!strcmp(argv[1],"import"))){
  int exporting=!strcmp(argv[1],"export");cp_adapter_handle a={0};cp_status s=cp_adapter_open(argv[2],&a);
  if(s!=CP_OK)return error("load adapter",s);
  cp_linear_model m={0};
  if(exporting){s=cp_capsule_load(argv[3],&m);if(s==CP_OK)s=a.api->write_model(argv[4],&m);}
  else{s=a.api->read_model(argv[3],&m);if(s==CP_OK)s=cp_capsule_save(argv[4],&m);}
  if(s==CP_OK)printf("direction=%s adapter=%s features=%" PRIu64 " status=ok\n",exporting?"out":"in",a.api->name,m.features);
  cp_adapter_close(&a);return s==CP_OK?0:error("adapter exchange",s);
 }
 usage();return 2;
}
