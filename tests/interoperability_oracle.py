#!/usr/bin/env python3
"""Real ONNX checker/runtime and PyArrow producer/consumer, plus negatives."""
import ctypes as C
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import numpy as np
import onnx
import onnxruntime as ort
import pyarrow as pa
from onnx import helper, numpy_helper, TensorProto

class Array(C.Structure): pass
class Schema(C.Structure): pass
AP=C.POINTER(Array); SP=C.POINTER(Schema)
Array._fields_=[('length',C.c_int64),('null_count',C.c_int64),('offset',C.c_int64),
 ('n_buffers',C.c_int64),('n_children',C.c_int64),('buffers',C.POINTER(C.c_void_p)),
 ('children',C.POINTER(AP)),('dictionary',AP),('release',C.c_void_p),('private_data',C.c_void_p)]
Schema._fields_=[('format',C.c_char_p),('name',C.c_char_p),('metadata',C.c_char_p),
 ('flags',C.c_int64),('n_children',C.c_int64),('children',C.POINTER(SP)),
 ('dictionary',SP),('release',C.c_void_p),('private_data',C.c_void_p)]
class Data(C.Structure):
 _fields_=[('x',C.POINTER(C.c_double)),('y',C.POINTER(C.c_double)),('rows',C.c_uint64),('features',C.c_uint64)]
class Model(C.Structure):
 _fields_=[('features',C.c_uint64),('fitted_rows',C.c_uint64),('beta',C.c_double*17),('rmse',C.c_double)]

def main(build):
 build=Path(build).resolve();exe=build/'caelum-port';lib=C.CDLL(str(build/'libcaelum_port.so'))
 lib.cp_arrow_import_dataset.argtypes=[AP,SP,C.POINTER(Data)]
 lib.cp_arrow_export_dataset.argtypes=[C.POINTER(Data),AP,SP]
 lib.cp_dataset_release.argtypes=[C.POINTER(Data)]
 lib.cp_onnx_import_linear.argtypes=[C.c_char_p,C.POINTER(Model)]
 rng=np.random.default_rng(1839);checks=0
 with tempfile.TemporaryDirectory(prefix='caelum-official-') as directory:
  root=Path(directory)
  for n in [1,2,3,8,16]:
   x=rng.normal(size=(79,n));w=rng.normal(size=(n,1));b=np.array([1.25])
   for typed in [False,True]:
    tensors=[helper.make_tensor('W',TensorProto.DOUBLE,[n,1],w.ravel()),
             helper.make_tensor('B',TensorProto.DOUBLE,[1],b)] if typed else [numpy_helper.from_array(w,'W'),numpy_helper.from_array(b,'B')]
    graph=helper.make_graph([helper.make_node('Gemm',['X','W','B'],['Y'])],'independent',
     [helper.make_tensor_value_info('X',TensorProto.DOUBLE,['N',n])],
     [helper.make_tensor_value_info('Y',TensorProto.DOUBLE,['N',1])],tensors)
    model=helper.make_model(graph,opset_imports=[helper.make_opsetid('',13)],ir_version=9)
    onnx.checker.check_model(model,full_check=True)
    origin=root/'external.onnx';onnx.save_model(model,origin)
    capsule=root/'native.cpm';exported=root/'exported.onnx'
    run=subprocess.run([str(exe),'onnx-import',str(origin),str(capsule)],capture_output=True,text=True)
    assert run.returncode==0,run.stderr
    subprocess.run([str(exe),'onnx-export',str(capsule),str(exported)],check=True,capture_output=True)
    returned=onnx.load(exported);onnx.checker.check_model(returned,full_check=True)
    opts=ort.SessionOptions();opts.intra_op_num_threads=1;opts.inter_op_num_threads=1
    for file in [origin,exported]:
     session=ort.InferenceSession(str(file),sess_options=opts,providers=['CPUExecutionProvider'])
     np.testing.assert_allclose(session.run(None,{'X':x})[0],x@w+b,atol=2e-13,rtol=2e-13)
    checks+=6
    # Fail closed on semantically different storage, malformed tags and metadata.
    for kind in ['external_data','segment','sparse','negative_rows','nul_rmse','duplicate_meta','field_zero']:
     bad=onnx.ModelProto();bad.CopyFrom(returned)
     if kind=='external_data':
      t=bad.graph.initializer[0];t.external_data.add(key='location',value='elsewhere');t.data_location=1
     elif kind=='segment':bad.graph.initializer[0].segment.begin=0;bad.graph.initializer[0].segment.end=1
     elif kind=='sparse':
      sparse=bad.graph.sparse_initializer.add();sparse.dims.extend([1]);sparse.values.CopyFrom(numpy_helper.from_array(np.array([1.]),'unused'));sparse.indices.CopyFrom(numpy_helper.from_array(np.array([0],dtype=np.int64)))
     elif kind=='negative_rows':
      next(v for v in bad.metadata_props if v.key=='caelum.fitted_rows').value='-1'
     elif kind=='nul_rmse':
      next(v for v in bad.metadata_props if v.key=='caelum.training_rmse').value='0\0hidden'
     elif kind=='duplicate_meta':bad.metadata_props.add(key='caelum.fitted_rows',value='7')
     raw=bad.SerializeToString()
     if kind=='field_zero':raw+=b'\x01'+b'\0'*8
     origin.write_bytes(raw);sentinel=Model();C.memset(C.byref(sentinel),0x55,C.sizeof(sentinel));before=bytes(sentinel)
     status=lib.cp_onnx_import_linear(str(origin).encode(),C.byref(sentinel))
     assert status!=0,kind
     assert bytes(sentinel)==before,kind
     checks+=2
   # Actual PyArrow -> native, with child slices (offsets) and a copy-lifetime test.
   for sliced in [False,True]:
    start=3 if sliced else 0;stop=66
    xx=x[start:stop];yy=(x@w+b).ravel()[start:stop]
    columns=[pa.array(x[:,j]).slice(start,stop-start) for j in range(n)]+[pa.array((x@w+b).ravel()).slice(start,stop-start)]
    arr=pa.StructArray.from_arrays(columns,names=[f'x{j}' for j in range(n)]+['y'])
    aa=Array();ss=Schema();arr._export_to_c(C.addressof(aa),C.addressof(ss));ds=Data()
    assert lib.cp_arrow_import_dataset(C.byref(aa),C.byref(ss),C.byref(ds))==0
    assert aa.release and ss.release  # borrowed input has not been consumed
    C.CFUNCTYPE(None,AP)(aa.release)(C.byref(aa));C.CFUNCTYPE(None,SP)(ss.release)(C.byref(ss))
    np.testing.assert_array_equal(np.ctypeslib.as_array(ds.x,shape=(len(xx)*n,)).reshape(len(xx),n),xx)
    np.testing.assert_array_equal(np.ctypeslib.as_array(ds.y,shape=(len(xx),)),yy)
    aa=Array();ss=Schema();assert lib.cp_arrow_export_dataset(C.byref(ds),C.byref(aa),C.byref(ss))==0
    back=pa.Array._import_from_c(C.addressof(aa),C.addressof(ss));assert not aa.release and not ss.release
    lib.cp_dataset_release(C.byref(ds))
    np.testing.assert_array_equal(np.column_stack([back.field(j).to_numpy() for j in range(n)]),xx)
    checks+=7
  # Real null-bearing Arrow arrays are rejected without consuming ownership.
  arr=pa.StructArray.from_arrays([pa.array([1.,None]),pa.array([2.,3.])],names=['x0','y'])
  aa=Array();ss=Schema();arr._export_to_c(C.addressof(aa),C.addressof(ss));ds=Data()
  assert lib.cp_arrow_import_dataset(C.byref(aa),C.byref(ss),C.byref(ds))!=0
  C.CFUNCTYPE(None,AP)(aa.release)(C.byref(aa));C.CFUNCTYPE(None,SP)(ss.release)(C.byref(ss));checks+=1
 print(json.dumps({'status':'PASS','checks':checks,'onnx':onnx.__version__,'onnxruntime':ort.__version__,'pyarrow':pa.__version__}))
if __name__=='__main__':main(sys.argv[1])
