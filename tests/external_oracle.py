#!/usr/bin/env python3
"""Concrete third-party verification: NumPy QR/lstsq, Torch tensors/DLPack,
Google Protobuf decoder for actual ONNX IR. External tests never drive the
CAELUM fitting engine; they only check its artifacts and numerical outputs.
"""
import ctypes as C
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import torch
from google.protobuf import descriptor_pb2, descriptor_pool, message_factory


def onnx_messages():
    fd = descriptor_pb2.FileDescriptorProto(name="caelum_onnx_external_subset.proto", package="onnx", syntax="proto3")
    F = descriptor_pb2.FieldDescriptorProto

    def message(name, specs):
        msg = fd.message_type.add(name=name)
        for name_, num, typ, repeated, type_name in specs:
            x = msg.field.add(name=name_, number=num, type=typ,
                              label=F.LABEL_REPEATED if repeated else F.LABEL_OPTIONAL)
            if type_name:
                x.type_name = ".onnx." + type_name
    I = F.TYPE_INT64
    S = F.TYPE_STRING
    M = F.TYPE_MESSAGE
    B = F.TYPE_BYTES
    message("Dim", [("dim_value", 1, I, False, None), ("dim_param", 2, S, False, None)])
    message("Shape", [("dim", 1, M, True, "Dim")])
    message("TensorType", [("elem_type", 1, F.TYPE_INT32, False, None), ("shape", 2, M, False, "Shape")])
    message("Type", [("tensor_type", 1, M, False, "TensorType")])
    message("ValueInfo", [("name", 1, S, False, None), ("type", 2, M, False, "Type")])
    message("Tensor", [("dims", 1, I, True, None), ("data_type", 2, F.TYPE_INT32, False, None),
                       ("name", 8, S, False, None), ("raw_data", 9, B, False, None)])
    message("Node", [("input", 1, S, True, None), ("output", 2, S, True, None), ("name", 3, S, False, None),
                     ("op_type", 4, S, False, None)])
    message("Graph", [("node", 1, M, True, "Node"), ("name", 2, S, False, None),
                      ("initializer", 5, M, True, "Tensor"), ("input", 11, M, True, "ValueInfo"),
                      ("output", 12, M, True, "ValueInfo")])
    message("Opset", [("domain", 1, S, False, None), ("version", 2, I, False, None)])
    message("Metadata", [("key", 1, S, False, None), ("value", 2, S, False, None)])
    message("Model", [("ir_version", 1, I, False, None), ("producer_name", 2, S, False, None),
                      ("graph", 7, M, False, "Graph"), ("opset_import", 8, M, True, "Opset"),
                      ("metadata_props", 14, M, True, "Metadata")])
    pool = descriptor_pool.DescriptorPool()
    pool.Add(fd)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName("onnx.Model"))


class CPDataset(C.Structure):
    _fields_ = [("x", C.POINTER(C.c_double)), ("y", C.POINTER(C.c_double)),
                ("rows", C.c_uint64), ("features", C.c_uint64)]


class DLDevice(C.Structure):
    _fields_ = [("device_type", C.c_int32), ("device_id", C.c_int32)]


class DLDtype(C.Structure):
    _fields_ = [("code", C.c_uint8), ("bits", C.c_uint8), ("lanes", C.c_uint16)]


class DLTensor(C.Structure):
    _fields_ = [("data", C.c_void_p), ("device", DLDevice), ("ndim", C.c_int32),
                ("dtype", DLDtype), ("shape", C.POINTER(C.c_int64)),
                ("strides", C.POINTER(C.c_int64)), ("byte_offset", C.c_uint64)]


class DLVersion(C.Structure):
    _fields_ = [("major", C.c_uint32), ("minor", C.c_uint32)]


class DLManaged(C.Structure):
    _fields_ = [("version", DLVersion), ("manager_ctx", C.c_void_p),
                ("deleter", C.c_void_p), ("flags", C.c_uint64), ("dl_tensor", DLTensor)]


class OwnedMatrix(C.Structure):
    _fields_ = [("data", C.POINTER(C.c_double)), ("rows", C.c_uint64), ("columns", C.c_uint64)]


def main(build: Path, root: Path):
    exe = build / "caelum-port"
    lib = C.CDLL(str(build / "libcaelum_port.so"))
    lib.cp_dlpack_import_features.argtypes = [C.POINTER(DLManaged), C.POINTER(OwnedMatrix)]
    lib.cp_dlpack_import_features.restype = C.c_int
    lib.cp_owned_matrix_release.argtypes = [C.POINTER(OwnedMatrix)]
    lib.cp_dlpack_export_features.argtypes = [C.POINTER(CPDataset), C.POINTER(C.POINTER(DLManaged))]
    lib.cp_dlpack_export_features.restype = C.c_int

    work = build / "external_conformance"
    work.mkdir(exist_ok=True)
    csv = root / "fixtures" / "linear_observations.csv"
    cpm = work / "linear.cpm"
    onnx = work / "linear.onnx"
    back = work / "back.cpm"

    def run(*args):
        return subprocess.run([str(exe), *map(str, args)], check=True, capture_output=True, text=True).stdout

    run("fit", csv, cpm)
    run("onnx-export", cpm, onnx)
    run("onnx-import", onnx, back)
    assert cpm.read_bytes() == back.read_bytes(), "ONNX model capsule byte roundtrip differs"

    Model = onnx_messages()
    model = Model.FromString(onnx.read_bytes())
    assert model.ir_version == 9 and model.producer_name == "CAELUM"
    assert len(model.opset_import) == 1 and model.opset_import[0].version == 13
    g = model.graph
    assert len(g.node) == 1 and g.node[0].op_type == "Gemm"
    assert list(g.node[0].input) == ["X", "W", "B"]
    assert list(g.node[0].output) == ["Y"]
    assert g.input[0].type.tensor_type.elem_type == 11
    assert g.output[0].type.tensor_type.elem_type == 11
    tensors = {t.name: t for t in g.initializer}
    assert set(tensors) == {"W", "B"}
    f = int(tensors["W"].dims[0])
    assert list(tensors["W"].dims) == [f, 1]
    assert list(tensors["B"].dims) == [1]
    w = np.frombuffer(tensors["W"].raw_data, dtype="<f8")
    b = np.frombuffer(tensors["B"].raw_data, dtype="<f8")[0]
    data = np.loadtxt(csv, delimiter=",")
    x, y = data[:, :-1], data[:, -1]
    np_beta, *_ = np.linalg.lstsq(np.column_stack((np.ones(len(x)), x)), y, rcond=None)
    np.testing.assert_allclose(np.r_[b, w], np_beta, rtol=1e-12, atol=1e-12)
    t_x = torch.tensor(x, dtype=torch.float64)
    t_w = torch.tensor(w[:, np.newaxis], dtype=torch.float64)
    t_b = torch.tensor([b], dtype=torch.float64)
    output_torch = (t_x @ t_w + t_b).numpy().ravel()
    np.testing.assert_allclose(output_torch, x @ w + b, rtol=1e-14, atol=1e-14)
    np.testing.assert_allclose(output_torch, y, rtol=1e-12, atol=1e-12)

    # Real third-party tensor -> CAELUM via the versioned DLPack C ABI.
    torch_tensor = torch.arange(1, 13, dtype=torch.float64).reshape(4, 3)
    torch_capsule = torch_tensor.__dlpack__(max_version=(1, 0))
    get_pointer = C.pythonapi.PyCapsule_GetPointer
    get_pointer.argtypes = [C.py_object, C.c_char_p]
    get_pointer.restype = C.c_void_p
    pointer = get_pointer(torch_capsule, b"dltensor_versioned")
    assert pointer
    out = OwnedMatrix()
    assert lib.cp_dlpack_import_features(C.cast(pointer, C.POINTER(DLManaged)), C.byref(out)) == 0
    ca_from_torch = np.ctypeslib.as_array(out.data, shape=(int(out.rows*out.columns),)).copy().reshape(out.rows, out.columns)
    np.testing.assert_array_equal(ca_from_torch, torch_tensor.numpy())
    lib.cp_owned_matrix_release(C.byref(out))

    # CAELUM tensor exporter -> actual PyTorch consumer. Ownership transfers
    # exactly once to the DLPack-consuming library.
    xvalues = (C.c_double * 6)(1, 2, 3, 4, 5, 6)
    yvalues = (C.c_double * 2)(1, 2)
    dataset = CPDataset(xvalues, yvalues, 2, 3)
    exported = C.POINTER(DLManaged)()
    assert lib.cp_dlpack_export_features(C.byref(dataset), C.byref(exported)) == 0
    capsule_new = C.pythonapi.PyCapsule_New
    capsule_new.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p]
    capsule_new.restype = C.py_object
    capsule = capsule_new(C.cast(exported, C.c_void_p), b"dltensor_versioned", None)
    result_torch = torch.from_dlpack(capsule)
    torch.testing.assert_close(result_torch, torch.tensor([[1., 2., 3.], [4., 5., 6.]], dtype=torch.float64))
    # torch owns exported copy after successful conversion; do not call deleter.
    del result_torch
    print("PASS external oracles: NumPy least-squares, Google Protobuf ONNX decode, "
          "PyTorch Gemm, PyTorch↔CAELUM versioned DLPack")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: external_oracle.py BUILD_DIR SOURCE_DIR")
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
