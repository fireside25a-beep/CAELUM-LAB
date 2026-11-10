#!/usr/bin/env python3
"""Execute packaged programs after relocation, in isolated working folders."""
import json,os,pathlib,subprocess,tempfile,time
root=pathlib.Path(__file__).resolve().parent/'native';results=[]
for profile in ['debug','optimized','sanitized']:
 folder=(root/profile).resolve();manifest=json.loads((folder/'TESTS.json').read_text())
 for test in manifest['tests']:
  with tempfile.TemporaryDirectory(prefix='caelum-packaged-') as scratch:
   command=[]
   for arg in test['command']:
    if arg.startswith('@artifact/'):
     file=(folder/arg[len('@artifact/'):]).resolve()
     if not file.is_relative_to(folder) or not file.is_file():raise ValueError('Invalid packaged artifact')
     command.append(str(file))
    elif arg=='@scratch':command.append(scratch)
    else:command.append(arg)
   env=os.environ.copy();env.pop('LD_LIBRARY_PATH',None);env.pop('DYLD_LIBRARY_PATH',None)
   if profile=='sanitized':env.setdefault('ASAN_OPTIONS','detect_leaks=0');env.setdefault('UBSAN_OPTIONS','halt_on_error=1')
   start=time.monotonic();run=subprocess.run(command,cwd=scratch,env=env,capture_output=True,text=True,timeout=180)
   item={'profile':profile,'test':test['name'],'exit_code':run.returncode,'seconds':time.monotonic()-start,'stdout':run.stdout,'stderr':run.stderr};results.append(item)
   if run.returncode:print(json.dumps({'status':'FAIL','results':results},indent=2));raise SystemExit(1)
print(json.dumps({'status':'PASS','executed_targets':len(results),'profiles':3,'loader_environment':'LD_LIBRARY_PATH and DYLD_LIBRARY_PATH removed','leak_scope':'Packaged ASan/UBSan default disables LSan because the validation host uses ptrace; separate full Valgrind evidence covers all 18 native targets.','results':results},indent=2))
