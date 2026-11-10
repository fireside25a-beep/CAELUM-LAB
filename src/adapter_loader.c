#include "caelum_port.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
cp_status cp_adapter_open(const char *path,cp_adapter_handle *out){
 if(!path||!out)return CP_INVALID;
 cp_adapter_handle a={0};
#ifdef _WIN32
 HMODULE h=LoadLibraryA(path);if(!h)return CP_PLUGIN_ERROR;
 cp_get_adapter_v1_fn get=(cp_get_adapter_v1_fn)(void*)GetProcAddress(h,"cp_adapter_get_v1");
 a.library_handle=(void*)h;
#else
 void *h=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!h)return CP_PLUGIN_ERROR;
 cp_get_adapter_v1_fn get=NULL;
 void *symbol=dlsym(h,"cp_adapter_get_v1");memcpy(&get,&symbol,sizeof(get));
 a.library_handle=h;
#endif
 if(!get){cp_adapter_close(&a);return CP_PLUGIN_ERROR;}
 a.api=get();
 if(!a.api || a.api->struct_bytes!=sizeof(cp_adapter_v1) || a.api->abi_major!=CP_ABI_MAJOR || a.api->abi_minor>CP_ABI_MINOR || a.api->model_kind!=1 || !a.api->name || !a.api->name[0] || !a.api->read_model || !a.api->write_model){
  cp_adapter_close(&a);return CP_ABI_MISMATCH;
 }
 *out=a;return CP_OK;
}
void cp_adapter_close(cp_adapter_handle *a){
 if(!a)return;
 if(a->library_handle){
#ifdef _WIN32
  FreeLibrary((HMODULE)a->library_handle);
#else
  dlclose(a->library_handle);
#endif
 }
 memset(a,0,sizeof(*a));
}
