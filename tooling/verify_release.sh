#!/usr/bin/env bash
# Source-grounded verification for Linux x86-64 hosts with GCC, Clang and
# third-party numeric oracle packages. No bypasses or simulated successes.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUDIT="${1:-$ROOT/release-validation}"
mkdir -p "$AUDIT"
AUDIT="$(cd "$AUDIT" && pwd)"
export ASAN_OPTIONS="detect_leaks=${CP_DETECT_LEAKS:-1}"
export UBSAN_OPTIONS=halt_on_error=1
"$ROOT/tooling/verify_native.sh" "$AUDIT/debug" | tee "$AUDIT/debug_and_external.log"
cmake -S "$ROOT" -B "$AUDIT/sanitized" -DCMAKE_C_COMPILER="${CP_SANITIZER_CC:-clang}" -DCMAKE_BUILD_TYPE=Debug -DCP_BUILD_TESTS=ON \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' \
  '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined' > "$AUDIT/sanitize_configure.log"
cmake --build "$AUDIT/sanitized" --parallel 4 > "$AUDIT/sanitize_build.log"
ctest --test-dir "$AUDIT/sanitized" --output-on-failure | tee "$AUDIT/sanitize_ctest.log"
if [[ "${CP_DETECT_LEAKS:-1}" == 0 ]]; then
  if [[ -z "${CP_VALGRIND_PATH:-}" ]]; then
    echo 'VERIFICATION INCOMPLETE: LeakSanitizer disabled without an independent leak checker' >&2; exit 3
  fi
  python3 "$ROOT/tests/leak_oracle.py" "$AUDIT/debug" "$CP_VALGRIND_PATH" "$AUDIT/leak-checks" | tee "$AUDIT/leak_oracle.log"
fi
cmake -S "$ROOT" -B "$AUDIT/optimized" -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release -DCP_BUILD_TESTS=ON \
  '-DCMAKE_C_FLAGS=-O3 -fstrict-aliasing' > "$AUDIT/optimized_configure.log"
cmake --build "$AUDIT/optimized" --parallel 4 > "$AUDIT/optimized_build.log"
ctest --test-dir "$AUDIT/optimized" --output-on-failure | tee "$AUDIT/optimized_ctest.log"
"$AUDIT/debug/caelum-port" lab-dynamics "$ROOT/fixtures/science/harmonic_trajectory.csv" 1 2081 > "$AUDIT/debug_dynamics.txt"
"$AUDIT/optimized/caelum-port" lab-dynamics "$ROOT/fixtures/science/harmonic_trajectory.csv" 1 2081 > "$AUDIT/optimized_dynamics.txt"
diff -u "$AUDIT/debug_dynamics.txt" "$AUDIT/optimized_dynamics.txt" > "$AUDIT/cross_build_diff.txt"
# Cross-build drift is a separate statistical oracle, not a single byte comparison.
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 MKL_NUM_THREADS=1 PYTHONDONTWRITEBYTECODE=1
python3 "$ROOT/tests/build_drift_oracle.py" \
  "$AUDIT/debug/caelum-port" "$AUDIT/optimized/caelum-port" "$AUDIT/sanitized/caelum-port" \
  | tee "$AUDIT/build_drift_oracle.log"
cmake --install "$AUDIT/debug" --prefix "$AUDIT/install" > "$AUDIT/install.log"
export PKG_CONFIG_PATH="$AUDIT/install/lib/pkgconfig"
if ! pkg-config --exists caelum-port; then echo 'Missing installed pkg-config record' >&2; exit 3; fi
cc -std=c11 -Wall -Wextra -Werror "$ROOT/examples/science_consumer.c" -o "$AUDIT/installed_science_consumer" \
  $(pkg-config --cflags --libs caelum-port) -lm -Wl,-rpath,"$AUDIT/install/lib"
"$AUDIT/installed_science_consumer" | tee "$AUDIT/installed_consumer.log"
readelf -d "$AUDIT/install/lib/libcaelum_port.so" > "$AUDIT/installed_binary_readelf.txt"
nm -D --defined-only "$AUDIT/install/lib/libcaelum_port.so" > "$AUDIT/installed_binary_exports.txt"
python3 "$ROOT/tests/package_oracle.py" "$AUDIT/install" "$ROOT" | tee "$AUDIT/package_oracle.log"
echo 'PASS: native, external, sanitizers, optimized, bit-identical report and installed SDK consumer'
