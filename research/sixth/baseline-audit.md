# Baseline and infrastructure audit

Baseline selection: `1fa62c7`, Weather Routing 1.32. The 1.30 release worktree
available locally exposed only three engine choices; 1.32 contains the five
requested independent strategies. The new isolated worktree is
`/home/paul/src/OpenCPN/.worktrees/xweather-sixth`. The original plugin checkout
and other worktrees were not edited.

## Search policies

| User title | ID / source | Relevant structure |
|---|---|---|
| Quick | `original`, `vendor/original-routing-engine/src/Engine.cpp`, `Contour.cpp` | Hardened catalogue contour solver, bounded headings/search/work. Cheap contour expansion followed by explicit physical repair and independent candidate validation. |
| Quick2 | `quick2`, `vendor/quick2-routing-engine/src/Engine.cpp` | Time-layer frontier, grid cells, bounded heading fan, angular diversity, retained lineage/work limits. |
| Standard | `quick`, `vendor/weather-routing-engine/src/QuickEngine.cpp` | Adaptive steps/headings, bounded beam/cells and compact lineage, endpoint and wider recovery policies. |
| Alternative | `alternative`, `vendor/weather-routing-engine/src/AlternativeEngine.cpp` | Bounded sector/frontier selection with independent heading/frontier policy and shared physical checks. |
| Professional | `main`, `vendor/weather-routing-engine/src/Engine.cpp` | Coastal endpoint search, corridor/fringe filtering, time-layer propagation with cell/sector pruning, wider forward/reverse/frontier/graph recovery and cumulative effort. Host first offers a validated Quick incumbent. |

The historical name “Cowboy” is not a sixth baseline implementation in this
worktree. Quick's pinned source retains catalogue v1.15.45.7 contour machinery
(upstream `ed07e567c22e5c65d1711f7a4ded654101912043`). `ReduceClosePoints`,
`IsoRoute::Normalize`, self-intersection splitting, `Merge` and child contours
are present in `Contour.cpp`. This is genuine contour shaping/normalisation;
it should not be described as absent from all five engines. What an outward
contour omits is a full collection of interior arrival/mode/resource histories.
An Eulerian level-set PDE and explicit contour surgery are different numerical
representations even if both produce visible fronts.

`verify_baselines.py` compared 48 tracked files in the three existing vendor
libraries against the baseline. All were byte-for-byte unchanged. This also
covers the shared core motion/validator implementation. See
`baseline-integrity.json` for file hashes. Host changes are explicit Atlas
selection/dispatch and presentation branches. Auto/All gain an explicit
Atlas opt-in bit (32); their default masks (23/31) and selection remain unchanged.

## Physical model and acceptance

`MotionKernel.h` and `RoutingInternal.h` expose shared `MotionState`, checked
heading-duration propagation, terminal connection and stationary waiting.
State includes chronological position/time, heading/tack, mode/profile/sail
plan, mode dwell and cumulative resource/history fields. These are physical
Markov information, not a requirement to propagate in fixed global time
layers. Atlas reuses these operations without reusing a baseline's search.

Checked motion performs bounded midpoint integration, adds ground current,
evaluates the selected polar/mode at the queried position/time, checks hard
weather/angle/resource constraints, transitions/penalties and land/forbidden
segments. Atlas uses at most five-minute integration slices; exact coastal
terminal connections use the common more finely sliced terminal operation.
The real request destination is retained throughout. Integrated endpoints are
kept rather than snapped to an intended spatial waypoint.

The common `RouteValidator` reconstructs chronological consistency and
performance and checks integrated segments as well as exported chords. It
uses the original provider and permits a separate authoritative validation
hook. Atlas forwards `prepareValidationRoute` and escalation semantics. Failed
validation never becomes a successful route. Shared source/fallback warnings
and metrics are preserved; metrics are reset for each improving incumbent.

## Weather, polars and currents

`OpenCpnWeatherProvider` in `src/engine/native/OpenCpnAdapter.cpp` delegates to
the host timeline/climatology policy. Coverage preflight and actual route
sampling are separate: a departure frame alone does not describe every future
forecast time. The provider owns a bounded per-candidate shared cache with a
single-entry hot cache and a small local cache. Keys use canonical 15-minute,
0.01-degree samples; the underlying values are sampled canonically, not at
whichever request happened to populate the bucket first. Cache capacity is
32,768 for fast engines and 500,000 otherwise. Candidate isolation prevents
another departure's cache contents changing the sampled approximation.
Atlas does not alter this policy.

`OpenCpnPerformanceModel` supplies the actual boat polars, position/time-based
night efficiency, upwind/downwind efficiency, available sail plans and motor
policy. The standalone native `PolarPerformanceModel` supplies the same
contract for table inputs. Atlas forwards the position/time-aware overloads;
it does not assume every performance provider is spatially or temporally
constant. A sampled best speed is a hint, not a certified global speed bound.

Environment resolution handles requested GRIB currents, prediction/climatology
fallbacks and explicit assumed-zero policies. Atlas's prediction wrapper
prepares bounded initial 48-hour coverage when preflight requests fallback, forwards
`ensureCoverage`, and counts queries. Missing required data fails closed.
Automatic expansion to an arbitrary voyage horizon is not implemented;
later requests depend on provider coverage or its own expansion behaviour.
The benchmark's real-GRIB adapter bilinearly interpolates supported regular
U/V grids, linearly interpolates time, converts m/s to knots and subtracts
current from ground-relative GRIB wind before supplying water-relative wind.
All six benchmark engines receive the same adapter; it is deliberately not
presented as a measurement of the OpenCPN timeline cache implementation.

## Geography, depth and charts

The standalone `GshhgProvider` uses the existing indexed shoreline reader,
including full resolution, buffered segments, dateline and short/long chord
checks. It has no depth. Synthetic bathymetry is explicitly a flat deep seabed
unless a fixture sets depth/missing-depth; a real shoreline fixture forwards
unavailable depth instead of inventing deep water.

The host land provider combines `ConstraintChecker`, configured GSHHG,
chart-safety raster/service requests, course/diversion limits, boundaries and
cyclone-time checks. `prepareValidationRoute` can prefetch missing chart tiles
in bounded rounds, and dense final replay can use authoritative chart checks.
A rejected segment is not generally proof of a coastline: it can be a course,
boundary, cyclone or missing-data rejection. Atlas does not extract assumed
coast vertices from arbitrary rejection reasons.

Host point/clearance semantics differ from an exact polygon provider: Atlas uses exact GSHHG point checks and a conservative bounded clearance ladder when GSHHG is the propagation authority;
for most other native engines, and Atlas with authoritative chart propagation,
point checks are permissive and clearance is a stand-off
probe returning zero or infinity, not a distance field. Consequently adaptive
point-based geography can be less informative in a chart-backed host run than
in the standalone full-GSHHG tests. Actual edge/validation checks remain
authoritative. Atlas's optional immutable GSHHG scheduling hints address this
gap for chart-backed runs, without changing the physical provider. False-safe,
false-blocked and false-deep hints are covered by tests. Original geography
is retried after a failed guided pass when another pass is available.
The clearance ladder explicitly tests the requested stand-off rather than
granting endpoint egress from an unnecessarily loose lower bound.
Standalone timings must not be described as complete live chart-provider timings.

## Headless and test infrastructure

The existing plugin JSON scenario loader, headless monitor and
`tools/headless-testing/run_headless.py` now accept `atlas`. The plugin builds
with the experimental source and uses the normal native adapter dispatch.
Existing tests exercise common chronology, weather/current/wave policy,
polars, chart caches/host services, coastal endpoint policy, resource limits,
concurrent candidates, preview and engine selection. The standalone Atlas
contracts add solver-specific analytic, event-wait, jet, reversal, narrow
portal, resource, mode, observer and concurrency regressions.

The six-engine benchmark is another standalone client of these interfaces.
It records separate setup, solving, audit and provider-query work; repeat
processes and retained-cache repeats are distinguished. It does not start an
interactive OpenCPN instance, modify a user's profile, install a plugin or
claim to qualify all live chart/depth-provider behaviour. Separate isolated
native replays cover actual GRIB, polar, chart and 5 m depth enforcement,
including a guidance A/B test and opt-in Auto/All dispatch.
