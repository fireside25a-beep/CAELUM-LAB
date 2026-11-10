#include "io.h"
#include "caelum_lab.h"
#include "file_identity.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#ifndef S_ISREG
#define S_ISREG(mode) (((mode)&_S_IFMT)==_S_IFREG)
#endif
#define CP_SYNC(f) _commit(_fileno(f))
#define CP_UNLINK(p) _unlink(p)
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#define CP_SYNC(f) fsync(fileno(f))
#define CP_UNLINK(p) unlink(p)
#endif
#define CP_PATH_BYTES 4096u
#define CP_FILE_LIMIT (64u*1024u*1024u)

/* Serialize commits and recovery across threads/processes. A recovery reader
   must never roll back an active writer. Kernel ownership is released on exit. */
#ifdef _WIN32
typedef HANDLE io_lock;
static io_lock lock_io(void){
 HANDLE h=CreateMutexA(NULL,FALSE,"Local\\CAELUM_SCIENTIFIC_IO_V1");
 if(!h)return NULL;
 DWORD r=WaitForSingleObject(h,INFINITE);if(r!=WAIT_OBJECT_0&&r!=WAIT_ABANDONED){CloseHandle(h);return NULL;}return h;
}
static void unlock_io(io_lock h){ReleaseMutex(h);CloseHandle(h);}
#define LOCK_FAILED(h) (!(h))
#else
typedef int io_lock;
static io_lock lock_io(void){
 char path[128];int n=snprintf(path,sizeof(path),"/tmp/caelum-scientific-io-%lu.lock",(unsigned long)getuid());
 if(n<0||(size_t)n>=sizeof(path))return -1;
 int fd=open(path,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;
 struct stat st;if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||(st.st_mode&077)!=0){close(fd);return -1;}
 int r;do{r=flock(fd,LOCK_EX);}while(r!=0&&errno==EINTR);
 if(r!=0){close(fd);return -1;}return fd;
}
static void unlock_io(io_lock fd){(void)flock(fd,LOCK_UN);close(fd);}
#define LOCK_FAILED(h) ((h)<0)
#endif
static int sync_parent(const char *path){
#ifdef _WIN32
 (void)path;return 0; /* MoveFileEx WRITE_THROUGH below; native qualification required. */
#else
 char *parent=malloc(strlen(path)+2);if(!parent)return -1;strcpy(parent,path);
 char *slash=strrchr(parent,'/');if(!slash)strcpy(parent,".");else if(slash==parent)slash[1]=0;else *slash=0;
 int fd=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC);free(parent);if(fd<0)return -1;
 int result=fsync(fd);int e=errno;close(fd);errno=e;return result;
#endif
}
static int durable_unlink(const char *path){
 if(CP_UNLINK(path)!=0)return errno==ENOENT?0:-1;
 return sync_parent(path);
}
static char *suffix(const char *path,const char *tail){
 size_t a=strlen(path),b=strlen(tail);if(a>CP_PATH_BYTES-b-1)return NULL;
 char *s=malloc(a+b+1);if(s){memcpy(s,path,a);memcpy(s+a,tail,b+1);}return s;
}
static int replace_file(const char *from,const char *to){
#ifdef _WIN32
 return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?0:-1;
#else
 if(rename(from,to)!=0)return -1;
 return sync_parent(to);
#endif
}
/* Same-directory temporary data files are not scientific RNG providers. */
static cp_status stage_file(const char *path,const void *data,size_t size,char **out){
 if(!path||(!data&&size)||!out)return CP_INVALID;
 char *tmp=suffix(path,".caelum-XXXXXX");if(!tmp)return CP_RANGE;
 FILE *f=NULL;
#ifdef _WIN32
 if(_mktemp_s(tmp,strlen(tmp)+1)!=0){free(tmp);return CP_IO;}
 int fd=_open(tmp,_O_CREAT|_O_EXCL|_O_WRONLY|_O_BINARY,_S_IREAD|_S_IWRITE);
 if(fd>=0){f=_fdopen(fd,"wb");if(!f)_close(fd);}
#else
 int fd=mkstemp(tmp);if(fd>=0){f=fdopen(fd,"wb");if(!f)close(fd);}
#endif
 if(!f){free(tmp);return CP_IO;}
 cp_status status=CP_OK;
 if((size&&fwrite(data,1,size,f)!=size)||fflush(f)!=0||CP_SYNC(f)!=0)status=CP_IO;
 if(fclose(f)!=0)status=CP_IO;
 if(status==CP_OK&&sync_parent(tmp)!=0)status=CP_IO;
 if(status!=CP_OK){(void)CP_UNLINK(tmp);free(tmp);return status;}
 *out=tmp;return CP_OK;
}
static cp_status atomic_raw(const char *path,const void *data,size_t bytes){
 char *tmp=NULL;cp_status s=stage_file(path,data,bytes,&tmp);if(s!=CP_OK)return s;
 if(replace_file(tmp,path)!=0)s=CP_IO;
 if(s!=CP_OK)(void)CP_UNLINK(tmp);
 free(tmp);return s;
}
cp_status cp_read_bytes(const char *path,size_t limit,uint8_t **bytes,size_t *size){
 if(!path||!bytes||!size||!limit)return CP_INVALID;
 FILE *f=fopen(path,"rb");if(!f)return CP_IO;
 if(fseek(f,0,SEEK_END)!=0){fclose(f);return CP_IO;}
 long end=ftell(f);if(end<0){fclose(f);return CP_IO;}
 if((uint64_t)end>limit){fclose(f);return CP_RANGE;}
 if(fseek(f,0,SEEK_SET)!=0){fclose(f);return CP_IO;}
 size_t n=(size_t)end;uint8_t *data=malloc(n?n:1);if(!data){fclose(f);return CP_NO_MEMORY;}
 size_t got=fread(data,1,n,f);int err=ferror(f),extra=EOF;
 if(!err&&got==n)extra=fgetc(f);
 err|=ferror(f);int closed=fclose(f);
 if(err||closed||got!=n||extra!=EOF){free(data);return CP_IO;}
 *bytes=data;*size=n;return CP_OK;
}
/* Local recovery record: checksummed, bounded paths; never a portable model. */
typedef struct pair_record {
 char magic[8];uint32_t committed,existed[2];
 char paths[2][CP_PATH_BYTES],backups[2][CP_PATH_BYTES],stages[2][CP_PATH_BYTES];uint8_t digest[32];
} pair_record;
static void seal_record(pair_record *r){cp_sha256((const uint8_t*)r,offsetof(pair_record,digest),r->digest);}
static int valid_record(const pair_record *r){
 uint8_t hash[32];cp_sha256((const uint8_t*)r,offsetof(pair_record,digest),hash);
 if(memcmp(r->magic,"CPPAIR3",8)||memcmp(hash,r->digest,32)||r->committed>1)return 0;
 for(unsigned i=0;i<2;i++)if(r->existed[i]>1||!memchr(r->paths[i],0,CP_PATH_BYTES)||
  !memchr(r->backups[i],0,CP_PATH_BYTES)||!memchr(r->stages[i],0,CP_PATH_BYTES)||!r->paths[i][0])return 0;
 return 1;
}
static cp_status read_record(const char *path,pair_record *r){
 FILE *f=fopen(path,"rb");if(!f)return CP_IO;
 size_t n=fread(r,1,sizeof(*r),f);int err=ferror(f),extra=EOF;
 if(!err&&n==sizeof(*r))extra=fgetc(f);
 err|=ferror(f);int close=fclose(f);
 return n==sizeof(*r)&&extra==EOF&&!err&&!close&&valid_record(r)?CP_OK:CP_CORRUPT;
}
static cp_status recover_unlocked(const char *path){
 if(!path)return CP_INVALID;
 char *journal=suffix(path,".caelum-pending");if(!journal)return CP_RANGE;
 struct stat info;if(stat(journal,&info)!=0){int e=errno;free(journal);return e==ENOENT?CP_OK:CP_IO;}
 pair_record r;cp_status status=read_record(journal,&r);free(journal);if(status!=CP_OK)return status;
 if(strcmp(path,r.paths[0])&&strcmp(path,r.paths[1])){
  if(!cp_same_file(path,r.paths[0])&&!cp_same_file(path,r.paths[1]))return CP_CORRUPT;
 }
 /* Either committed record proves BOTH new renames already completed. */
 for(unsigned i=0;i<2;i++){
  char *other=suffix(r.paths[i],".caelum-pending");pair_record q;
  if(other&&read_record(other,&q)==CP_OK&&memcmp(q.paths,r.paths,sizeof(r.paths))==0&&memcmp(q.backups,r.backups,sizeof(r.backups))==0&&memcmp(q.stages,r.stages,sizeof(r.stages))==0&&q.committed)r.committed=1;
  free(other);
 }
 if(!r.committed){
  for(unsigned i=0;i<2;i++){
   if(r.existed[i]){
    uint8_t *data=NULL;size_t size=0;status=cp_read_bytes(r.backups[i],CP_FILE_LIMIT,&data,&size);
    if(status==CP_OK)status=atomic_raw(r.paths[i],data,size);
    free(data);if(status!=CP_OK)return status;
   }else if(durable_unlink(r.paths[i])!=0)return CP_IO;
  }
 }
 /* After restoring BOTH old files, commit that recovery outcome before
    deleting backups. A second crash can then safely finish cleanup. */
 r.committed=1;seal_record(&r);
 /* Both surviving sidecars must describe the committed recovery outcome.
    Otherwise a crash while deleting the first sidecar could strand the
    second as an obsolete rollback record after its backups were removed. */
 for(unsigned i=0;i<2;i++){
  char *j=suffix(r.paths[i],".caelum-pending");if(!j)return CP_RANGE;
  status=atomic_raw(j,&r,sizeof(r));free(j);if(status!=CP_OK)return status;
 }
 for(unsigned i=0;i<2;i++){
  if(r.backups[i][0]&&durable_unlink(r.backups[i])!=0)return CP_IO;
  if(r.stages[i][0]&&durable_unlink(r.stages[i])!=0)return CP_IO;
 }
 for(unsigned i=0;i<2;i++){
  char *j=suffix(r.paths[i],".caelum-pending");if(!j)return CP_RANGE;
  if(durable_unlink(j)!=0){free(j);return CP_IO;}free(j);
 }
 return CP_OK;
}
static cp_status atomic_write_unlocked(const char *path,const void *data,size_t bytes){
 if(!path||(!data&&bytes))return CP_INVALID;
 cp_status status=recover_unlocked(path);if(status!=CP_OK)return status;
 return atomic_raw(path,data,bytes);
}
static cp_status atomic_stream_unlocked(const char *path,cp_status (*write)(FILE *,const void *),const void *context){
 if(!path||!write)return CP_INVALID;
 cp_status status=recover_unlocked(path);if(status!=CP_OK)return status;
 char *tmp=NULL;status=stage_file(path,"",0,&tmp);if(status!=CP_OK)return status;
 FILE *f=fopen(tmp,"wb");if(!f){(void)CP_UNLINK(tmp);free(tmp);return CP_IO;}
 status=write(f,context);
 if(fflush(f)!=0||CP_SYNC(f)!=0)status=CP_IO;
 if(fclose(f)!=0)status=CP_IO;
 if(status==CP_OK&&replace_file(tmp,path)!=0)status=CP_IO;
 if(status!=CP_OK)(void)CP_UNLINK(tmp);
 free(tmp);return status;
}
static cp_status write_pair_unlocked(const char *a,const void *aa,size_t na,const char *b,const void *bb,size_t nb){
 if(!a||!b||(!aa&&na)||(!bb&&nb)||cp_same_file(a,b))return CP_INVALID;
 const char *paths[2]={a,b};const void *values[2]={aa,bb};size_t sizes[2]={na,nb};
 pair_record r;memset(&r,0,sizeof(r));memcpy(r.magic,"CPPAIR3",8);
 char *stages[2]={NULL,NULL},*journals[2]={NULL,NULL};cp_status status=CP_OK;int journaled=0;
 for(unsigned i=0;i<2;i++){
  status=recover_unlocked(paths[i]);if(status!=CP_OK)goto done;
  char *canonical=cp_resolved_output(paths[i]);
  if(!canonical||strlen(canonical)>=CP_PATH_BYTES){free(canonical);status=CP_IO;goto done;}
  memcpy(r.paths[i],canonical,strlen(canonical)+1);free(canonical);
  journals[i]=suffix(r.paths[i],".caelum-pending");if(!journals[i]){status=CP_RANGE;goto done;}
  struct stat info;
  if(stat(r.paths[i],&info)==0){
   if(!S_ISREG(info.st_mode)){status=CP_INVALID;goto done;}
   uint8_t *old=NULL;size_t old_size=0;status=cp_read_bytes(r.paths[i],CP_FILE_LIMIT,&old,&old_size);
   char *backup=NULL;if(status==CP_OK)status=stage_file(r.paths[i],old,old_size,&backup);free(old);
   if(status!=CP_OK)goto done;
   memcpy(r.backups[i],backup,strlen(backup)+1);free(backup);r.existed[i]=1;
  }else if(errno!=ENOENT){status=CP_IO;goto done;}
  status=stage_file(r.paths[i],values[i],sizes[i],&stages[i]);if(status!=CP_OK)goto done;
  memcpy(r.stages[i],stages[i],strlen(stages[i])+1);
 }
 seal_record(&r);
 for(unsigned i=0;i<2;i++){
  /* Rename may succeed even when the following directory fsync fails.
     From this point on, recovery owns every backup until proven complete. */
  journaled=1;
  status=atomic_raw(journals[i],&r,sizeof(r));if(status!=CP_OK)goto done;
 }
 for(unsigned i=0;i<2;i++)if(replace_file(stages[i],r.paths[i])!=0){status=CP_IO;goto done;}
 r.committed=1;seal_record(&r);
 /* One durable committed record is enough for deterministic roll-forward. */
 status=atomic_raw(journals[0],&r,sizeof(r));if(status!=CP_OK)goto done;
 status=recover_unlocked(r.paths[0]);if(status==CP_OK)journaled=0;
 done:
 if(status!=CP_OK&&journaled){cp_status recovered=recover_unlocked(r.paths[0]);if(recovered!=CP_OK)status=recovered;}
 for(unsigned i=0;i<2;i++){
  if(stages[i]){(void)CP_UNLINK(stages[i]);free(stages[i]);}
  if(!journaled&&r.backups[i][0])(void)CP_UNLINK(r.backups[i]);
  free(journals[i]);
 }
 return status;
}
static cp_status csv_snapshot_unlocked(const char *path,cp_dataset *out,uint8_t hash[32]){
 if(!path||!out||!hash)return CP_INVALID;
 cp_status status=recover_unlocked(path);if(status!=CP_OK)return status;
 uint8_t *data=NULL;size_t n=0;status=cp_read_bytes(path,32u*1024u*1024u,&data,&n);
 if(status!=CP_OK)return status;
 cp_dataset result={0};status=cp_dataset_parse_memory(data,n,&result);
 if(status==CP_OK){uint8_t digest[32];cp_sha256(data,n,digest);memcpy(hash,digest,32);*out=result;}
 free(data);return status;
}

cp_status cp_recover_pair(const char *path){
 io_lock lock=lock_io();if(LOCK_FAILED(lock))return CP_IO;
 cp_status status=recover_unlocked(path);unlock_io(lock);return status;
}
cp_status cp_atomic_write(const char *path,const void *data,size_t bytes){
 io_lock lock=lock_io();if(LOCK_FAILED(lock))return CP_IO;
 cp_status status=atomic_write_unlocked(path,data,bytes);unlock_io(lock);return status;
}
cp_status cp_atomic_stream(const char *path,cp_status (*write)(FILE *,const void *),const void *context){
 io_lock lock=lock_io();if(LOCK_FAILED(lock))return CP_IO;
 cp_status status=atomic_stream_unlocked(path,write,context);unlock_io(lock);return status;
}
cp_status cp_write_pair(const char *a,const void *aa,size_t na,const char *b,const void *bb,size_t nb){
 io_lock lock=lock_io();if(LOCK_FAILED(lock))return CP_IO;
 cp_status status=write_pair_unlocked(a,aa,na,b,bb,nb);unlock_io(lock);return status;
}
cp_status cp_csv_snapshot(const char *path,cp_dataset *out,uint8_t hash[32]){
 io_lock lock=lock_io();if(LOCK_FAILED(lock))return CP_IO;
 cp_status status=csv_snapshot_unlocked(path,out,hash);unlock_io(lock);return status;
}

cp_status cp_dataset_read_snapshot(const char *path,cp_dataset *out,uint8_t hash[32]){return cp_csv_snapshot(path,out,hash);}

cp_status cp_read_pair(const char *a,size_t limit_a,uint8_t **aa,size_t *na,const char *b,size_t limit_b,uint8_t **bb,size_t *nb){
 if(!a||!b||!aa||!bb||!na||!nb)return CP_INVALID;
 io_lock lock=lock_io();if(LOCK_FAILED(lock))return CP_IO;
 uint8_t *x=NULL,*y=NULL;size_t nx=0,ny=0;
 cp_status status=recover_unlocked(a);if(status==CP_OK)status=recover_unlocked(b);
 if(status==CP_OK)status=cp_read_bytes(a,limit_a,&x,&nx);
 if(status==CP_OK)status=cp_read_bytes(b,limit_b,&y,&ny);
 if(status==CP_OK){*aa=x;*bb=y;*na=nx;*nb=ny;}else{free(x);free(y);}
 unlock_io(lock);return status;
}

cp_status cp_write_atomic_file(const char *path,const uint8_t *data,size_t bytes){
 if(bytes>CP_FILE_LIMIT)return CP_RANGE;
 return cp_atomic_write(path,data,bytes);
}
