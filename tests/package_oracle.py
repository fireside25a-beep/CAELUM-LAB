#!/usr/bin/env python3
"""Relocated installed SDK, loaded without build-tree paths or loader env."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
prefix=Path(sys.argv[1]).resolve();source=Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix='caelum-relocation-') as folder:
 root=Path(folder);target=root/'moved distribution';shutil.copytree(prefix,target,symlinks=True)
 env=os.environ.copy();env.pop('LD_LIBRARY_PATH',None);env.pop('DYLD_LIBRARY_PATH',None)
 exe=target/'bin/caelum-port';pc=next(target.rglob('caelum-port.pc'));env['PKG_CONFIG_PATH']=str(pc.parent)
 run=subprocess.run([str(exe),'probe'],env=env,cwd=root,check=True,capture_output=True,text=True)
 assert 'abi=' in run.stdout
 flags=subprocess.check_output(['pkg-config','--cflags','--libs','caelum-port'],env=env,text=True)
 # pkg-config escapes paths containing spaces using shell notation.
 import shlex
 libdir=pc.parent.parent
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(source/'examples/science_consumer.c'),
                 '-o',str(root/'consumer'),*shlex.split(flags),'-lm',f'-Wl,-rpath,{libdir}'],check=True,env=env)
 subprocess.run([str(root/'consumer')],check=True,env=env,capture_output=True,text=True)
 dynamic=subprocess.check_output(['readelf','-d',str(exe)],text=True)
 assert '$ORIGIN' in dynamic and '/workspace/' not in dynamic
 print('PASS relocated installed CLI and pkg-config C consumer, paths with spaces, no loader environment, relative RUNPATH')
