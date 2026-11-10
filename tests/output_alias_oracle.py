#!/usr/bin/env python3
"""Every CLI file-producing path must preserve its input files under aliases."""
from pathlib import Path
import json, os, subprocess, sys, tempfile
import numpy as np

def main(build, expected_pass=True):
 build=Path(build).resolve();exe=build/'caelum-port';results=[]
 with tempfile.TemporaryDirectory(prefix='caelum-output-') as folder:
  root=Path(folder)
  def run(*args):return subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True)
  x=np.linspace(-2,2,150);data=np.c_[x,1.2+3*x]
  csv=root/'data.csv';np.savetxt(csv,data,delimiter=',',fmt='%.17g')
  model=root/'model.cpm';assert run('fit',csv,model).returncode==0
  receipt=root/'model.cpr';assert run('lab-discover',csv,1,'ff',receipt).returncode==0
  onnx=root/'model.onnx';assert run('onnx-export',model,onnx).returncode==0
  text=root/'model.txt';plugin=build/'cp_linear_exchange.so';assert run('export',plugin,model,text).returncode==0
  payloads={p.name:p.read_bytes() for p in [csv,model,receipt,onnx,text]}
  cases=[('fit',csv.name),('onnx-export',model.name),('onnx-import',onnx.name),
         ('export',model.name),('import',text.name),('lab-export-model',csv.name),
         ('lab-export-model',receipt.name),('lab-export-basis',csv.name),('lab-export-basis',receipt.name)]
  for command,target in cases:
   for kind in ['same','hardlink','symlink']:
    for name,payload in payloads.items():(root/name).write_bytes(payload)
    source=root/target;dest=root/'alias';dest.unlink(missing_ok=True)
    if kind=='same':dest=source
    elif kind=='hardlink':os.link(source,dest)
    else:dest.symlink_to(source)
    if command=='lab-export-model':args=['lab-export',csv,receipt,dest,root/'basis.txt']
    elif command=='lab-export-basis':args=['lab-export',csv,receipt,root/'new.cpm',dest]
    elif command in ['export','import']:args=[command,plugin,source,dest]
    else:args=[command,source,dest]
    p=run(*args);preserved=all((root/n).read_bytes()==b for n,b in payloads.items())
    passed=p.returncode!=0 and preserved
    results.append({'command':command,'target':target,'alias':kind,'rejected':p.returncode!=0,'preserved':preserved,'pass':passed})
    if expected_pass:assert passed,results[-1]
  for name,payload in payloads.items():(root/name).write_bytes(payload)
  # Lexical alias between two outputs that do not exist yet.
  first=root/'future.cpm';first.unlink(missing_ok=True)
  second=str(root)+'/./future.cpm'
  p=run('lab-export',csv,receipt,first,second)
  passed=p.returncode!=0 and not first.exists()
  results.append({'command':'lab-export-two-new-outputs','pass':passed})
  if expected_pass:assert passed,results[-1]
  p=run('predict',model,'');passed=p.returncode!=0
  results.append({'command':'predict-empty-feature','pass':passed})
  if expected_pass:assert passed,results[-1]
 print(json.dumps({'status':'PASS' if all(r['pass'] for r in results) else 'FAIL','cases':results},indent=2))
if __name__=='__main__':main(sys.argv[1],len(sys.argv)<3)
