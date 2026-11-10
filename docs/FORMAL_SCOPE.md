# Exact formal-verification scope

The verification directory contains executable obligations, source hashes, actual tool outputs, Z3 5.4.0 and CBMC 6.11.0 for Linux x86-64. These tools are independent verifiers, not production numerical dependencies. No Lean dependency was needed for these particular obligations.

| Obligation | Domain and result |
|---|---|
| Midpoint symmetry | Reals; reversing the equation negates the step, UNSAT counterexample |
| Affine inverse uniqueness | Reals; scalar affine step denominators nonzero, UNSAT counterexample |
| Harmonic energy | Reals; midpoint oscillator preserves quadratic norm, UNSAT counterexample |
| Nonuniform derivative | Reals; positive neighboring gaps, quadratic-polynomial exactness, UNSAT counterexample |
| Matrix span | Integers; legal indices stay inside the checked representability bound, UNSAT counterexample |
| Chronological stencil | Integers; training centers/neighbors precede the future block, UNSAT counterexample |
| XOR-shift inverses | All 64-bit words, shifts 27/30/31, three UNSAT counterexamples |
| Odd multiplier inverses | 64-bit modular arithmetic, two finalizer constants, two UNSAT counterexamples |
| Nonlinear branch control | Reals; paired midpoint solves can use different roots, SAT as expected |
| Actual Philox C safety | Arbitrary 128-bit counter and 64-bit key; ten-round implementation, valid-array and in-place alias memory safety, counter nonmutation, signed-arithmetic checks and completed loop unwinding; CBMC verification successful |

The eleven UNSAT obligations and one SAT negative control are mathematical models of specific formulas. They do not mechanically prove every C statement or floating-point result. The CBMC harness operates on the actual `src/random_v2.c`, with explicitly valid bounded pointer objects and a complete ten-round unwind. It does not certify all RNG entry points, distribution quality, cryptographic security, hardware entropy or statistical inference. Reference known-answer vectors are a separate native test.

An earlier CBMC attempt with an insufficient library-loop unwind was rejected. A more ambitious output-equivalence attempt was interrupted before completion and is not counted as a proof. Both records are retained in audit history. Unknown results, timeouts, unexpected solver output and nonzero verification exits are never converted to PASS.

Source-grounded compiler warnings, native tests, independent numerical comparisons, mutation controls, actual process interruption, sanitizers and full Valgrind checks provide different evidence about executed software. None supplies an exhaustive proof of the application, its compiler, an operating system, every possible dataset or a physical law.

This product includes software developed by Daniel Kroening, Edmund Clarke, Computer Science Department, University of Oxford, Computer Science Department, Carnegie Mellon University. Complete CBMC, MiniSAT and CaDiCaL notices are adjacent to the bundled verifier. Z3's MIT license is adjacent to its executable.
