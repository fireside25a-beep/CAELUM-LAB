#ifndef CAELUM_LAB_H
#define CAELUM_LAB_H
#include "caelum_science.h"
#ifdef __cplusplus
extern "C" {
#endif
#define CP_UNIT_AXES 7u
#define CP_EXPR_LIMIT 256u
#define CP_CAUSAL_LIMIT 4u
#define CP_HYPOTHESIS_LIMIT 8u
/* Least-squares diagnostics expose the original-unit residual and scales. */
typedef struct cp_fit_diagnostics {
 double relative_residual, response_scale, feature_scale[CP_MAX_FEATURES];
 double coefficient_norm;
 uint32_t algorithm_version, iterations;
} cp_fit_diagnostics;
CP_API cp_status cp_dataset_read_snapshot(const char *path,cp_dataset *out,uint8_t hash[32]);
CP_API cp_status cp_fit_linear_checked(cp_matrix_view x,const double *y,
 cp_linear_model *out,cp_fit_diagnostics *diagnostics);
CP_API cp_status cp_fit_ridge(cp_matrix_view x,const double *y,double lambda,cp_linear_model *out);
CP_API cp_status cp_fit_huber(cp_matrix_view x,const double *y,double delta,unsigned max_iterations,
 cp_linear_model *out,cp_fit_diagnostics *diagnostics);
CP_API cp_status cp_law_discover_v2(cp_matrix_view x,const double *y,unsigned degree,
 uint64_t split_seed,cp_law *out);
/* Philox4x32-10: full 128-bit counter and 64-bit key. */
CP_API void cp_philox4x32_10(const uint32_t counter[4],const uint32_t key[2],uint32_t out[4]);
CP_API uint64_t cp_random_v2_at(const cp_random_stream *stream,uint64_t counter);
CP_API cp_status cp_random_named(const cp_random_stream *root,const char *purpose,
 uint64_t epoch,cp_random_stream *out);
CP_API const char *cp_source_identity(void);
CP_API const char *cp_build_identity(void);
CP_API cp_status cp_lab_predict_export(const char *capsule,const char *basis,
 const double *raw,unsigned inputs,double *out);
typedef struct cp_unit { int8_t exponent[CP_UNIT_AXES]; } cp_unit;
typedef enum cp_role { CP_ROLE_INPUT=1,CP_ROLE_RESPONSE=2,CP_ROLE_TIME=3,CP_ROLE_TREATMENT=4 } cp_role;
typedef struct cp_variable {
 char name[32];cp_unit unit;double lower,upper;uint32_t role,controllable;
} cp_variable;
typedef enum cp_expr_op { CP_E_CONST=1,CP_E_VAR,CP_E_ADD,CP_E_SUB,CP_E_MUL,CP_E_DIV,CP_E_POW,CP_E_EXP,CP_E_LOG,CP_E_SIN,CP_E_COS } cp_expr_op;
typedef struct cp_expr_node {
 uint32_t op,left,right,variable;int32_t power;double value;cp_unit constant_unit;
} cp_expr_node;
typedef struct cp_expression {
 uint32_t inputs,count;cp_variable variables[CP_LAW_INPUT_LIMIT];cp_expr_node nodes[CP_EXPR_LIMIT];
} cp_expression;
CP_API cp_status cp_variable_validate(const cp_variable *variable);
CP_API cp_status cp_expression_unit(const cp_expression *expression,cp_unit *out);
CP_API cp_status cp_expression_evaluate(const cp_expression *expression,const double *x,double *value,double *gradient);
CP_API cp_status cp_expression_hash(const cp_expression *expression,uint8_t hash[32]);
CP_API cp_status cp_law_expression(const cp_law *law,const cp_variable *variables,cp_unit response,cp_expression *out);
/* Fully observed binary SCM: <=4 variables, acyclic parent masks, Bernoulli
   mechanism probabilities indexed by the global parent-bit assignment. */
typedef struct cp_binary_scm {
 uint32_t variables,parent_mask[CP_CAUSAL_LIMIT];
 double probability_one[CP_CAUSAL_LIMIT][1u<<CP_CAUSAL_LIMIT];
} cp_binary_scm;
typedef struct cp_causal_graph {
 uint32_t variables,directed[CP_CAUSAL_LIMIT],undirected[CP_CAUSAL_LIMIT];
 uint32_t conditional_tests,removed_edges,ambiguous;
} cp_causal_graph;
CP_API cp_status cp_scm_validate(const cp_binary_scm *model);
CP_API cp_status cp_scm_joint(const cp_binary_scm *model,uint32_t intervention_mask,uint32_t values,double out[1u<<CP_CAUSAL_LIMIT]);
CP_API cp_status cp_scm_effect(const cp_binary_scm *model,unsigned treatment,unsigned outcome,double *effect);
CP_API cp_status cp_binary_pc(const uint8_t *observations,size_t rows,unsigned variables,double alpha,cp_causal_graph *out);
/* Statistical standardization only: the caller must establish a valid
   backdoor adjustment set and positivity before interpreting this causally. */
CP_API cp_status cp_causal_adjustment(const double *joint,unsigned variables,unsigned treatment,unsigned outcome,uint32_t adjust_mask,double *effect);
CP_API cp_status cp_finite_eig(const cp_binary_scm *models,const double *weights,unsigned count,unsigned treatment,unsigned outcome,unsigned value,double *utility);
/* Finite-sample split-conformal radius for a model fixed independently of
   these absolute calibration residuals. Coverage requires exchangeability. */
CP_API cp_status cp_conformal_radius(const double *absolute_residuals,size_t count,double alpha,double *radius);
typedef enum cp_run_phase { CP_RUN_CREATED=0,CP_RUN_READY=1,CP_RUN_RUNNING=2,CP_RUN_STOPPED=3,CP_RUN_SEALED=4,CP_RUN_COUNTEREXAMPLE=5 } cp_run_phase;
typedef struct cp_run_summary {
 uint32_t schema,phase,steps,model_count,rejected_models,causal_status;
 double cost,holdout_rmse,predictive_entropy,effect,effect_lower,effect_upper,coverage,interval_radius;
 uint64_t seed;
} cp_run_summary;
/* Only explicitly imported measurements are consumed. A step limit checkpoints
   the same experiment rather than silently altering its configured budget. */
CP_API cp_status cp_lab_run(const char *protocol,const char *bundle,unsigned step_limit,int resume,cp_run_summary *out);
CP_API cp_status cp_lab_run_replay(const char *bundle);
typedef struct cp_run_point {uint32_t row;double prediction,observed,model_variance;} cp_run_point;
CP_API cp_status cp_lab_run_point(const char *bundle,unsigned index,cp_run_point *out);
CP_API cp_status cp_lab_run_summary(const char *bundle,cp_run_summary *out);
#ifdef __cplusplus
}
#endif
#endif
