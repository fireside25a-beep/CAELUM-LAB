#include "caelum_port.h"
#include <string.h>
#if (defined(__x86_64__) || defined(_M_X64))
# if defined(_MSC_VER)
#  include <intrin.h>
# else
#  include <cpuid.h>
# endif
#endif

cp_status cp_cpu_query(cp_cpu_report *out){
 if(!out)return CP_INVALID;
 memset(out,0,sizeof(*out));
#if defined(__x86_64__) || defined(_M_X64)
 out->arch_x86_64=1;
# if defined(_MSC_VER)
 int regs[4]={0};__cpuid(regs,0);out->max_basic_leaf=(uint32_t)regs[0];
 if(out->max_basic_leaf>=1){__cpuid(regs,1);out->has_rdrand=((uint32_t)regs[2]>>30)&1;
 out->has_osxsave=((uint32_t)regs[2]>>27)&1;out->has_avx=((uint32_t)regs[2]>>28)&1;
 out->has_sse2=((uint32_t)regs[3]>>26)&1;}
 if(out->max_basic_leaf>=7){__cpuidex(regs,7,0);out->has_rdseed=((uint32_t)regs[1]>>18)&1;}
 __cpuid(regs,0x80000000);out->max_extended_leaf=(uint32_t)regs[0];
 if(out->max_extended_leaf>=0x80000001){__cpuid(regs,0x80000001);out->has_rdtscp=((uint32_t)regs[3]>>27)&1;}
 if(out->max_extended_leaf>=0x80000007){__cpuid(regs,0x80000007);out->has_invariant_tsc=((uint32_t)regs[3]>>8)&1;}
# else
 unsigned a,b,c,d;
 if(__get_cpuid(0,&a,&b,&c,&d)){out->max_basic_leaf=a;}
 if(out->max_basic_leaf>=1 && __get_cpuid(1,&a,&b,&c,&d)){
  out->has_rdrand=(c>>30)&1;out->has_osxsave=(c>>27)&1;out->has_avx=(c>>28)&1;out->has_sse2=(d>>26)&1;
 }
 if(out->max_basic_leaf>=7 && __get_cpuid_count(7,0,&a,&b,&c,&d))out->has_rdseed=(b>>18)&1;
 if(__get_cpuid(0x80000000,&a,&b,&c,&d))out->max_extended_leaf=a;
 if(out->max_extended_leaf>=0x80000001&&__get_cpuid(0x80000001,&a,&b,&c,&d))out->has_rdtscp=(d>>27)&1;
 if(out->max_extended_leaf>=0x80000007&&__get_cpuid(0x80000007,&a,&b,&c,&d))out->has_invariant_tsc=(d>>8)&1;
# endif
 return CP_OK;
#else
 return CP_UNSUPPORTED;
#endif
}
cp_status cp_native_word(const char *source,uint64_t *out,unsigned *attempts){
 if(!source||!out||!attempts)return CP_INVALID;
 *attempts=0;
 int want_seed=strcmp(source,"rdseed")==0;
 if(!want_seed && strcmp(source,"rdrand")!=0)return CP_INVALID;
 cp_cpu_report r;cp_status st=cp_cpu_query(&r);if(st!=CP_OK)return st;
 if(want_seed&&!r.has_rdseed)return CP_UNSUPPORTED;
 if(!want_seed&&!r.has_rdrand)return CP_UNSUPPORTED;
#if defined(__x86_64__) || defined(_M_X64)
 for(unsigned j=0;j<64;j++){
  unsigned char ok=0;uint64_t word=0;
# if defined(_MSC_VER)
  if(want_seed)ok=(unsigned char)_rdseed64_step((unsigned __int64*)&word);
  else ok=(unsigned char)_rdrand64_step((unsigned __int64*)&word);
# else
  /* Flag-output constraint prevents the SETC memory-operand/address aliasing
   that an unconstrained '=qm' output can introduce with a random register.
   The CPU success condition is carry flag; no memory address may depend on
   the newly generated random word. */
  if(want_seed)__asm__ volatile("rdseed %0" : "=r"(word),"=@ccc"(ok) :: "cc");
  else __asm__ volatile("rdrand %0" : "=r"(word),"=@ccc"(ok) :: "cc");
# endif
  *attempts=j+1;
  if(ok){*out=word;return CP_OK;}
 }
 return CP_UNAVAILABLE;
#else
 return CP_UNSUPPORTED;
#endif
}
cp_status cp_native_rip(uintptr_t *out){
 if(!out)return CP_INVALID;
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
 uintptr_t pc=0;__asm__ volatile("lea 0(%%rip),%0" : "=r"(pc));*out=pc;return CP_OK;
#elif defined(_M_X64) && defined(_MSC_VER)
 /* MSVC lacks a direct RIP register intrinsic. Do not fabricate an instruction address. */
 return CP_UNSUPPORTED;
#else
 return CP_UNSUPPORTED;
#endif
}
cp_status cp_native_tsc(uint64_t *out,uint32_t *aux){
 if(!out||!aux)return CP_INVALID;
 cp_cpu_report r;cp_status st=cp_cpu_query(&r);if(st!=CP_OK)return st;
 if(!r.has_rdtscp)return CP_UNSUPPORTED;
#if defined(__x86_64__) || defined(_M_X64)
# if defined(_MSC_VER)
 unsigned int id;unsigned __int64 ticks=__rdtscp(&id);_mm_lfence();*out=(uint64_t)ticks;*aux=id;
# else
 unsigned a=0,d=0,c=0;__asm__ volatile("rdtscp; lfence" : "=a"(a),"=d"(d),"=c"(c)::"memory");
 *out=((uint64_t)d<<32)|a;*aux=c;
# endif
 return CP_OK;
#else
 return CP_UNSUPPORTED;
#endif
}

cp_status cp_native_rflags(uint64_t *out){
 if(!out)return CP_INVALID;
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
 uint64_t v=0;
 __asm__ volatile("pushfq; popq %0" : "=r"(v)::"memory");
 *out=v;
 return CP_OK;
#elif defined(_M_X64) && defined(_MSC_VER)
 *out=(uint64_t)__readeflags();
 return CP_OK;
#else
 return CP_UNSUPPORTED;
#endif
}
cp_status cp_native_xcr0(uint64_t *out){
 if(!out)return CP_INVALID;
 cp_cpu_report r={0};cp_status st=cp_cpu_query(&r);
 if(st!=CP_OK)return st;
 if(!r.has_osxsave)return CP_UNSUPPORTED;
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
 uint32_t a=0,d=0;
 __asm__ volatile("xgetbv" : "=a"(a),"=d"(d) : "c"(0));
 *out=((uint64_t)d<<32)|a;
 return CP_OK;
#elif defined(_M_X64) && defined(_MSC_VER)
 *out=_xgetbv(0);
 return CP_OK;
#else
 return CP_UNSUPPORTED;
#endif
}
