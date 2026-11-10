#ifndef CP_JSON_H
#define CP_JSON_H
#include "caelum_port.h"
#define CP_JSON_TOKENS 4096u
typedef struct cp_json_token {size_t begin,end;unsigned next,children;char type;} cp_json_token;
typedef struct cp_json {const char *text;size_t size,at;unsigned count;cp_json_token tokens[CP_JSON_TOKENS];} cp_json;
cp_status cp_json_parse(cp_json *json,const char *text,size_t size);
int cp_json_get(const cp_json *json,unsigned object,const char *key);
int cp_json_at(const cp_json *json,unsigned array,unsigned index);
cp_status cp_json_string(const cp_json *json,unsigned token,char *out,size_t capacity);
cp_status cp_json_number(const cp_json *json,unsigned token,double *out);
cp_status cp_json_boolean(const cp_json *json,unsigned token,int *out);
cp_status cp_json_keys(const cp_json *json,unsigned object,const char *const *keys,unsigned count);
#endif
