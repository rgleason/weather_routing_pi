> Release 1.33 integration: this branch combines Atlas with the installed
> 1.32 partial-result/Professional fixes and Android Qt fixes from `4fce556`.
> The research comparisons below remain the frozen `1fa62c7` baseline study;
> they are not new comparisons against the later Professional policy.
> Execution/display order is Quick, Quick2, Atlas, Standard, Alternative,
> Professional. Saved IDs and membership bit positions are preserved.

# Atlas: experimental sixth routing engine

Atlas is a standalone minimum-time hybrid optimal-control solver, integrated
as **Atlas** in the advanced engine selector. Its stable ID is
`atlas`, selection index 3. The five-engine default Auto/All memberships and default selection
are unchanged. Atlas can be explicitly enabled in Advanced for either mode;
it then runs after Quick2 and before Standard, and All compares its validated arrival.
The original study is isolated on `experimental/sixth-routing-engine`, based
on Weather Routing 1.32 commit `1fa62c7`. Release integration is on
`feature/xweather-1.33`, based on `4fce556`; see
[the 1.33 qualification record](release-1.33.md).

The research question is whether solving for useful spatial and temporal
*decisions*, rather than repeatedly expanding every retained boat heading at
every time layer, can improve solvability and cost. Atlas combines continuous
boundary-value connections, an adaptive spatial decision graph, local polar
velocity geometry and coarse time-dependent Bellman feedback. It does not
invoke any of the five existing search engines. It shares their physical
operations and independent validator.

See [the research record](../research/sixth/README.md),
[mathematics and architecture review](../research/sixth/mathematics.md),
[literature review](../research/sixth/literature.md), and
[benchmark evidence](../research/sixth/results.md). These distinguish measured
results from hypotheses and record limitations; this is not a claim of a
complete or globally optimal solver.

## Build and reproduce

From the plugin root:

```sh
cmake -S vendor/atlas-routing-engine -B build-atlas -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-atlas -j 6
ctest --test-dir build-atlas --output-on-failure
python3 research/sixth/run_benchmark.py --output /tmp/atlas-comparison \
  --generated 128 --repeats 2 --timeout 10 --cpu 4 \
  --shoreline data/shoreline/poly-f-2.3.7.dat.gz
python3 research/sixth/oracle_checks.py --binary build-atlas/tests/atlas_benchmark \
  --output /tmp/atlas-oracles --count 1000
```

CMake needs C++20, Ninja (or another selected generator), pkg-config and
JsonCpp for the benchmark/tests. ecCodes is optional; real-GRIB scenarios fail
explicitly when it is absent. No GUI, network, installed plugin, OpenCPN
profile or global provider binding is needed. The Python representation
experiments additionally use NumPy/SciPy. JSON benchmark inputs can supply
`grib`, `polar` and `epoch`, using a stored regular-grid GRIB and standard
TWA/TWS polar table. Paths and hashes are recorded, not the private datasets.
`--cpu` is optional; choose an allowed logical CPU on the test machine.
The qualified run additionally supplies `--extra-cases
research/sixth/real-cases.json` after substituting available dataset paths.
`research/sixth/verify_baselines.py` belongs to the original `1fa62c7`
study and must be run in that study's checkout. The release uses the later
`4fce556` baseline, whose 51 existing engine/core files are preserved byte for
byte; its separate integrity record is `output/xweather-1.33/baseline-preservation.json`
in the parent OpenCPN workspace. Do not overwrite the historical study's
integrity record with a release comparison.

Postprocess a completed matrix with `analyze_benchmark.py`. Run
`profile_cases.py` separately with `--binary`, `--cases` pointing at the
matrix's `scenarios` directory, and a new `--output` directory. To reproduce
the broader historical references:

```sh
python3 research/sixth/build_track_audit.py
python3 research/sixth/best_known.py --comparison research/sixth/comparison-final \
  --audit build-atlas/tests/atlas_track_audit --output /tmp/atlas-best-known \
  --pools research/sixth/*/
python3 research/sixth/plot_results.py research/sixth/comparison-final \
  --best-known /tmp/atlas-best-known/summary.json
```

The replay tool accepts only identical synthetic physical scenarios. It
recovers recorded controls without searching, reconstructs actual endpoints
and checks them using the current model/validator. Historical runtimes are
excluded. [Native application reproduction](../research/sixth/native-replay.md)
requires separate chart/profile/GRIB setup.

Application use links `atlas_routing_engine` (alias
`weather_routing_engine::atlas`) and includes `atlas_routing/Engine.h`:

```cpp
atlas_routing::Options options;
options.improvementAttempts = 200;
options.onIncumbent = [](const auto& validated) {
  // Copy the snapshot here if it should be retained while search continues.
};
auto result = atlas_routing::Engine{}.route(request, environment, options);
// result.route is the shared RoutingResult; result.atlas contains diagnostics.
```

`improvementAttempts=0` stops after the first independently validated
candidate. Positive values continue each refinement pass. The result exposes
strictly improving incumbent passage times. `onIncumbent` receives each
independently validated improvement synchronously; exceptions are isolated. More allowance need not improve a
route; different discretisations are not ordered approximations, and runtime
is not an optimality certificate.
The standalone callback makes incumbents available during a solve. The host
currently waits for the bounded solve to finish before accepting its route;
it does not yet stream these snapshots as selectable completed routes.

Spatially varying currents also trigger broad three-leg entry/cruise/exit
shape probes and bounded continuous coordinate optimisation. These suggest
useful weather lanes before detailed graph search. Fine physics evaluates
each leg against the original fields; the graph budget and samples remain
the same whether these probes succeed or fail.

## Behaviour and integration

- The state includes time, heading/tack, propulsion/profile/sail plan, minimum
  motor dwell, cumulative motor/wait time, fuel, risk, manoeuvres and departure
  egress. Every delivered leg is explicit physical sailing, motoring or waiting.
- Headings and durations of graph connections are solved together. There is
  no global propagation timestep. The coarse Bellman field has its own
  discretisation for guidance; its predicted moves are never delivered as
  physical motion.
- Wind/current/wave sources, polar interpolation, hard constraints, transition
  penalties, finite forecast coverage and authorised missing-data policies
  use the existing provider contracts. Wind supplied to the engine must
  already be water-relative, as for the other standalone engines.
- Coastline, forbidden boundaries, time-dependent hazards, depth and chart
  validation use the supplied providers. Internal graph vertices do not
  acquire destination ingress privileges. Only the real destination can use
  the shared endpoint policy. Land is not inferred from a coarse weather tile.
- Every candidate is reconstructed with the same physical partition and
  independently checked by `RouteValidator`. Search hints cannot override a
  validation failure. An authoritative provider can request search escalation;
  Atlas returns failure for that request rather than accepting the candidate.
- Optional `geographyGuidance` supplies cheap segment/clearance hints for
  scheduling and spatial refinement. Original point admission, sailed legs,
  waiting and validation retain the authoritative provider. If guided search
  finds no validated route and another pass remains, Atlas retries with the
  original geography. Global work limits still apply. The chart-backed host
  uses immutable GSHHG for these hints, and chart/depth checks for physics.
- Supports deterministic fastest and fastest-under-safety-limits objectives.
  Fuel/risk/comfort objectives are explicitly unsupported. Host comfort
  alternatives can request another bounded fastest solve; Atlas does not
  implement a separate comfort-directed optimiser.
- The host arrival scheduler can call Atlas under a cumulative 90,000 edge
  attempt allowance. The normal per-solve default is 30,000 attempts.
- Irrelevant fixed heading, nominal timestep, Professional effort and reverse
  recovery controls are disabled when Atlas is selected. Experimental spatial
  and feedback parameters are exposed in the standalone API and benchmark,
  not presented as working Professional settings. Auto/All membership has a
  separate Atlas checkbox, initially off.

## Limits and scope

Default roadmap capacity is 2,400 vertices, 24 nearest neighbours plus angular
and anchor connections, two refinement passes, 0.12 NM minimum mixed-cell
resolution (0.06 NM on the next pass), 15-minute arrival buckets and two labels
per bucket. Heading buckets are 15 degrees. These are approximate resource
policies; they can miss a viable narrow passage, topology or arrival history.
The domain initially extends a fraction of route length to each side and
widens on refinement. No completeness or bounded-suboptimality theorem is
claimed. Increasing spatial choices up to the API limit of 20,000 vertices
is supported and was tested in the research matrix.

The PMR allocator bounds managed search storage (96 MiB by default). Separate
limits bound labels, queue entries, edge attempts, weather and geometry calls;
request cancellation/deadlines apply throughout provider access. Provider
caches, validator output, temporary vectors and strings are not all allocated
through PMR. `memoryPeakBytes` is therefore **managed search bytes**, not total
process memory. The benchmark also reports process high-water RSS, including
provider data and startup; warm-run RSS is cumulative within that process.
Linux qualification reads `/proc/self/status` VmHWM, because `getrusage`
alone can inherit a launcher high-water mark across exec.
A synchronous engine cannot interrupt an indefinitely blocking provider call.

Prediction fallback prepares an initial bounded 48-hour window through the
existing provider API. Providers must cover later requests or expand their
coverage themselves; automatic temporal expansion to an arbitrary voyage
horizon is not implemented. The provider-contract tests exercise explicit
coverage, refusal, bounded areas, cancellation and validation preparation.

Finite endpoints must lie between 85 S and 85 N. A coincident start/destination
currently returns an incomplete empty route, because the common validator
requires a nonempty physical route. No artificial zero-duration leg is made.
Environmental and geometry sampling inherit the resolution limits of the
common model and data; passing validation is not a proof of continuous-world
navigation safety. Full GSHHG tests establish shoreline behaviour, not chart
depth coverage. The supplied GSHHG provider has no bathymetry; unavailable real
depth remains unavailable.

Coarse tile averaging is optional and disabled by default: it helped some
smooth fields but hid a narrow favourable jet. Fine physics always samples
the original fields. Coarse weather change detection samples a few positions
and times; a localised feature outside those probes can be missed. The value
field is a scheduling aid, with a heuristic terminal condition, not an
admissible lower bound. The solver cannot certify that a returned route is
within any stated percentage of the continuous optimum.
