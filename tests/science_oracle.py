#!/usr/bin/env python3
"""Independent science acceptance: C process is production, NumPy/SciPy/Sklearn
are external differential oracles. Known-truth synthetic mathematical data only.
Does not import the CAELUM science implementation into Python.
"""
import hashlib
import os
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np
import scipy.linalg
from sklearn.metrics import mean_squared_error

if len(sys.argv) != 2:
    raise SystemExit("usage: science_oracle.py /absolute/path/to/caelum-port")
BIN = pathlib.Path(sys.argv[1]).resolve()
assert BIN.is_file()
MASK = (1 << 64) - 1

def run(*args, ok=True):
    p = subprocess.run([str(BIN), *map(str, args)], capture_output=True, text=True)
    if ok != (p.returncode == 0):
        raise AssertionError(f"exit {p.returncode} expected_ok={ok} args={args} stdout={p.stdout} stderr={p.stderr}")
    return p

def mix(z):
    z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & MASK
    z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & MASK
    return z ^ (z >> 31)

def random_at(seed, stream, counter):
    return mix(mix(seed ^ 0x243f6a8885a308d3) ^
               mix(stream ^ 0x13198a2e03707344) ^
               mix(counter ^ 0xa4093822299f31d0))

def random_v2(seed,stream,counter):
    c=[counter&0xffffffff,counter>>32,stream&0xffffffff,stream>>32]
    k=[seed&0xffffffff,seed>>32]
    for _ in range(10):
        p0=0xd2511f53*c[0];p1=0xcd9e8d57*c[2]
        c=[((p1>>32)^c[1]^k[0])&0xffffffff,p1&0xffffffff,((p0>>32)^c[3]^k[1])&0xffffffff,p0&0xffffffff]
        k=[(k[0]+0x9e3779b9)&0xffffffff,(k[1]+0xbb67ae85)&0xffffffff]
    return c[0]|c[1]<<32

def parse_receipt(p):
    lines = p.read_text().splitlines()
    assert lines[0] in ("CAELUM-EXPERIMENT-A-v1","CAELUM-EXPERIMENT-A-v2")
    d = dict(x.split("=", 1) for x in lines[1:] if "=" in x and not x.startswith("term."))
    for line in lines:
        if line.startswith("term."):
            idx = int(line.split("=",1)[0].split(".")[1])
            parts = line.split("=",1)[1].split(",")
            d.setdefault("term_items", {})[idx] = (tuple(map(int, parts[:3])), float.fromhex(parts[3]))
    b = p.read_bytes()
    body = b.split(b"receipt_sha256=", 1)[0]
    assert hashlib.sha256(body).hexdigest() == d["receipt_sha256"]
    return d

def data_case(n, case, seed):
    r = np.random.default_rng(seed)
    x = r.uniform(-1.75,1.75,(n,2))
    a,b=x.T
    if case == "linear": y = 0.2 + 2.5*a - 1.75*b
    elif case == "quadratic": y = 1.25 + 2*a*a - .75*a*b
    elif case == "cubic": y = .4 - 1.1*a*a*a + 2.2*a*b
    elif case == "noisy": y = .2 + 2.5*a - 1.75*b + r.normal(0,.045,n)
    else: raise ValueError(case)
    return x, y

checks=0
start=time.monotonic()
with tempfile.TemporaryDirectory(prefix="caelum-science-independent-") as name:
    tmp=pathlib.Path(name)
    for case in ("linear", "quadratic", "cubic", "noisy"):
        for trial in range(10):
            x,y=data_case(240,case,1230+trial*101+len(case))
            data=tmp/f"{case}-{trial}.csv"
            np.savetxt(data,np.column_stack((x,y)),delimiter=",",fmt="%.17g")
            report=tmp/f"{case}-{trial}.cpr"
            degree=1 if case in ("linear","noisy") else (2 if case=="quadratic" else 3)
            seed=314159+trial
            run("lab-discover",data,degree,f"{seed:x}",report)
            run("lab-replay",data,report)
            d=parse_receipt(report)
            assert d["data_sha256"] == hashlib.sha256(data.read_bytes()).hexdigest()
            assert int(d["seed"],16)==seed
            splitseed=int(d["split_seed"],16)
            train=np.array([random_v2(splitseed,0x53434c41424c4142,i)%5!=0 for i in range(240)])
            assert sum(train)==int(d["train_rows"])
            assert sum(~train)==int(d["holdout_rows"])
            terms=[d["term_items"][i] for i in range(int(d["terms"]))]
            design=np.column_stack([np.prod(x**np.array(powers[:2])[None,:],axis=1) for powers,c in terms])
            external,*_=scipy.linalg.lstsq(design[train],y[train],lapack_driver="gelsy")
            ca=np.array([coef for powers,coef in terms])
            assert np.max(np.abs(ca-external))<2e-8, (case,trial,ca,external)
            calc=design@ca
            tr=float(np.sqrt(mean_squared_error(y[train],calc[train])))
            hold=float(np.sqrt(mean_squared_error(y[~train],calc[~train])))
            assert abs(tr-float.fromhex(d["train_rmse"]))<3e-10
            assert abs(hold-float.fromhex(d["holdout_rmse"]))<3e-10
            if case != "noisy": assert hold<2e-9, (case,trial,hold)
            else: assert 0.025<hold<0.085,hold
            checks += 9
            if trial == 0:
                # Reverse from the sealed result through the adapter/ONNX chain.
                capsule=tmp/"reconstructed.cpm";basis=tmp/"basis.txt"
                run("lab-export",data,report,capsule,basis)
                assert "CAELUM-POLYNOMIAL-BASIS-v2" in basis.read_text()
                onnx=tmp/"reconstructed.onnx";back=tmp/"back.cpm"
                run("onnx-export",capsule,onnx)
                run("onnx-import",onnx,back)
                assert capsule.read_bytes()==back.read_bytes()
                checks += 4
                # Every bit in the receipt must be rejected if flipped.
                raw=report.read_bytes()
                for bit in range(len(raw)*8):
                    corrupted=bytearray(raw);corrupted[bit//8] ^= 1 << (bit%8)
                    bad=tmp/"tampered.cpr";bad.write_bytes(corrupted)
                    run("lab-replay",data,bad,ok=False)
                    checks+=1
                # A one-ulp input change is mathematical drift and must be detected.
                mutated=np.loadtxt(data,delimiter=",")
                mutated[0,0]=np.nextafter(mutated[0,0],np.inf)
                drift=tmp/"drift.csv";np.savetxt(drift,mutated,delimiter=",",fmt="%.17g")
                assert data.read_bytes()!=drift.read_bytes()
                run("lab-replay",drift,report,ok=False)
                checks+=1
    # Parser and ONNX robustness: garbage is an expected rejection, not success.
    random_state=random.Random(9877)
    for k in range(256):
        binary=bytes(random_state.getrandbits(8) for _ in range(random_state.randrange(0,400)))
        raw=tmp/"mutated.onnx";raw.write_bytes(binary)
        run("onnx-import",raw,tmp/"untrusted.cpm",ok=False)
        checks+=1
    for badline in ("NaN,2,3\n","1,Infinity,3\n","1,2,\n","1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18\n","x,y,z\n"):
        broken=tmp/"bad.csv";broken.write_text(badline)
        run("lab-discover",broken,2,"ff",tmp/"invalid.cpr",ok=False)
        checks+=1
print(f"PASS external science: {checks} cross-library/replay/corruption assertions; 40 independent datasets, 4 full bit-flip receipts, 256 random ONNX invalid inputs; elapsed={time.monotonic()-start:.2f}s")
