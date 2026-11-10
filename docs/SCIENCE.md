# Scientific methods and assumptions

CAELUM 0.4.0 extends the existing scientific application. It retains the native interchange contracts, bounded polynomial workflow, chronological dynamics and historical v1 receipt replay. New functionality is additive and shares the same C authority.

## Experiment execution

A `caelum.discovery.v1` protocol declares the variables, their domains and units, permitted controls, input measurements, model family, seed policy, costs and budget. Candidate rows are **already measured, imported interventions**. The engine selects an unused candidate from its inputs and costs and only then consumes its response. It does not generate production observations or operate laboratory hardware. The selectors receive no response or holdout pointer.

An initial observation snapshot, candidate snapshot and independent holdout are frozen in each bundle. Polynomial runs additionally require a separate calibration snapshot. A protocol hash and all snapshot hashes are stored with the captured original seed, derived random stream, models, selected rows, outcomes and event history. A per-bundle kernel lock excludes concurrent authorities. Short file transactions serialize recovery and readers across processes.

Run budgets are fixed by the protocol. A step limit stops and checkpoints the configured experiment; it does not silently shrink its total budget. Resume continues from the saved model and action state. Replay uses captured randomness and frozen data rather than drawing another hardware seed. Source/build mismatches or modified snapshots are rejected. A finding that falsifies every supplied deterministic mechanism is recorded as `COUNTEREXAMPLE`, rather than relabelled a successful discovery.

## Numerical learning

Affine least squares normalizes and centers features and normalizes the response before Householder QR. Coefficients are reconstructed in original units. Rank or unrepresentable-result failures remain explicit. Ridge solves a centered, augmented least-squares problem with an unpenalized intercept and a penalty on original-unit slopes. Huber IRLS uses weighted QR at every step; nonconvergence leaves the caller's result unchanged. No normal matrix is formed by these new solvers.

Sparse polynomial discovery is bounded to one to three inputs, degree at most three, and sixteen terms. Forward selection uses training BIC and never selects on its internal held-out rows. New experiments use algorithm v2, normalized reductions and Philox splitting. The public legacy discovery entry point retains algorithm v1 for historical receipt compatibility. BIC ensemble weights in polynomial active runs are a scoring approximation, not an exact continuous Bayesian posterior.

## Meaning and expressions

Variable contracts include a finite domain, role, name, controllability and seven SI-base-dimension exponents. Dimensionless radians may appear in trigonometric expressions. Addition/subtraction require equal dimensions; multiplication, division and integer powers combine dimensions; transcendental arguments must be dimensionless. Invalid domains, division by zero, overflow and nonfinite gradients are refused. Canonical expression hashes bind the operation tree and variable domain/role contract. Declaring coefficient units compatible with a polynomial does not establish a physical law.

## Causal scope

The SCM evaluator supports acyclic, fully observed binary models with at most four variables and explicit Bernoulli mechanism tables. It calculates joint and interventional probabilities by finite truncated factorization in binary64 arithmetic, with explicit numerical refusal when a positive event underflows to zero. The active finite-SCM engine presently accepts a supplied class of two-variable models, updates their weights from observational and imported interventional outcomes, and selects interventions by disagreement or exact finite-class information gain. Its conclusions are conditional on those supplied models and declarations.

PC-Stable estimation is bounded to four binary variables, nominal conditional G-tests, and adequate expected cell counts. It returns graph ambiguity explicitly; it is not latent-confounder discovery or a universal identification theorem. `cp_causal_adjustment` is a statistical standardization operator: the caller must independently justify the adjustment set and positivity before interpreting its result causally.

A positive event probability that underflows to zero during SCM multiplication is refused as a numeric error, rather than reported as an impossible event. A numerical posterior weight can underflow to zero. This does **not** falsify a model. Effect-support bounds include every still-active model; only an impossible observed outcome can remove a deterministic candidate in the finite engine. The support is a model-class range, not a frequentist confidence interval. The reported posterior mean and entropy remain floating-point computations.

## Uncertainty

Split-conformal intervals use independent absolute calibration residuals and order statistic `ceil((n+1)*(1-alpha))`. If a finite requested rank does not exist, the API returns unavailable. Calibration never enters selection or fitting. Finite-sample marginal coverage requires exchangeability between calibration and future observations and a model fixed independently of calibration; it is not a guarantee under distribution shift or for every individual input.

The validation study records both exchangeable coverage and a deliberately shifted negative control. It also records actual coverage on a reserved block of published measurements. Approximate randomized-treatment normal intervals and Holm adjustment retain their original declared-assumption contracts.

## Numerical time reversal

The nonuniform three-point derivative is exact for quadratic time polynomials in real arithmetic. Dynamics fitting reserves a strict future block. Midpoint convergence and inverse/repeat acceptance use each coordinate's own local trajectory scale, including small coordinates; another coordinate cannot hide an error. Jacobian row/column equilibration improves unit-scale handling.

Backward prediction error against observations, forward error against observations and a numerical forward/backward cycle are different measurements. A small solver cycle error certifies neither the learned dynamics nor physical reversibility. Ambiguous or poorly conditioned nonlinear solves are refused without modifying output.

## Executed scientific studies

The preregistered finite-class study contains thirty seeds and four equal-budget policies, with 120 complete runs and replay records. All costs are one in this control, so cost-normalized information gain must match ordinary information gain. Results include losing trials and descriptive sign-test comparisons; no universal superiority claim follows.

The empirical evaluation uses 1,000 original `lt_malus_v1/white_255.csv` measurements published by Gamella, Peters and Buhlmann, DOI `10.1038/s42256-024-00964-x`. Three physical/predictive templates were declared before downloading and analyzing the measurements. The native typed evaluator and conditioned QR are compared against NumPy. Selection uses only the first 600 rows; 200 subsequent rows calibrate intervals and 200 remain held out. This evaluates supplied templates on published measurements; it does not claim automated discovery of Malus' law or operation of the physical chamber.
