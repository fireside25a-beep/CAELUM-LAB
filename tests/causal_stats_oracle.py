#!/usr/bin/env python3
"""External SciPy and statsmodels oracle for CAELUM native RCT statistics.
All datasets here are explicitly generated seeded mathematical tests.
"""
import ctypes as C
import pathlib, subprocess, sys, tempfile
import numpy as np
from scipy.stats import norm
from statsmodels.stats.multitest import multipletests

if len(sys.argv)!=3:
    raise SystemExit('usage: causal_stats_oracle.py <caelum-port> <libcaelum_port.so>')
exe=pathlib.Path(sys.argv[1]).resolve();lib=C.CDLL(str(pathlib.Path(sys.argv[2]).resolve()))
lib.cp_holm_adjust.argtypes=[C.POINTER(C.c_double),C.c_size_t,C.POINTER(C.c_double)]
lib.cp_holm_adjust.restype=C.c_int
rng=np.random.default_rng(4800)
checks=0;covered=0;alltrials=0
with tempfile.TemporaryDirectory(prefix='caelum-rct-oracle-') as directory:
    f=pathlib.Path(directory)/'randomized.csv'
    for n in (50,150,400):
        for k in range(40):
            treatment=rng.binomial(1,.5,n)
            # No confounding; treatment assigned independently of latent noise.
            noise=rng.normal(0,1,n)
            outcome=2.0+1.2*treatment+noise
            np.savetxt(f,np.column_stack((treatment,outcome)),delimiter=',',fmt='%.17g')
            proc=subprocess.run([exe,'lab-effect',f,'--randomized'],capture_output=True,text=True,check=True)
            import re
            found=re.search(r'effect=(\S+) standard_error=(\S+) ci95=\[(\S+),(\S+)\]',proc.stdout)
            if not found:raise AssertionError(proc.stdout)
            actual=np.array([float(v) for v in found.groups()])
            a=outcome[treatment==0];b=outcome[treatment==1]
            diff=b.mean()-a.mean()
            se=np.sqrt(a.var(ddof=1)/len(a)+b.var(ddof=1)/len(b))
            ref=[diff,se,diff-norm.ppf(.975)*se,diff+norm.ppf(.975)*se]
            assert np.allclose(actual,ref,rtol=2e-12,atol=1e-12),(n,k,actual,ref)
            covered+=(actual[2]<=1.2<=actual[3]);alltrials+=1;checks+=4
            fail=subprocess.run([exe,'lab-effect',f,'--observational'],capture_output=True,text=True)
            assert fail.returncode!=0
            checks+=1
    assert .85 < covered/alltrials < 1.0,covered/alltrials
    for n in (3,5,25,100):
        p=rng.uniform(0,1,n).astype('float64')
        p[0]=0.;p[-1]=1.
        out=(C.c_double*n)()
        status=lib.cp_holm_adjust(p.ctypes.data_as(C.POINTER(C.c_double)),n,out)
        assert status==0
        expected=multipletests(p,method='holm')[1]
        assert np.array_equal(np.asarray(out),expected),(np.asarray(out),expected)
        checks+=n
print(f'PASS randomized effect vs SciPy and Holm vs statsmodels: {checks} assertions, {alltrials} independent seeded trials, coverage={covered/alltrials:.3f} (observed benchmark statistic, not clinical coverage certification)')
