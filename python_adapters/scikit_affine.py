"""Working bidirectional sklearn LinearRegression <-> CAELUM native capsule.

This is an *external interoperability tool*, not an ML production dependency.
Requires an existing CAELUM shared library, numpy, and scikit-learn. The
single-output affine predictor is the only supported estimator. Unsupported
regularization, preprocessing, class labels and multioutput semantics are
refused, not approximated. Native CP ABI major must be 1.
"""
from __future__ import annotations

import ctypes as C
from pathlib import Path
import numpy as np
from sklearn.linear_model import LinearRegression

MAX_FEATURES=16
class NativeModel(C.Structure):
    _fields_=[("features",C.c_uint64),("fitted_rows",C.c_uint64),
              ("beta",C.c_double*(MAX_FEATURES+1)),("training_rmse",C.c_double)]

class NativeSDK:
    def __init__(self, library: str | Path):
        self.lib=C.CDLL(str(Path(library).resolve()))
        self.lib.cp_abi_version.restype=C.c_uint32
        self.lib.cp_capsule_load.argtypes=[C.c_char_p,C.POINTER(NativeModel)]
        self.lib.cp_capsule_load.restype=C.c_int
        self.lib.cp_capsule_save.argtypes=[C.c_char_p,C.POINTER(NativeModel)]
        self.lib.cp_capsule_save.restype=C.c_int
        if (self.lib.cp_abi_version()>>16)!=1:
            raise RuntimeError("Unsupported CAELUM ABI major")

    def export_sklearn(self, model: LinearRegression, path: str | Path,
                       *, fitted_rows: int, training_rmse: float) -> None:
        if type(model) is not LinearRegression:
            raise TypeError("Only exact sklearn.linear_model.LinearRegression is supported")
        if not hasattr(model,"coef_") or not hasattr(model,"intercept_"):
            raise ValueError("Unfitted sklearn model")
        coefs=np.asarray(model.coef_,dtype=np.float64)
        if coefs.ndim!=1 or not (1<=len(coefs)<=MAX_FEATURES):
            raise ValueError("Only single-target affine models with 1..16 inputs")
        if not np.all(np.isfinite(coefs)) or not np.isfinite(model.intercept_):
            raise ValueError("Nonfinite sklearn parameters")
        if not np.isfinite(training_rmse) or training_rmse<0 or fitted_rows<0:
            raise ValueError("Invalid native provenance fields")
        if getattr(model,"positive",False):
            # The coefficients can be represented, but the positive-constrained
            # training objective cannot be reconstructed from a prediction capsule.
            raise ValueError("Positive constrained sklearn training is not portable")
        m=NativeModel();m.features=len(coefs);m.fitted_rows=fitted_rows
        m.training_rmse=float(training_rmse);m.beta[0]=float(model.intercept_)
        for j,c in enumerate(coefs,1): m.beta[j]=float(c)
        status=self.lib.cp_capsule_save(str(path).encode(),C.byref(m))
        if status:raise RuntimeError(f"CAELUM capsule export status={status}")

    def import_sklearn(self, path: str | Path) -> LinearRegression:
        m=NativeModel()
        status=self.lib.cp_capsule_load(str(path).encode(),C.byref(m))
        if status:raise RuntimeError(f"CAELUM capsule import status={status}")
        obj=LinearRegression()
        obj.coef_=np.array(list(m.beta)[1:1+m.features],dtype=np.float64)
        obj.intercept_=float(m.beta[0]);obj.n_features_in_=int(m.features)
        return obj
