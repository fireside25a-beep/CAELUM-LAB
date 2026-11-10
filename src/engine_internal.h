#ifndef CP_ENGINE_INTERNAL_H
#define CP_ENGINE_INTERNAL_H
#include "caelum_lab.h"
#define CP_RUN_ACTIONS 256u
#define CP_RUN_POOL 4096u
#define CP_RUN_EVENTS 131072u
#define CP_RUN_PATH 4096u
typedef struct cp_run_config {
 uint32_t schema,inputs,family,policy,max_actions,interventions,fully_observed,iid;
 unsigned model_count;double max_cost,noise,cost_per_action,costs[CP_RUN_POOL],priors[8];unsigned costs_count;
 cp_variable variables[4];cp_binary_scm hypotheses[8];cp_random_stream root_rng,rng;
 char observations[CP_RUN_PATH],pool[CP_RUN_PATH],holdout[CP_RUN_PATH],calibration[CP_RUN_PATH];
 uint8_t protocol_hash[32];
} cp_run_config;
typedef struct cp_run_state {
 char magic[8],source[65],build[65];cp_run_config config;
 cp_run_summary summary;cp_law laws[3];uint32_t active[8],model_count;
 double log_weight[8],weights[8],cost;
 uint8_t used[CP_RUN_POOL];uint32_t chosen[CP_RUN_ACTIONS];
 double predicted[CP_RUN_ACTIONS],observed[CP_RUN_ACTIONS],variance[CP_RUN_ACTIONS];
 uint8_t observations_hash[32],pool_hash[32],holdout_hash[32],calibration_hash[32];
 uint32_t event_count,event_bytes;char events[CP_RUN_EVENTS];uint8_t digest[32];
} cp_run_state;
cp_status cp_protocol_read(const char *path,cp_run_config *out);
cp_status cp_engine_from_state(cp_run_state *state,const char *bundle,unsigned limit);
#endif
