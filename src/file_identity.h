#ifndef CP_INTERNAL_FILE_IDENTITY_H
#define CP_INTERNAL_FILE_IDENTITY_H
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static inline char *cp_resolved_output(const char *path){
 char absolute[32768];DWORD n=GetFullPathNameA(path,sizeof(absolute),absolute,NULL);
 if(!n||n>=sizeof(absolute))return NULL;
 char *slash=strrchr(absolute,'\\');if(!slash)return NULL;
 char base[32768];memcpy(base,slash+1,strlen(slash+1)+1);*slash=0;
 HANDLE parent=CreateFileA(absolute,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,
                          OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
 if(parent==INVALID_HANDLE_VALUE)return NULL;
 char canonical[32768];DWORD used=GetFinalPathNameByHandleA(parent,canonical,sizeof(canonical),FILE_NAME_NORMALIZED);
 CloseHandle(parent);if(!used||used>=sizeof(canonical))return NULL;
 size_t len=strlen(canonical),tail=strlen(base);char *out=malloc(len+tail+2);
 if(out){memcpy(out,canonical,len);out[len]='\\';memcpy(out+len+1,base,tail+1);}return out;
}
static inline int cp_same_file(const char *a,const char *b){
 if(!strcmp(a,b))return 1;
 HANDLE ha=CreateFileA(a,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,
                      OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
 HANDLE hb=CreateFileA(b,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,
                      OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
 BY_HANDLE_FILE_INFORMATION ia,ib;int same=0;
 if(ha!=INVALID_HANDLE_VALUE&&hb!=INVALID_HANDLE_VALUE&&
    GetFileInformationByHandle(ha,&ia)&&GetFileInformationByHandle(hb,&ib))
  same=ia.dwVolumeSerialNumber==ib.dwVolumeSerialNumber&&
       ia.nFileIndexHigh==ib.nFileIndexHigh&&ia.nFileIndexLow==ib.nFileIndexLow;
 if(ha!=INVALID_HANDLE_VALUE)CloseHandle(ha);
 if(hb!=INVALID_HANDLE_VALUE)CloseHandle(hb);
 char *pa=cp_resolved_output(a),*pb=cp_resolved_output(b);
 int canonical=pa&&pb&&_stricmp(pa,pb)==0;free(pa);free(pb);return same||canonical;
}
#else
#include <sys/stat.h>
#include <unistd.h>
/* Resolve the parent too: two differently spelled, not-yet-existing outputs
   must be distinguished before either one is opened. */
static inline char *cp_resolved_depth(const char *path,unsigned depth){
 if(depth>40)return NULL;
 char *existing=realpath(path,NULL);if(existing)return existing;
 struct stat link_info;
 if(lstat(path,&link_info)==0&&S_ISLNK(link_info.st_mode)){
  char target[4096];ssize_t n=readlink(path,target,sizeof(target)-1);
  if(n<0||(size_t)n>=sizeof(target)-1)return NULL;
  target[n]=0;
  if(target[0]=='/')return cp_resolved_depth(target,depth+1);
  const char *end=strrchr(path,'/');size_t prefix=end?(size_t)(end-path)+1:0;
  size_t length=prefix+(size_t)n;char *joined=malloc(length+1);if(!joined)return NULL;
  if(prefix)memcpy(joined,path,prefix);
  memcpy(joined+prefix,target,(size_t)n+1);
  char *resolved=cp_resolved_depth(joined,depth+1);free(joined);return resolved;
 }
 const char *slash=strrchr(path,'/'),*base=slash?slash+1:path;
 if(!*base)return NULL;
 size_t parent_len=slash?(size_t)(slash-path):1;
 char *parent=malloc(parent_len+2);if(!parent)return NULL;
 if(slash){if(parent_len)memcpy(parent,path,parent_len);else parent[parent_len++]='/';}
 else parent[0]='.';
 parent[parent_len]=0;
 char *dir=realpath(parent,NULL);free(parent);if(!dir)return NULL;
 size_t n=strlen(dir),m=strlen(base);char *full=malloc(n+m+2);
 if(full){memcpy(full,dir,n);full[n]='/';memcpy(full+n+1,base,m+1);}
 free(dir);return full;
}
static inline char *cp_resolved_output(const char *path){return cp_resolved_depth(path,0);}
static inline int cp_same_file(const char *a,const char *b){
 if(!strcmp(a,b))return 1;
 struct stat ia,ib;
 if(stat(a,&ia)==0&&stat(b,&ib)==0&&ia.st_dev==ib.st_dev&&ia.st_ino==ib.st_ino)return 1;
 char *pa=cp_resolved_output(a),*pb=cp_resolved_output(b);
 int same=pa&&pb&&!strcmp(pa,pb);free(pa);free(pb);return same;
}
#endif
#endif
