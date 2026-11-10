#ifndef CP_LOCALE_NUMERIC_H
#define CP_LOCALE_NUMERIC_H
#include <stdarg.h>
#include <stdio.h>
double cp_strtod_c(const char *s,char **end);
int cp_vsnprintf_c(char *dst,size_t n,const char *fmt,va_list args);
int cp_snprintf_c(char *dst,size_t n,const char *fmt,...);
int cp_fprintf_c(FILE *f,const char *fmt,...);
int cp_rounding_supported(void);
#endif
