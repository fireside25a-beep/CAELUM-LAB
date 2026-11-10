#!/usr/bin/env python3
"""Independent Valgrind memory/leak checks for every registered native target."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
build=Path(sys.argv[1]).resolve();checker=Path(sys.argv[2]).resolve();logs=Path(sys.argv[3]).resolve()
logs.mkdir(parents=True,exist_ok=True)
tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1'],text=True))['tests']
results=[]
for test in tests:
 log=logs/(test['name']+'.log')
 command=[str(checker),'--leak-check=full','--show-leak-kinds=all','--errors-for-leak-kinds=all',
          '--error-exitcode=99',f'--log-file={log}']
 if os.environ.get('CP_VALGRIND_DEBUG'):command.append('--extra-debuginfo-path='+os.environ['CP_VALGRIND_DEBUG'])
 with tempfile.TemporaryDirectory(prefix="caelum-valgrind-") as scratch:
  run=subprocess.run([*command,*test['command']],cwd=scratch,capture_output=True,text=True,timeout=180)
 results.append({'target':test['name'],'exit_code':run.returncode,'stdout':run.stdout,'stderr':run.stderr})
 if run.returncode:
  print(json.dumps(results,indent=2));raise SystemExit(1)
print(json.dumps({'status':'PASS','checker':subprocess.check_output([str(checker),'--version'],text=True).strip(),
 'targets':len(tests),'results':results},indent=2))
