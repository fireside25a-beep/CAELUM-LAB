#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "locale_numeric.h"
#include <locale.h>
#include <stdlib.h>
#include <fenv.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#ifdef _WIN32
typedef _locale_t cp_locale;
#define CREATE_LOCALE() _create_locale(LC_NUMERIC,"C")
#define FREE_LOCALE(p) _free_locale(p)
#else
typedef locale_t cp_locale;
#define CREATE_LOCALE() newlocale(LC_NUMERIC_MASK,"C",(locale_t)0)
#define FREE_LOCALE(p) freelocale(p)
#endif
/* Immutable process-lifetime locale; never mutate the application's locale. */
static _Atomic(uintptr_t) numeric_locale=0;
static cp_locale get_locale(void){
 uintptr_t value=atomic_load(&numeric_locale);
 if(!value){
  cp_locale fresh=CREATE_LOCALE();if(!fresh)return (cp_locale)0;
  uintptr_t expected=0;
  if(!atomic_compare_exchange_strong(&numeric_locale,&expected,(uintptr_t)fresh))FREE_LOCALE(fresh);
  value=atomic_load(&numeric_locale);
 }
 return (cp_locale)value;
}
double cp_strtod_c(const char *s,char **end){
 cp_locale loc=get_locale();if(!loc){if(end)*end=(char*)s;errno=ENOMEM;return 0;}
#ifdef _WIN32
 return _strtod_l(s,end,loc);
#else
 return strtod_l(s,end,loc);
#endif
}
int cp_vsnprintf_c(char *dst,size_t n,const char *fmt,va_list args){
 cp_locale loc=get_locale();if(!loc)return -1;
#ifdef _WIN32
 return _vsnprintf_l(dst,n,fmt,loc,args);
#else
 locale_t previous=uselocale(loc);if(!previous)return -1;
 int result=vsnprintf(dst,n,fmt,args);(void)uselocale(previous);return result;
#endif
}
int cp_snprintf_c(char *dst,size_t n,const char *fmt,...){
 va_list args;va_start(args,fmt);int nout=cp_vsnprintf_c(dst,n,fmt,args);va_end(args);return nout;
}
int cp_fprintf_c(FILE *f,const char *fmt,...){
 cp_locale loc=get_locale();if(!loc)return -1;
 va_list args;va_start(args,fmt);
#ifdef _WIN32
 int result=_vfprintf_l(f,fmt,loc,args);
#else
 locale_t previous=uselocale(loc);if(!previous){va_end(args);return -1;}
 int result=vfprintf(f,fmt,args);(void)uselocale(previous);
#endif
 va_end(args);return result;
}
int cp_rounding_supported(void){return fegetround()==FE_TONEAREST;}
