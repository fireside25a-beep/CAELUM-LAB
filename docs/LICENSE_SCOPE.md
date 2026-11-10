# License scope

Copyright 2026 Alexandru Moraru. First-party CAELUM is licensed under **Apache-2.0**, with the complete unmodified license at the repository root. This permits commercial and noncommercial use under its terms, including the applicable contributor patent grant. Scientific citation is requested; it is not an added license condition. No endorsement or trademark permission is implied.

| Material | License |
|---|---|
| First-party source, headers, scripts and native CAELUM binaries | Apache-2.0 |
| First-party documentation, original mathematical fixtures and benchmark outputs | Apache-2.0, except the attributed materials below |
| Published Causal Chambers measurements in `fixtures/empirical/` | CC BY 4.0, full license and provenance adjacent |
| Derived published-measurement tables in `evidence/qualification/empirical/` and plots reproducing those measurements | CC BY 4.0; credit Gamella, Peters and Bühlmann and retain the documented transformations |
| Z3 verification executable | MIT; adjacent license |
| CBMC verification executable and included MiniSAT/CaDiCaL solver code | Their adjacent upstream licenses and required acknowledgement in `NOTICE.md` |
| Citation File Format schema | CC BY 4.0; adjacent license and attribution |
| Historical archives and compatibility evidence | Their preserved notices; no rewriting of earlier artifacts |

Empirical plots are based on *Causal chambers as a real-world physical testbed for AI methodology*, Gamella, Peters and Bühlmann (2025), DOI 10.1038/s42256-024-00964-x. Angles are converted from degrees to radians and fixed row blocks are reserved for fitting, calibration and held-out evaluation. The software was evaluated on the authors' existing measurements; no chamber was operated here.

External validation frameworks and system libraries are separate distributions. The application-only download includes CAELUM's native binaries and example, not the full research volume's bundled proof tools or empirical archive. Their presence in the repository does not make them dependencies of the application runtime.
