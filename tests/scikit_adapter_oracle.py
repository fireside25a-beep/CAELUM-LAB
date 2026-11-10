#!/usr/bin/env python3
"""Concrete third-party sklearn -> CAELUM -> ONNX -> sklearn round trip."""
import pathlib,sys,tempfile,subprocess
import numpy as np
from sklearn.linear_model import LinearRegression
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'python_adapters'))
from scikit_affine import NativeSDK

if len(sys.argv)!=3:raise SystemExit('usage: scikit_adapter_oracle.py <sdk.so> <caelum-port>')
sdk=NativeSDK(sys.argv[1]);cli=pathlib.Path(sys.argv[2]).resolve()
r=np.random.default_rng(553)
checks=0
with tempfile.TemporaryDirectory(prefix='caelum-sk-') as root:
    folder=pathlib.Path(root)
    for size in (1,2,5,12,16):
        for noise in (0.,0.03):
            x=r.normal(size=(150,size));beta=r.normal(size=size)
            y=x@beta + 1.25 + r.normal(size=150)*noise
            sk=LinearRegression().fit(x,y)
            rmse=float(np.sqrt(np.mean((sk.predict(x)-y)**2)))
            cap=folder/'linear.cpm';onx=folder/'model.onnx';back=folder/'readback.cpm'
            sdk.export_sklearn(sk,cap,fitted_rows=len(x),training_rmse=rmse)
            sk2=sdk.import_sklearn(cap)
            assert np.array_equal(sk.coef_,sk2.coef_)
            assert sk.intercept_==sk2.intercept_
            assert np.array_equal(sk.predict(x),sk2.predict(x))
            subprocess.run([cli,'onnx-export',cap,onx],check=True,capture_output=True)
            subprocess.run([cli,'onnx-import',onx,back],check=True,capture_output=True)
            assert cap.read_bytes()==back.read_bytes()
            restored=sdk.import_sklearn(back)
            assert np.array_equal(sk.predict(x),restored.predict(x))
            checks+=5
    # Import mutation: signed capsule digest makes a single bit flip reject.
    b=bytearray(cap.read_bytes());b[31]^=1
    (folder/'corrupt.cpm').write_bytes(b)
    try:
        sdk.import_sklearn(folder/'corrupt.cpm')
    except RuntimeError:checks+=1
    else:raise AssertionError('corrupt capsule accepted by native API')
print(f'PASS scikit-learn bidirectional native bridge: {checks} assertions; 10 fitted sklearn models, 1/2/5/12/16 inputs and ONNX traversal')
