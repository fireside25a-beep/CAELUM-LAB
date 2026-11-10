#!/usr/bin/env python3
"""Nonlinear root-branch, residual and inverse stress; mathematical fixtures."""
import ctypes as C
import json
from pathlib import Path
import sys
import math
class Law(C.Structure):
 _fields_=[('inputs',C.c_uint32),('degree',C.c_uint32),('term_count',C.c_uint32),
 ('powers',(C.c_uint8*3)*16),('coefficients',C.c_double*16),('train_rmse',C.c_double),
 ('holdout_rmse',C.c_double),('training_bic',C.c_double),('train_rows',C.c_uint64),
 ('holdout_rows',C.c_uint64),('split_seed',C.c_uint64),('attempted_candidates',C.c_uint32),
 ('rank_rejections',C.c_uint32),('selection_steps',C.c_uint32),
 ('selection_basis_ids',C.c_uint32*16),('selection_bic',C.c_double*16)]
lib=C.CDLL(str(Path(sys.argv[1]).resolve()))
lib.cp_dynamics_step_midpoint.argtypes=[C.POINTER(Law),C.c_uint,C.POINTER(C.c_double),C.c_double,C.POINTER(C.c_double)]
accepted=rejected=paired_refusals=0;max_error=max_residual=0.
for degree in [2,3]:
 for c0 in [-5,-1,0,1,5]:
  for c1 in [-5,-1,1,5]:
   m=Law();m.inputs=1;m.degree=degree;m.term_count=2;m.selection_steps=2
   m.attempted_candidates=1;m.powers[1][0]=degree;m.coefficients[0]=c0;m.coefficients[1]=c1
   for x in [-3,-2,-1,-.5,.1,.5,1,2,3]:
    for h in [-3,-1,-.5,-.1,.1,.5,1,3]:
     initial=C.c_double(x);n=C.c_double(17);back=C.c_double(19)
     status=lib.cp_dynamics_step_midpoint(C.byref(m),1,C.byref(initial),h,C.byref(n))
     if status:
      assert n.value==17,'failed step committed output';rejected+=1;continue
     accepted+=1
     residual=abs(n.value-x-h*(c0+c1*((x+n.value)/2)**degree))
     scale=max(1,abs(x),abs(n.value));assert math.isfinite(n.value) and residual<=3e-13*scale
     max_residual=max(max_residual,residual/scale)
     status=lib.cp_dynamics_step_midpoint(C.byref(m),1,C.byref(n),-h,C.byref(back))
     if status:
      assert back.value==19;paired_refusals+=1;continue
     error=abs(back.value-x)/max(1,abs(x),abs(back.value));assert error<=256*sys.float_info.epsilon
     max_error=max(max_error,error)
# Literal one-step root-branch counterexample that passed R1 in both directions.
m=Law();m.inputs=1;m.degree=2;m.term_count=2;m.selection_steps=2;m.attempted_candidates=1
m.powers[1][0]=2;m.coefficients[0]=m.coefficients[1]=-5
x=C.c_double(-3);out=C.c_double(123)
assert lib.cp_dynamics_step_midpoint(C.byref(m),1,C.byref(x),-1,C.byref(out))!=0 and out.value==123
assert paired_refusals==0,'accepted forward step failed its public reverse contract on the fixed stress grid'
print(json.dumps({'status':'PASS','cases':accepted+rejected,'accepted':accepted,'refused':rejected,
 'paired_public_refusals':paired_refusals,'max_scaled_inverse_error':max_error,
 'max_scaled_equation_residual':max_residual}))
