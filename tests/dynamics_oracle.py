#!/usr/bin/env python3
"""Independent future-block and reverse-time numeric audit.

Production C11 runs as an external executable. This verifier independently
reconstructs the discovered linear vector field and uses NumPy/LAPACK matrix
solves to validate its implicit midpoint trajectory and every reported score.
Known-truth oscillator trajectories are legitimate scientific test fixtures.
"""
import math
import pathlib
import re
import subprocess
import sys
import tempfile
import numpy as np

if len(sys.argv) != 2:
    raise SystemExit("usage: dynamics_oracle.py /absolute/path/to/caelum-port")
binary = pathlib.Path(sys.argv[1]).resolve()
assert binary.exists()


def call(path, seed="9217"):
    command = [str(binary), "lab-dynamics", str(path), "1", seed]
    run = subprocess.run(command, capture_output=True, text=True)
    if run.returncode:
        raise AssertionError(f"CLI failed {run.returncode}: {run.stderr}")
    return run.stdout


def parse_report(text):
    parts = text.split("derivative_target=")
    assert len(parts) == 3
    a = np.zeros((2, 2), dtype=np.float64)
    b = np.zeros(2, dtype=np.float64)
    term_text = []
    for k, part in enumerate(parts[1:]):
        entries = re.findall(r"term\.\d+ powers=(\d+),(\d+),(\d+) coefficient=([^\n]+)", part)
        term_text.extend((k, *item) for item in entries)
        for p0, p1, p2, coef in entries:
            coeff = float(coef)
            if (p0,p1,p2) == ("0","0","0"):
                b[k] = coeff
            elif (p0,p1,p2) == ("1","0","0"):
                a[k,0] = coeff
            elif (p0,p1,p2) == ("0","1","0"):
                a[k,1] = coeff
            else:
                raise AssertionError("unexpected polynomial term in first-degree linear dynamics")
    match = re.search(r"future_block_anchor=(\d+) forecast_steps=(\d+) forward_rmse=([\deE+.-]+) reverse_observed_rmse=([\deE+.-]+) solver_cycle_rmse=([\deE+.-]+) solver_cycle_max_abs=([\deE+.-]+)",text)
    assert match, text
    return a,b,term_text,(int(match[1]),int(match[2]),*(float(match[i]) for i in range(3,7)))


def midpoint(x, h, a, b):
    eye = np.eye(2)
    return np.linalg.solve(eye-0.5*h*a,(eye+0.5*h*a)@x+h*b)


checks = 0
with tempfile.TemporaryDirectory(prefix="caelum-independent-dynamics-") as folder:
    folder=pathlib.Path(folder)
    rng=np.random.default_rng(911173)
    for trial in range(20):
        n=200+trial*19
        gaps=rng.uniform(.006,.023,n-1)
        times=np.r_[0.,np.cumsum(gaps)]
        amp=rng.uniform(.5,2)
        omega=rng.uniform(.5,1.5)
        phase=rng.uniform(-2,2)
        x=np.column_stack((amp*np.sin(omega*times+phase),amp*np.cos(omega*times+phase)))
        path=folder/f"oscillator_{trial}.csv"
        np.savetxt(path,np.column_stack((x,times)),delimiter=",",fmt="%.17g")
        output=call(path)
        assert output==call(path), "non-deterministic scientific replay"
        a,b,terms,(anchor,steps,forward,observed,cycle,maximum)=parse_report(output)
        assert anchor == n*7//10-1 and steps == n-1-anchor
        pred=np.zeros((steps+1,2));pred[0]=x[anchor]
        for i in range(steps):
            pred[i+1]=midpoint(pred[i],times[anchor+i+1]-times[anchor+i],a,b)
        back=np.zeros_like(pred);back[0]=pred[-1]
        from_observed=np.zeros_like(pred);from_observed[0]=x[-1]
        for i in range(steps):
            h=times[n-2-i]-times[n-1-i]
            back[i+1]=midpoint(back[i],h,a,b)
            from_observed[i+1]=midpoint(from_observed[i],h,a,b)
        reference_forward=np.sqrt(np.mean((pred[1:]-x[anchor+1:])**2))
        reference_reverse=np.sqrt(np.mean((from_observed[1:]-x[anchor:-1][::-1])**2))
        reference_cycle=np.sqrt(np.mean((back[::-1]-pred)**2))
        reference_peak=np.max(np.abs(back[::-1]-pred))
        assert np.isclose(reference_forward,forward,rtol=1e-8,atol=1e-10),(trial,reference_forward,forward)
        assert np.isclose(reference_reverse,observed,rtol=1e-8,atol=1e-10),(trial,reference_reverse,observed)
        assert abs(reference_cycle-cycle)<1e-10
        assert abs(reference_peak-maximum)<1e-10
        assert cycle<1e-10 and maximum<1e-9
        checks+=6
        # Holdout observations may affect only test scores, not fitted terms.
        altered=x.copy()
        altered[anchor+20,0] += 0.07
        changed=folder/f"changed_{trial}.csv"
        np.savetxt(changed,np.column_stack((altered,times)),delimiter=",",fmt="%.17g")
        output2=call(changed)
        _,_,terms2,(_,_,forward2,_,_,_)=parse_report(output2)
        assert terms==terms2, "future holdout contaminated the training fit"
        assert abs(forward2-forward)>1e-6
        checks+=2
print(f"PASS independent dynamics oracle: {checks} checks, 20 future-block datasets, matrix-solve forward/reverse and leakage perturbations")
