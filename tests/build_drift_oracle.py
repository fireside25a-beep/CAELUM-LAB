#!/usr/bin/env python3
"""Cross-build numeric drift: actual CAELUM binaries compared on identical inputs.
Not a proof of numerical invariance across every machine or compiler.
"""
import math
import pathlib
import re
import subprocess
import sys
import tempfile
import numpy as np

if len(sys.argv) < 3:
    raise SystemExit('usage: build_drift_oracle.py <caelum-cli-1> <caelum-cli-2> [more]')
executables=[pathlib.Path(s).resolve() for s in sys.argv[1:]]
for executable in executables:
    if not executable.is_file(): raise FileNotFoundError(executable)

def run(exe,*args):
    p=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,check=True)
    return p.stdout

def vector(text):
    vals=re.findall(r'coefficient=([-+\de.E]+)',text)
    assert vals,text
    return np.asarray([float(v) for v in vals])

checks=0
rng=np.random.default_rng(20261110)
with tempfile.TemporaryDirectory(prefix='caelum-build-drift-') as temp:
    root=pathlib.Path(temp)
    for trial in range(12):
        n=270
        x=rng.uniform(-1.8,1.8,size=(n,2))
        y=1.5+0.3*x[:,0]**2-0.8*x[:,0]*x[:,1]+rng.normal(scale=0.003,size=n)
        p=root/'data.csv'
        np.savetxt(p,np.column_stack([x,y]),delimiter=',',fmt='%.17g')
        outputs=[];capsules=[]
        for index,exe in enumerate(executables):
            receipt=root/f'{index}.cpr'
            run(exe,'lab-discover',p,2,'cafe',receipt)
            output=receipt.read_text()
            outputs.append(output)
            capsules.append((receipt,output))
            assert 'receipt_sha256=' in output
            assert 'source=deterministic' in output
            assert run(exe,'lab-replay',p,receipt).startswith('experiment_a_replay=')
            checks+=3
        reference=outputs[0]
        ref_terms=re.findall(r'term\.\d+=([\d,]+),([^\n]+)',reference)
        for i,output in enumerate(outputs[1:],1):
            terms=re.findall(r'term\.\d+=([\d,]+),([^\n]+)',output)
            assert [a for a,_ in terms]==[a for a,_ in ref_terms],('term drift',trial,i)
            np.testing.assert_allclose([float.fromhex(b) for _,b in terms],
                                      [float.fromhex(b) for _,b in ref_terms],rtol=1e-8,atol=1e-10)
            checks+=2
    for trial in range(8):
        n=260
        h=rng.uniform(0.007,0.019,size=n-1)
        t=np.r_[0.,np.cumsum(h)]
        freq=rng.uniform(.7,1.3)
        data=np.column_stack((np.sin(freq*t),np.cos(freq*t),t))
        p=root/'trajectory.csv';np.savetxt(p,data,delimiter=',',fmt='%.17g')
        outputs=[run(exe,'lab-dynamics',p,1,'2026') for exe in executables]
        ref=vector(outputs[0])
        metrics=re.compile(r'future_block_anchor=(\d+) forecast_steps=(\d+) forward_rmse=([-+\de.E]+) reverse_observed_rmse=([-+\de.E]+) solver_cycle_rmse=([-+\de.E]+) solver_cycle_max_abs=([-+\de.E]+)')
        m=metrics.search(outputs[0]);assert m,outputs[0]
        for j,output in enumerate(outputs[1:],1):
            np.testing.assert_allclose(vector(output),ref,atol=1e-9,rtol=1e-8)
            another=metrics.search(output);assert another,(trial,j)
            assert another.group(1,2)==m.group(1,2)
            np.testing.assert_allclose([float(v) for v in another.group(3,4,5,6)],
                                      [float(v) for v in m.group(3,4,5,6)],atol=1e-9,rtol=1e-7)
            checks+=3
print(f'PASS cross-compiler/build drift: {checks} assertions, 12 discovery and 8 dynamics datasets, {len(executables)} native binaries')
