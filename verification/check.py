#!/usr/bin/env python3
"""Run exact obligations with the bundled solver; never accept unknown/timeout."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
root=Path(__file__).resolve().parent
solver=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'vendor/z3-linux-x86_64/z3'
results=[]
for path in sorted((root/'obligations').glob('*.smt2')):
 start=time.monotonic();result=subprocess.run([str(solver),str(path)],capture_output=True,text=True,timeout=40)
 expected='sat' if 'negative_control' in path.name else 'unsat'
 actual=result.stdout.strip()
 item={'obligation':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
       'expected':expected,'actual':actual,'exit_code':result.returncode,'stderr':result.stderr,
       'seconds':time.monotonic()-start}
 results.append(item)
 if actual!=expected or result.returncode or result.stderr:
  print(json.dumps(results,indent=2));raise SystemExit(1)
print(json.dumps({'status':'PASS','solver':subprocess.check_output([str(solver),'-version'],text=True).strip(),
 'proved_obligations':len(results)-1,'negative_controls':1,'results':results},indent=2))
