#!/usr/bin/env python3
"""128 independent numerical oracle trials against NumPy and scikit-learn.
Legitimate synthetic known-truth datasets; never report as physical entropy.
"""
import ctypes as C
import sys
from pathlib import Path
import numpy as np
from sklearn.linear_model import LinearRegression

class Matrix(C.Structure):
    _fields_=[('values',C.POINTER(C.c_double)),('rows',C.c_uint64),('columns',C.c_uint64),('stride',C.c_uint64)]
class Model(C.Structure):
    _fields_=[('features',C.c_uint64),('fitted_rows',C.c_uint64),('beta',C.c_double*17),('training_rmse',C.c_double)]
lib=C.CDLL(str(Path(sys.argv[1]).resolve()))
lib.cp_fit_linear_qr.argtypes=[Matrix,C.POINTER(C.c_double),C.POINTER(Model)]
lib.cp_fit_linear_qr.restype=C.c_int
lib.cp_predict_linear.argtypes=[C.POINTER(Model),C.POINTER(C.c_double),C.POINTER(C.c_double)]
lib.cp_predict_linear.restype=C.c_int
rng=np.random.default_rng(20261110)
comparisons=0
for step in range(128):
    n=int(rng.integers(36,120));f=int(rng.integers(1,13))
    X=rng.standard_normal((n,f)).astype(np.float64)
    beta=rng.normal(size=f+1)
    Y=beta[0]+X@beta[1:]+rng.normal(scale=0.004,size=n)
    X=np.ascontiguousarray(X);Y=np.ascontiguousarray(Y)
    model=Model()
    status=lib.cp_fit_linear_qr(Matrix(X.ctypes.data_as(C.POINTER(C.c_double)),n,f,f),Y.ctypes.data_as(C.POINTER(C.c_double)),C.byref(model))
    assert status==0,(step,'native fitting status',status)
    ref=np.linalg.lstsq(np.column_stack([np.ones(n),X]),Y,rcond=None)[0]
    sklearn=LinearRegression().fit(X,Y)
    np.testing.assert_allclose(list(model.beta)[:f+1],ref,rtol=1e-9,atol=1e-9)
    np.testing.assert_allclose(list(model.beta)[:f+1],np.r_[sklearn.intercept_,sklearn.coef_],rtol=1e-9,atol=1e-9)
    assert abs(model.training_rmse-np.sqrt(np.mean((Y-(ref[0]+X@ref[1:]))**2)))<1e-9
    probe=np.ascontiguousarray(rng.normal(size=f));val=C.c_double(0)
    assert lib.cp_predict_linear(C.byref(model),probe.ctypes.data_as(C.POINTER(C.c_double)),C.byref(val))==0
    assert abs(val.value-(ref[0]+probe@ref[1:]))<1e-9
    comparisons+=1
# Deliberate exact linear dependence must not be reported as a fit.
X=np.array([[1.,2.],[2.,4.],[3.,6.],[4.,8.],[5.,10.]],dtype=np.float64)
Y=np.array([1.,2.,3.,4.,5.],dtype=np.float64);model=Model()
status=lib.cp_fit_linear_qr(Matrix(X.ctypes.data_as(C.POINTER(C.c_double)),5,2,2),Y.ctypes.data_as(C.POINTER(C.c_double)),C.byref(model))
assert status==4,('rank-deficiency not detected',status)
X[0,0]=np.nan
status=lib.cp_fit_linear_qr(Matrix(X.ctypes.data_as(C.POINTER(C.c_double)),5,2,2),Y.ctypes.data_as(C.POINTER(C.c_double)),C.byref(model))
assert status==3,('NaN not rejected',status)
print(f'PASS {comparisons} randomized native QR fits matched NumPy + scikit-learn; rank and NaN negatives passed')
