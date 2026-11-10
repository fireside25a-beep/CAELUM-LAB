# Public C API reference

The installed headers are the authoritative declarations: `caelum_port.h`, `caelum_exchange.h`, `caelum_science.h` and `caelum_lab.h`. ABI major 1 is retained and the minor version is 3. All seventy-one public declarations have compiled definitions; the export audit rejects missing or unexpected `cp_` symbols.

## Status and ownership

`CP_OK` means the operation met its stated software contract. It is not a scientific-validity certificate. Invalid inputs, unrepresentable ranges, numeric failure, rank deficiency, IO failure, corruption, unsupported semantics, unavailable evidence, identity mismatch, adapter failure and allocation failure have separate statuses. Unless explicitly stated, an output result commits only on success. Valid pointer extents remain the caller's responsibility; a C API cannot validate arbitrary foreign pointers.

`cp_dataset_release` frees an owned dataset and clears its descriptor. Arrow and DLPack exports use their standard release/deleter rules. The native plugin handle must be closed. The application does not retain a caller's temporary matrix buffer after a completed fitting call.

## Regression and snapshots

| Functions | Contract |
|---|---|
| `cp_fit_linear_qr`, `cp_fit_linear_checked` | Conditioned v2 affine least squares; 1–16 columns; at least columns+1 rows; checked matrix span; optional residual/scale diagnostics |
| `cp_fit_ridge` | Nonnegative finite penalty; centered augmented QR; unpenalized intercept; zero penalty delegates to least squares; at most 100,000 rows |
| `cp_fit_huber` | Positive finite delta, 1–1,000 iterations; weighted QR IRLS; explicit nonconvergence; at most 100,000 rows |
| `cp_predict_linear` | Bounded validated affine model and finite features; failure does not publish a prediction |
| `cp_dataset_read_csv`, `cp_dataset_write_csv` | Headerless finite float64 features and final response; owned dataset read; staged atomic CSV write |
| `cp_dataset_read_snapshot` | Parses and hashes the same bounded byte snapshot; returns owned dataset and exact-byte SHA-256 |

The new fitting paths require nearest rounding. No opaque third-party solver is invoked. Diagnostics describe the normalized solve and reconstructed original-unit model, not a universal condition-number certificate.

## Scientific laws and dynamics

`cp_law_discover` retains v1 fitting and splitting for compatibility. `cp_law_discover_v2` is the conditioned, Philox-split entry point used by new experiments. Both enforce one to three inputs, degree one to three and a sixteen-term cap; unsupported dictionaries return a range status. An active polynomial run skips an unsupported degree while retaining supported candidates. Failure to obtain a polynomial fit is an error, rather than a scientific counterexample.

`cp_law_predict`, `cp_law_basis` and `cp_law_as_linear` provide prediction, checked transformed features and a constrained affine representation. Constant-only laws are valid predictors but are not exported through the transformed-feature affine capsule contract. `cp_design_disagreement` exhaustively compares a bounded grid; its output is a proposal, not a physical intervention.

`cp_dynamics_discover` uses ordered time samples and a strict chronological holdout. `cp_dynamics_step_midpoint` and `cp_dynamics_trace_midpoint` commit only complete successful solutions. `cp_dynamics_audit_holdout` reports forward prediction, reverse-from-observed prediction and numerical cycle error separately. Signed steps do not establish physically invertible dynamics.

`cp_randomized_effect` requires an explicit randomized-assignment declaration and at least two observations per arm. Its normal interval is approximate. `cp_holm_adjust` requires valid p-values and nonoverlapping input/output buffers. These preconditions are not inferred from a dataset filename.

## Entropy and identity

`cp_random_init` selects deterministic, recorded, RDSEED or RDRAND provenance with bounded native retries. `cp_random_at`/`cp_random_unit` preserve the legacy generator. `cp_philox4x32_10` exposes the complete 128-bit-counter/64-bit-key generator; input/output counter aliasing is supported. `cp_random_v2_at` maps a stream and a 64-bit counter to its first 64 output bits. `cp_random_named` derives a tagged seed/stream domain from root seed, root stream, purpose and epoch.

`cp_source_identity` and `cp_build_identity` return immutable identity strings owned by the library. Their lifetime is the loaded library's lifetime. The source identity covers native source, headers, adapter source and build configuration. The build identity adds compiler bytes/version, effective configuration flags, target/system identity and linker flags. A runtime file manifest separately binds distributed binary bytes.

`cp_cpu_query` exposes bounded CPUID features. `cp_native_word` obtains a real supported instruction word or fails explicitly. `cp_native_rip`, `cp_native_tsc`, `cp_native_rflags` and `cp_native_xcr0` are address/timing/status instruments with architecture and permission guards, not extra entropy sources.

## Typed expressions

`cp_variable_validate` checks a bounded name, finite increasing domain, declared role, controllability and seven bounded dimension exponents. `cp_expression_unit` validates a topologically ordered DAG of at most 256 nodes and returns the final dimension. `cp_expression_evaluate` evaluates its final value and optional analytic input gradient on a valid-domain input. `cp_expression_hash` binds a canonical commutative tree and complete variable contract. `cp_law_expression` constructs a typed polynomial expression, assigning compatible coefficient dimensions for the supplied input and response units.

The seven exponent axes use this order: length (metre), mass (kilogram), time (second), electric current (ampere), thermodynamic temperature (kelvin), amount of substance (mole), luminous intensity (candela). All-zero exponents are dimensionless; radians use that dimensionless contract.

Supported operators are constants, variables, addition, subtraction, multiplication, division, integer powers −8 through 8, exponential, logarithm, sine and cosine. This evaluator is not a claim that the polynomial search discovers every supported transcendental operator.

## Causal and uncertainty operators

| Functions | Scope |
|---|---|
| `cp_scm_validate` | Acyclic binary model, up to four variables, consistent parent-indexed finite probabilities |
| `cp_scm_joint` | Finite observational/interventional joint; intervention values must be a subset of the intervention mask |
| `cp_scm_effect` | Exact finite-class expectation difference for two distinct variables in the supplied SCM |
| `cp_binary_pc` | Bounded PC-Stable estimate, nominal G-tests, expected cells at least five; ambiguity is explicit |
| `cp_causal_adjustment` | Standardization with caller-supplied adjustment set and positivity; causal justification is external |
| `cp_finite_eig` | Bernoulli finite-class expected information gain, up to eight supplied models; normalized finite nonnegative weights |
| `cp_conformal_radius` | Absolute residual order statistic, 1–1,000,000 independent calibration residuals; insufficient finite rank returns unavailable |

## Experiment authority

`cp_lab_run(protocol, bundle, step_limit, resume, summary)` creates a new output directory for a new protocol, or resumes an existing validated bundle when `resume=1`. Zero step limit runs to the configured stopping condition; a positive limit checkpoints after that many additional steps. The summary commits on success. An unavailable authority lock prevents concurrent writers. Failed initialization may retain an incomplete output directory and frozen diagnostic inputs; the application does not silently reuse or overwrite it as a new experiment.

`cp_lab_run_summary` reads a validated authoritative checkpoint. `cp_lab_run_point` reads a bounded selected row, its pre-measurement prediction, observed response and model disagreement variance. `cp_lab_run_replay` validates frozen protocol/data, source/build identity and computational history. These functions share the core used by the workbench. They do not parse a derived `result.json` as scientific authority.

Phases are `CREATED`, `READY`, `RUNNING`, `STOPPED`, `SEALED` and `COUNTEREXAMPLE`. A sealed finite-model result remains conditional on its supplied class. `rejected_models` counts inactive candidates: polynomial fit/unavailable-dictionary exclusions are not causal falsifications. A finite mechanism is logically falsified only by an impossible observed outcome. Underflow of a positive posterior weight does not remove an active model from effect-support bounds.

## Receipt and model exchange

`cp_lab_discover_file`, `cp_lab_replay_file` and `cp_lab_receipt_law` create, replay and reconstruct scientific receipts. `cp_lab_export_file` commits a bound capsule/basis pair; `cp_lab_predict_export` validates a consistent pair before transforming raw inputs and predicting. Existing bare-CSV receipts explicitly carry no physical units.

`cp_write_atomic_file` is the bounded shared IO sink for owned binary/text artifacts, including the first-party text adapter. Its caller supplies semantic and path authorization. `cp_write_atomic_file` is the bounded shared IO sink for owned binary/text artifacts, including the first-party text adapter. Its caller supplies semantic and path authorization. `cp_capsule_save`/`cp_capsule_load` retain capsule v1. `cp_onnx_export_linear`/`cp_onnx_import_linear` retain the constrained Gemm subset. `cp_adapter_open`/`cp_adapter_close` retain the versioned native affine plugin contract. `caelum_exchange.h` defines the complete bounded Arrow and DLPack producer/consumer interfaces and ownership rules. `cp_sha256`, `cp_status_string` and `cp_abi_version` expose integrity and contract metadata.
