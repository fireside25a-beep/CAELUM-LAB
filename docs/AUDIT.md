# Release audit · 0.4.0

The active candidate is the continued `CAELUM_0_4` source tree. The supplied 0.3.0-R1 archive and preserved 0.3.1 binaries were not overwritten. Their identities are recorded separately. The release contains one current application/source version; earlier failed verification attempts are consolidated in `evidence/history/continuation_audit_history.zip`.

## Reproduced defects and corrections

| Finding | Correction and observed regression |
|---|---|
| A huge coordinate masked another midpoint coordinate's residual/inverse error | Coordinate-local trajectory scales and Jacobian equilibration; mixed scales from 10^-200 to 10^200, forward/reverse/repeat checks |
| A receipt could hash a different file read from the one fitted | Single immutable byte snapshot feeds both parsing and hashing; 1,000 concurrent replacement checks |
| Coupled model/basis outputs could be left inconsistent | Staged data, backups, synchronized parents, checksummed recovery journals and serialized recovery/readers; process interruption and injected sync failures |
| Journal rename succeeded but its following directory sync failed before bookkeeping | Recovery ownership begins before the potentially visible journal commit; the observed failure is retained |
| Interrupted recovery cleanup could leave a stale rollback sidecar after deleting backups | Both sidecars record the completed recovery outcome before backup cleanup |
| Text adapter bypassed atomic output and was locale-sensitive | Bounded C-locale serialization and shared atomic sink; the preserved adapter truncated an existing file under RLIMIT_FSIZE, while the current adapter preserved it |
| Unscaled fitting/reductions could overflow or mix incompatible scales | Normalized QR, augmented ridge QR, weighted Huber QR and stable scoring/statistical reductions, with independent references |
| Replay had no explicit source/build/locale/rounding version | v2 computational contract plus frozen v1 dispatch; legacy receipt replay remains successful |
| Symmetric legacy stream-key mixing was unsuitable for new named domains | Tagged named-stream derivation and Philox4x32-10 with upstream known-answer vectors; legacy generator retained only for compatibility |
| Raw polynomial export did not bind its basis to its capsule/receipt | Model/receipt/data hashes and basis-body seal, consistent pair reads and raw-input validation |
| Positive SCM factor products underflowed to zero and could resemble impossible events | Explicit numeric refusal with unchanged output; positive factors are distinguished from actual zero mechanisms |
| A numerically underflowed posterior weight incorrectly narrowed effect support | Support includes every unfalsified active mechanism, including numerically zero-weight candidates |
| Unsupported cubic dictionary aborted a valid three-input active run | Supported degrees continue; unsupported candidates are excluded explicitly |
| Inability to fit a polynomial could be mislabelled a scientific counterexample | Fit failure is returned as an error; only appropriate finite-model impossible-outcome findings produce the tested counterexample state |
| Journal reader attempted a further read after a failed stream operation | Explicit stream-error/size gate before trailing-byte validation |
| Reused test process IDs and stale GUI output directories caused harness failures | Exclusive unique test directories and fresh workbench run directories; corrected tests rerun without suppressing failures |

All pre-existing native interchange, polynomial, dynamics and source-path contracts remain exercised. The public ABI retains major 1 and has minor 3. All 71 declared public functions have concrete definitions and the core exports no unexpected `cp_` functions.

## Executed gates

| Gate | Actual result |
|---|---|
| GCC Debug, GCC optimized, Clang ASan/UBSan | 18/18 native targets in each build |
| Packaged relocation | All 54 packaged native targets passed from isolated working folders without loader-environment paths |
| Independent leak/memory check | Full Valgrind checks for all 18 native targets passed |
| Static analysis | 23 native/adapter translation units, zero diagnostics after corrections |
| Parser mutation campaign | 100,000 deterministic bounded mutations per native profile |
| Deliberate source mutants | Generator multiplier, midpoint tolerance and Huber weighting mutations were detected by regressions |
| File commits | 80 process-crash, recovery, sync/size-limit and alias trials; 1,000 concurrent snapshot checks |
| Engine edge cases | 14 checks, including authority refusal, leakage controls, underflow support, three-input fitting, real falsification and frozen-protocol corruption; also executed through the sanitizer binary |
| Cross-build numerics | 204 comparisons over 12 discovery and 8 dynamics datasets |
| Independent scientific oracle | 33,633 cross-library/replay/corruption assertions, 40 datasets and four full receipt bit sweeps |
| Chronological dynamics oracle | 160 checks over 20 datasets, including future-only perturbations |
| Randomized statistics | 733 SciPy/statsmodels comparisons over 120 seeded trials |
| Existing regression/interchange | 128 NumPy/scikit-learn affine comparisons; actual ONNX/ONNX Runtime, PyArrow and PyTorch producer/consumer checks |
| New scientific study | 120 equal-budget active runs, 626 numerical/calibration assertions and 9,300 finite-SCM enumeration/effect checks |
| Published physical data | 1,000 original measurements, fixed 600/200/200 split, native versus NumPy evaluation and all three predeclared template results |
| Native workbench | Actual X11 Run, Stop, Resume, Replay and clean close; normal and ASan/UBSan execution, actual screenshots |
| Exact formula obligations | Eleven UNSAT proofs plus one required SAT counterexample control |
| Actual C verification | Scoped CBMC Philox memory/arithmetic/counter-preservation checks completed; see formal scope |
| Legacy and installed SDK | Preserved v1 receipt replay, relocated installation, pkg-config consumer, paths containing spaces and restricted ONNX round trips passed |

LeakSanitizer is disabled in this managed ptrace environment. Its absence is not called a leak-check pass: the separate full Valgrind runs provide that evidence. Windows/macOS native hosts were unavailable and are not claimed as qualified. No physical chamber was operated. The formal results cover their stated domains, rather than every application path or statistical/physical claim.

## Measured findings

On the predeclared thirty-seed finite-class fixture, EIG had lower effect error than random selection in 29 trials and higher error in one. Against fixed round-robin it won thirty trials. Equal unit costs made cost-normalized EIG exactly match EIG, as required by that control. These are bounded benchmark findings; descriptive sign tests are retained without a general superiority claim.

The split-conformal mathematical test measured mean exchangeable coverage of 95.048% and shifted-negative-control coverage of 0.138%. The published-data evaluation selected the supplied `cos(theta_1-theta_2)^2` template using training BIC, with held-out RMSE about 60.05 in the original sensor units and measured interval coverage 97%. Native/NumPy prediction discrepancy was below 6e-12 on that evaluation. The losing physical template and affine baseline remain in the evidence.

The precise metadata and per-run values are in the referenced JSON/CSV records. Integrity seals do not authenticate a source, certify entropy quality or establish a physical law. Some process kills before a durable journal can leave harmless staging files; automatic deletion of unknown user files is intentionally avoided. A failed initial experiment may preserve diagnostic snapshots in its new, incomplete directory and returns an error.

The Linux volume is qualified within these declared contracts. The all-platform release gate remains open for actual Windows and macOS execution. `RELEASE_STATUS.json` records that distinction for every priority.


## GitHub-ready publication edition

The presentation edition adds an Apache-2.0 first-party license, validated CFF citation, simple black-background SVG/PNG figures, repository attributes, native CI and an owner-triggered draft-release workflow. Every protected scientific source, header, adapter and qualified native binary remains byte-identical to the sealed 0.4.0 scientific release. The original archive remains preserved separately.

Local publication checks cover the CFF 1.2.0 schema, actionlint, README links, SVG safety, reproducible graph rendering, byte-identical repeated packaging, application-only contents, actual native workbench lifecycle, all 54 freshly extracted native targets, local Git staging/archive integrity, wrong-tag refusal and tamper rejection. GitHub workflows were prepared and linted locally; they were not executed on GitHub and no repository, tag or release was uploaded here. Publication records are in `evidence/qualification/publication/`.
