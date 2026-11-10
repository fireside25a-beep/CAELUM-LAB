#ifndef CP_PRIVATE_IO_H
#define CP_PRIVATE_IO_H
#include "caelum_port.h"
#include <stdio.h>
cp_status cp_atomic_write(const char *path,const void *data,size_t bytes);
cp_status cp_atomic_stream(const char *path,cp_status (*write)(FILE *,const void *),const void *context);
cp_status cp_capsule_decode(const uint8_t bytes[CP_CAPSULE_BYTES],cp_linear_model *out);
cp_status cp_capsule_encode(const cp_linear_model *model,uint8_t out[CP_CAPSULE_BYTES]);
cp_status cp_read_bytes(const char *path,size_t limit,uint8_t **bytes,size_t *size);
cp_status cp_dataset_parse_memory(const uint8_t *bytes,size_t size,cp_dataset *out);
cp_status cp_csv_snapshot(const char *path,cp_dataset *out,uint8_t hash[32]);
cp_status cp_read_pair(const char *a,size_t limit_a,uint8_t **aa,size_t *na,const char *b,size_t limit_b,uint8_t **bb,size_t *nb);
cp_status cp_write_pair(const char *a,const void *aa,size_t na,const char *b,const void *bb,size_t nb);
cp_status cp_recover_pair(const char *path);
#endif
