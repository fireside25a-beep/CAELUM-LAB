#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
/* Test-only process interruption. Never linked into the scientific library. */
static unsigned renames,unlinks,syncs;
static int target(const char *kind,unsigned count){const char *s=getenv(kind);return s&&strtoul(s,NULL,10)==count;}
int rename(const char *a,const char *b){
 int (*real_rename)(const char*,const char*)=NULL;*(void**)(&real_rename)=dlsym(RTLD_NEXT,"rename");
 unsigned n=++renames;if(target("CP_KILL_BEFORE_RENAME",n))_exit(86);
 int result=real_rename(a,b);if(target("CP_KILL_AFTER_RENAME",n))_exit(86);return result;
}
int unlink(const char *a){
 int (*real_unlink)(const char*)=NULL;*(void**)(&real_unlink)=dlsym(RTLD_NEXT,"unlink");
 int result=real_unlink(a);if(target("CP_KILL_AFTER_UNLINK",++unlinks))_exit(86);return result;
}
int fsync(int fd){
 int (*real_fsync)(int)=NULL;*(void**)(&real_fsync)=dlsym(RTLD_NEXT,"fsync");
 if(target("CP_FAIL_FSYNC",++syncs)){errno=EIO;return -1;}return real_fsync(fd);
}
