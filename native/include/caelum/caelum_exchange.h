#ifndef CAELUM_EXCHANGE_H
#define CAELUM_EXCHANGE_H
/* First-party implementation of selected, published Arrow C Data and DLPack ABI
 * contracts. No dependency on their language runtimes is required.
 * Arrow: https://arrow.apache.org/docs/format/CDataInterface.html
 * DLPack: https://dmlc.github.io/dlpack/latest/
 */
#include "caelum_port.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef ARROW_C_DATA_INTERFACE
#define ARROW_C_DATA_INTERFACE
struct ArrowArray {
 int64_t length, null_count, offset, n_buffers, n_children;
 const void **buffers;
 struct ArrowArray **children, *dictionary;
 void (*release)(struct ArrowArray*);
 void *private_data;
};
struct ArrowSchema {
 const char *format, *name, *metadata;
 int64_t flags, n_children;
 struct ArrowSchema **children, *dictionary;
 void (*release)(struct ArrowSchema*);
 void *private_data;
};
#endif /* ARROW_C_DATA_INTERFACE */
typedef struct cp_owned_matrix {
 double *data;
 uint64_t rows, columns;
} cp_owned_matrix;
/* Arrow struct array: x0 ... xN, y; Float64, no nulls, CPU owned buffers.
 * Both outputs must be released exactly once using their respective callbacks.
 * Import is copying, validated, and does not consume the source arrays. */
CP_API cp_status cp_arrow_export_dataset(const cp_dataset *data, struct ArrowArray *array, struct ArrowSchema *schema);
CP_API cp_status cp_arrow_import_dataset(const struct ArrowArray *array, const struct ArrowSchema *schema, cp_dataset *out);
CP_API void cp_owned_matrix_release(cp_owned_matrix *m);

typedef struct cp_dl_device { int32_t device_type, device_id; } cp_dl_device;
typedef struct cp_dl_dtype { uint8_t code, bits; uint16_t lanes; } cp_dl_dtype;
typedef struct cp_dl_tensor {
 void *data;
 cp_dl_device device;
 int32_t ndim;
 cp_dl_dtype dtype;
 int64_t *shape, *strides;
 uint64_t byte_offset;
} cp_dl_tensor;
typedef struct cp_dl_version { uint32_t major, minor; } cp_dl_version;
typedef struct cp_dl_managed_tensor_versioned cp_dl_managed_tensor_versioned;
struct cp_dl_managed_tensor_versioned {
 cp_dl_version version;
 void *manager_ctx;
 void (*deleter)(cp_dl_managed_tensor_versioned *);
 uint64_t flags;
 cp_dl_tensor dl_tensor;
};
/* CPU Float64, shape rows x features, dense contiguous. The returned DLPack
 * managed tensor owns a copy and must have its deleter called exactly once.
 * Import copies, supports positive strides, does not take ownership. */
CP_API cp_status cp_dlpack_export_features(const cp_dataset *data, cp_dl_managed_tensor_versioned **out);
CP_API cp_status cp_dlpack_import_features(const cp_dl_managed_tensor_versioned *tensor, cp_owned_matrix *out);
#ifdef __cplusplus
}
#endif
#endif
