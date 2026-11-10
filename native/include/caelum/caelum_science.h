#ifndef CAELUM_SCIENCE_H
#define CAELUM_SCIENCE_H
/* CAELUM Experiment A native science contracts. v1.2; algorithms are owned C11. */
#include "caelum_port.h"
#ifdef __cplusplus
extern "C" {
#endif
#define CP_LAW_INPUT_LIMIT 3u
#define CP_LAW_TERM_LIMIT 16u
#define CP_LAB_RECEIPT_LIMIT 8192u

typedef struct cp_random_stream {
 uint64_t seed, stream;
 uint32_t source; /* 0=deterministic, 1=recorded, 2=rdseed, 3=rdrand */
 uint32_t hardware_attempts;
} cp_random_stream;
/* For deterministic and recorded, seed is caller supplied and is never replaced.
   For rdseed/rdrand, seed is obtained from real feature-gated native instructions.
   Counter addressing allows order-independent exact replay. Not cryptographic. */
CP_API cp_status cp_random_init(cp_random_stream *out, const char *source,
                                uint64_t seed, uint64_t stream);
CP_API uint64_t cp_random_at(const cp_random_stream *r, uint64_t counter);
CP_API double cp_random_unit(const cp_random_stream *r, uint64_t counter);

typedef struct cp_law {
 uint32_t inputs, degree, term_count;
 uint8_t powers[CP_LAW_TERM_LIMIT][CP_LAW_INPUT_LIMIT];
 double coefficients[CP_LAW_TERM_LIMIT];
 double train_rmse, holdout_rmse, training_bic;
 uint64_t train_rows, holdout_rows, split_seed;
 /* Actual search audit, never a fabricated theoretical search count. */
 uint32_t attempted_candidates, rank_rejections, selection_steps;
 uint32_t selection_basis_ids[CP_LAW_TERM_LIMIT];
 double selection_bic[CP_LAW_TERM_LIMIT];
} cp_law;
/* Fits sparse polynomial terms by forward BIC selection, with a sealed
   seed-defined 20% holdout that is never consulted during model selection.
   Inputs are features, y is the measured response, NOT a causal identity proof. */
CP_API cp_status cp_law_discover(cp_matrix_view x, const double *y,
                                 unsigned degree, uint64_t split_seed,
                                 cp_law *out);
CP_API cp_status cp_law_predict(const cp_law *law, const double *x, double *out);
/* Exports into the existing native linear capsule representation, operating on
   transformed polynomial basis features rather than original raw inputs. */
CP_API cp_status cp_law_as_linear(const cp_law *law, cp_linear_model *out);
/* Basis output commits only on success; in-place input/output is supported. */
CP_API cp_status cp_law_basis(const cp_law *law, const double *x, double *out);

typedef struct cp_intervention_design {
 uint32_t inputs, grid_points;
 double lower[CP_LAW_INPUT_LIMIT], upper[CP_LAW_INPUT_LIMIT];
} cp_intervention_design;
typedef struct cp_intervention_choice {
 double x[CP_LAW_INPUT_LIMIT], prediction_a, prediction_b, disagreement;
 uint64_t enumerated;
} cp_intervention_choice;
/* Exact bounded-grid argmax of predictive disagreement. Planning only:
   the caller performs and independently records any real intervention. */
CP_API cp_status cp_design_disagreement(const cp_law *a, const cp_law *b,
  const cp_intervention_design *grid, cp_intervention_choice *out);

/* State-trajectory law recovery: time must strictly increase and samples
   are ordered. Central finite differences are compared with an independent
   blocked future holdout; derivative bias is O(dt^2) on smooth trajectories. */
CP_API cp_status cp_dynamics_discover(cp_matrix_view states,
  const double *times, unsigned degree, uint64_t seed,
  cp_law out_laws[CP_LAW_INPUT_LIMIT]);
/* Implicit midpoint: reversible numerical step for a supplied continuous-time
   vector field. No claim that a learned field is exact, physically reversible,
   or that noisy measurements can be reconstructed. A failure leaves output
   untouched; a positive or negative finite nonzero dt is required.
   Each step must also pass reverse and forward-repeat solves within 256*DBL_EPSILON
   times each component's local trajectory scale (initial, candidate, recovered),
   floored at DBL_MIN. No other coordinate contributes to its tolerance. Ambiguous/unstable
   branches are refused, without claiming global uniqueness of polynomial roots. */
CP_API cp_status cp_dynamics_step_midpoint(const cp_law *laws,unsigned dimensions,
  const double *initial,double dt,double *next);
/* Each signed dt[i] corresponds to one step. Output has (steps+1)*dimensions
   values and is modified only when the complete trajectory succeeds. */
CP_API cp_status cp_dynamics_trace_midpoint(const cp_law *laws,unsigned dimensions,
  const double *initial,const double *dt,size_t steps,double *trace);
typedef struct cp_dynamics_audit {
 uint64_t anchor_row,forecast_steps;
 double forward_rmse,backward_observed_rmse;
 double cycle_rmse,cycle_max_absolute_error;
} cp_dynamics_audit;
/* Strict future block: the fitted laws must have been obtained independently.
   Forward error = predictive validity; reverse-from-observed = inverse model
   error; forward-then-back = integrator's numerical reversibility only. */
CP_API cp_status cp_dynamics_audit_holdout(const cp_law *laws,
  cp_matrix_view states,const double *times,cp_dynamics_audit *out);
/* Holm step-down adjusted p-values; nonoverlapping input/output buffers.
   Monotone and familywise-controlled when
   individual input p-values are valid for the underlying hypotheses. */
CP_API cp_status cp_holm_adjust(const double *pvalues, size_t n,
                               double *adjusted);
typedef struct cp_effect_estimate {
 uint64_t control_n, treated_n;
 double control_mean, treated_mean, difference, standard_error;
 double ci95_lower, ci95_upper;
} cp_effect_estimate;
/* Requires an explicit independently randomized-assignment declaration;
   rejects observational-only labels. Z intervals are approximate, not small
   sample exact or a general causal-identification theorem. */
CP_API cp_status cp_randomized_effect(const uint8_t *treatment,
  const double *outcomes, uint64_t n, int randomized_assignment,
  cp_effect_estimate *out);
/* Deterministic evidence: the source CSV SHA256, model, split and score are
   represented by canonical text, then SHA256 sealed. Not a digital signature. */
CP_API cp_status cp_lab_discover_file(const char *csv, unsigned degree,
  const cp_random_stream *random, const char *receipt);
CP_API cp_status cp_lab_replay_file(const char *csv, const char *receipt);
CP_API cp_status cp_lab_receipt_law(const char *csv, const char *receipt,
  cp_law *out);
/* Coupled, recoverable model/basis commit; basis binds model/receipt/data hashes. */
CP_API cp_status cp_lab_export_file(const char *csv,const char *receipt,
 const char *capsule,const char *basis);
#ifdef __cplusplus
}
#endif
#endif
