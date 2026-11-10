# Formats and compatibility

## Stable public interchange

The existing 200-byte, little-endian capsule v1 remains compatible with C ABI major 1. It contains a bounded affine model and SHA-256 integrity. A capsule alone does not describe a causal model, units or a raw polynomial input transformation. SHA-256 seals are integrity records, not signatures or evidence that a scientific claim is true.

ONNX remains the explicitly constrained IR9/opset13 double-precision Gemm contract. Arbitrary graphs are rejected. Arrow C Data and versioned CPU float64 DLPack retain their documented ownership and release-callback contracts. The affine Python adapter is an optional external exchange endpoint; no external scientific framework is linked into the native authority.

## Scientific receipts

`CAELUM-EXPERIMENT-A-v1` receipts replay through the preserved v1 QR/search/split implementation. New receipts use `CAELUM-EXPERIMENT-A-v2`, polynomial-forward-BIC-v2 and Philox4x32-10. They record the exact data hash, captured entropy provenance, split, selected terms, scores, source identity, build identity, compiler, nearest rounding and C numeric locale. Canonical reconstruction and the integrity seal must both agree. An identity mismatch is explicit; cross-compiler numerical comparison is a separate test and does not turn a different build into a bit-exact replay.

The library uses a private immutable numeric locale without changing the caller's locale. The standalone command-line process begins in the C locale. Explicit deterministic/recorded seeds are never replaced. RDSEED/RDRAND are feature-gated, carry-checked and bounded-retry native instructions; no OS-random fallback supplies a hidden replacement. Capturing such an instruction does not certify entropy quality.

## Bound polynomial export

The v2 basis manifest binds capsule bytes, receipt, source-data hash and integer powers, then seals its own body. Raw-input prediction reads a consistent capsule/basis snapshot, checks both seals and applies the recorded transformation. Existing headerless CSV discovery has no supplied physical-unit metadata, so its basis explicitly says `units=unspecified`. No units are inferred. Constant-only laws remain unsupported by this transformed-feature capsule exporter, matching the existing affine-capsule feature contract.

Coupled outputs use a checksummed `CPPAIR3` local recovery journal, same-directory staged files and backups. Files and POSIX parent directories are synchronized. Recovery produces a complete old or complete new pair under the tested interruptions. Both sidecars describe the completed recovery outcome before backups are removed. Kernel locks prevent a reader from rolling back an active writer. Hostile filesystem replacement and authenticated storage are outside this local recovery protocol. A kill before the first journal becomes durable may leave harmless staged temporary files; no unknown user files are automatically deleted.

## Experiment protocol

The strict bounded JSON root accepts `schema_version=1`, `experiment_kind=caelum.discovery.v1`, `family`, `policy`, `observations`, `candidates`, `holdout`, `variables`, `entropy`, `assumptions`, `budget`, `measurement_noise`, and either `cost_per_action` or `costs`. Polynomial runs additionally require `calibration`; finite-SCM runs require `hypotheses`. Unknown or duplicate decoded keys, malformed UTF-8, nonfinite numbers and unsupported structures fail closed.

Families are `polynomial` and `finite_scm`. Policies are `random`, `round_robin`, `disagreement`, `eig` and `cost_eig`; polynomial runs support the first three. Variable entries carry `name`, seven integer `unit` exponents, `lower`, `upper`, `role`, and `controllable`. Finite-SCM variables are binary and dimensionless. Assumptions explicitly declare intervention authority, full observability and IID measurement noise. These declarations are caller preconditions, not external attestations.

Entropy entries specify `mode`, hexadecimal `stream`, `purpose`, and a hexadecimal `seed` only for deterministic/recorded modes. Protocol hex words contain one to sixteen hex digits. Named streams derive from root seed, root stream, purpose and epoch with a tagged encoding. Selection and fitting have explicit domains; the original root seed and instruction attempts remain available in the result.

Candidate pools are limited to 4,096 rows; action histories to 256 steps; native protocols to 262,144 bytes, 4,096 JSON tokens and depth 32. Thus aggregate token limits may constrain arrays before their individual row cap. Input snapshots are bounded to 32 MiB each. CSVs are headerless finite numeric rows, with the final column as response. Paths in a new protocol are relative to the protocol file. Saved replay reads the bundle's frozen inputs rather than the original paths.

## Run bundle

`checkpoint.cpc` is a checksummed **local build-specific** `CPRUN01` state record, not a portable cross-ABI model format. It contains the protocol contract, captured seed, models, actions, observations, hashes, summary and canonical events. `events.jsonl` is committed with the checkpoint as a pair. `result.json` is a derived human-readable view with the authoritative checkpoint hash. The public summary/point APIs read the validated checkpoint; a crash can leave a stale derived result view, which a successful resume regenerates.

`inputs/` contains the immutable snapshots, and `protocol.json` retains the exact original protocol bytes. `authority.lock` is a runtime lock file with no scientific payload. A new run refuses an existing output directory. Resume validates its state and input identities. Replay does not draw fresh hardware randomness and compares the resulting action/response/weight history and models.

The application binaries are Linux x86-64 ELF. The source retains platform branches, but Windows/macOS native execution is not qualified by this package. No local checkpoint portability or nonexistent desktop backend is implied.
