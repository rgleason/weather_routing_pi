# Sixth-engine research record

This directory contains a reproducible research lab and evidence for Atlas.
The baseline is Weather Routing 1.32, `1fa62c7`. The release 1.30 tree did not
contain all five strategies requested for comparison. No baseline algorithm,
physical motion operation or common validator was changed.

Start with [the mathematical formulation](mathematics.md), then the
[primary-source literature review](literature.md),
[results and qualifications](results.md), and
[public API/build instructions](../../docs/atlas-engine.md).

## Experiments

- `prototypes.py`: independent static geometry comparison of a local Bellman
  mesh, visibility graph and multistart continuous waypoint optimisation.
- `formulation_prototypes.py`: forward Eulerian Hamilton–Jacobi reachability
  and continuous Zermelo/Pontryagin shooting. These isolate mathematical
  representations. They are not substitutes for the production sailing model
  and their Python timings are not compared with C++ engine timings.
- `run_benchmark.py`: deterministic broad scenario generator and serial
  six-engine runner. Frozen executable snapshots, source/input hashes,
  deadlines, cold/warm provider caches, process failures and raw records.
- `oracle_checks.py`: 1,000 seeded short problems with independent analytic or
  exact-visibility reference costs. Numerical/model tolerances are explicit.
- `verify_baselines.py`: byte-for-byte baseline preservation check.
- `real-cases.json`: local stored GRIB and real polar inputs; external files
  are not embedded in the repository. Substitute paths to reproduce elsewhere.
- `large-space/results.json`, `weather-first/results.json`: parameter and
  candidate-architecture ablations. Increasing spatial choice was measured,
  as were weather-first feedback and coarse field averaging.
- `lane-family/results.json`: three-leg weather-band shape optimisation
  versus disabled lane probes across 16 generated jets.
- `chart-guidance-ab.json`: actual OpenCPN chart/depth replay with full chart
  scheduling versus cheap GSHHG hints; original motion/validation authority.
- `profile_cases.py`: separate provider-instrumented profiling, with explicit
  overlapping phase/provider timer semantics.
- `comparison-final`: qualified frozen serial six-engine matrix; CPU affinity,
  complete case inputs, provenance, raw records and analysis are retained.
- `oracle-qualified-1000`: 1,000 independent references using the same frozen
  binary as the qualified matrix. Earlier oracle directories are development
  runs, not additional independent statistical samples.
- `audit_tracks.cpp`, `build_track_audit.py`, `best_known.py`: recover faster
  historical synthetic control sequences, reproduce physical endpoints and
  independently revalidate them with the current model. `best-known-final`
  extends ETA references without importing historical timings.

The `smoke-*`, `atlas-screen`, `coastal-*`, `harbor-*`, `hadley-*` and
`pilot-newton` directories record development experiments, including failures.
They are **not** the qualified final comparison. `comparison-v1` is the broad discovery run;
its timing was overlapped by development work and its RSS inherited a launcher
high-water mark. Later measurements use `/proc/self/status` VmHWM, streaming
input hashing and a frozen, otherwise idle serial run. In particular `pilot-newton`
predates immutable executable snapshots and had mixed binary provenance;
some early coastal inputs incorrectly passed a compressed shoreline file to
a raw reader, and some draft Hadley endpoints were on land or at the wrong
harbour. These were corrected, not silently counted as engine failures or
excluded to improve a final denominator. The final case coordinates and
source/input hashes are authoritative.

`profile-coastal.er` is an attempted gprofng sampling experiment. Its header
reported that the interval timer changed; the sampling totals are unreliable
and are not used as quantitative evidence. Phase timers and optional explicit
provider timers provide the reported profiling measurements instead.
