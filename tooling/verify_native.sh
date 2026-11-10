#!/usr/bin/env bash
# Complete first-party and independent third-party verification of this
# host's actual compiled CAELUM scientific native code. No fake passes/skips.
set -euo pipefail
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 MKL_NUM_THREADS=1
export PYTHONDONTWRITEBYTECODE=1
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DCP_BUILD_TESTS=ON
cmake --build "$BUILD" --parallel 4
ctest --test-dir "$BUILD" --output-on-failure
"$BUILD/caelum-port" probe
if ! python3 -c 'import numpy, scipy, sklearn, statsmodels, torch, google.protobuf, onnx, onnxruntime, pyarrow' >/dev/null 2>&1; then
  echo 'VERIFICATION INCOMPLETE: required independent oracle package missing' >&2
  exit 3
fi
python3 "$ROOT/tests/external_oracle.py" "$BUILD" "$ROOT"
python3 "$ROOT/tests/property_science.py" "$BUILD/libcaelum_port.so"
python3 "$ROOT/tests/science_oracle.py" "$BUILD/caelum-port"
python3 "$ROOT/tests/dynamics_oracle.py" "$BUILD/caelum-port"
python3 "$ROOT/tests/path_alias_oracle.py" "$BUILD/caelum-port"
python3 "$ROOT/tests/scikit_adapter_oracle.py" "$BUILD/libcaelum_port.so" "$BUILD/caelum-port"
python3 "$ROOT/tests/causal_stats_oracle.py" "$BUILD/caelum-port" "$BUILD/libcaelum_port.so"
python3 "$ROOT/tests/interoperability_oracle.py" "$BUILD"
python3 "$ROOT/tests/output_alias_oracle.py" "$BUILD"
python3 "$ROOT/tests/reverse_stress_oracle.py" "$BUILD/libcaelum_port.so"
python3 "$ROOT/verification/check.py"
echo 'PASS: first-party science + real external oracles completed without bypasses'
