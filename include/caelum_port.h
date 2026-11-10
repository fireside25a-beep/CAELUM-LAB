#ifndef CAELUM_PORT_H
#define CAELUM_PORT_H
/* CAELUM native porting SDK v1. All declared functions have concrete implementations. */
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32)
# if defined(CP_BUILD_SHARED)
#  define CP_API __declspec(dllexport)
# else
#  define CP_API __declspec(dllimport)
# endif
#else
# define CP_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define CP_ABI_MAJOR 1u
#define CP_ABI_MINOR 3u
#define CP_MAX_FEATURES 16u
#define CP_MAX_COEFFICIENTS (CP_MAX_FEATURES+1u)
#define CP_CAPSULE_BYTES 200u

typedef enum cp_status {
 CP_OK=0, CP_INVALID=1, CP_RANGE=2, CP_NUMERIC=3, CP_RANK_DEFICIENT=4,
 CP_IO=5, CP_CORRUPT=6, CP_UNSUPPORTED=7, CP_UNAVAILABLE=8, CP_ABI_MISMATCH=9,
 CP_PLUGIN_ERROR=10, CP_NO_MEMORY=11
} cp_status;

typedef struct cp_matrix_view {
 const double *values; uint64_t rows, columns, stride; /* stride in doubles, row-major */
} cp_matrix_view;
typedef struct cp_dataset {
 double *x; double *y; uint64_t rows, features; /* owned buffers, free with cp_dataset_release */
} cp_dataset;
typedef struct cp_linear_model {
 uint64_t features, fitted_rows;
 double beta[CP_MAX_COEFFICIENTS]; /* beta[0] intercept */
 double training_rmse;
} cp_linear_model;
typedef struct cp_cpu_report {
 uint32_t arch_x86_64, has_rdrand, has_rdseed, has_rdtscp;
 uint32_t has_osxsave, has_avx, has_invariant_tsc, has_sse2;
 uint32_t max_basic_leaf, max_extended_leaf;
} cp_cpu_report;

typedef struct cp_adapter_v1 {
 uint32_t abi_major, abi_minor;
 uint32_t struct_bytes, model_kind; /* 1 = dense real linear predictor */
 const char *name;
 cp_status (*read_model)(const char *path, cp_linear_model *model);
 cp_status (*write_model)(const char *path, const cp_linear_model *model);
} cp_adapter_v1;
typedef const cp_adapter_v1 *(*cp_get_adapter_v1_fn)(void);
typedef struct cp_adapter_handle {
 void *library_handle;
 const cp_adapter_v1 *api;
} cp_adapter_handle;

CP_API const char *cp_status_string(cp_status status);
CP_API uint32_t cp_abi_version(void);
CP_API cp_status cp_fit_linear_qr(cp_matrix_view x, const double *y, cp_linear_model *out);
CP_API cp_status cp_predict_linear(const cp_linear_model *model, const double *features, double *out);
CP_API cp_status cp_dataset_read_csv(const char *path, cp_dataset *out);
CP_API cp_status cp_dataset_write_csv(const char *path, const cp_dataset *data);
CP_API void cp_dataset_release(cp_dataset *data);
/* Atomic replacement for owned binary/text artifacts; <=64 MiB. A caller
   must validate semantic/path authorization before invoking this IO primitive. */
CP_API cp_status cp_write_atomic_file(const char *path,const uint8_t *data,size_t bytes);
CP_API cp_status cp_capsule_save(const char *path, const cp_linear_model *model);
CP_API cp_status cp_capsule_load(const char *path, cp_linear_model *model);
/* Bidirectional ONNX IR 9 / ai.onnx opset 13 constrained Gemm model adapter.
   Unsupported graphs are rejected, never silently evaluated or approximated. */
CP_API cp_status cp_onnx_export_linear(const char *path, const cp_linear_model *model);
CP_API cp_status cp_onnx_import_linear(const char *path, cp_linear_model *model);
CP_API void cp_sha256(const uint8_t *data, size_t bytes, uint8_t out[32]);
CP_API cp_status cp_adapter_open(const char *path, cp_adapter_handle *out);
CP_API void cp_adapter_close(cp_adapter_handle *adapter);
CP_API cp_status cp_cpu_query(cp_cpu_report *out);
CP_API cp_status cp_native_word(const char *source, uint64_t *out, unsigned *attempts);
CP_API cp_status cp_native_rip(uintptr_t *out); /* address of native instruction, not a random source */
CP_API cp_status cp_native_tsc(uint64_t *out, uint32_t *aux); /* telemetry only */
CP_API cp_status cp_native_rflags(uint64_t *out); /* architectural status snapshot, not entropy */
CP_API cp_status cp_native_xcr0(uint64_t *out); /* guarded XGETBV; OS-managed state, not entropy */
#ifdef __cplusplus
}
#endif
#endif
