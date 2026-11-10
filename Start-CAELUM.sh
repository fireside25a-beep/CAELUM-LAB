#!/usr/bin/env bash
set -euo pipefail
CAELUM_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$CAELUM_ROOT/runs"
CAELUM_RUN="$CAELUM_ROOT/runs/experiment_$(date -u +%Y%m%dT%H%M%SZ)_$$"
exec "$CAELUM_ROOT/native/bin/caelum-workbench" "$CAELUM_ROOT/examples/finite_scm/protocol.json" "$CAELUM_RUN"
