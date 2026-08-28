# Modern native routing engine

The plugin now uses a modern, deterministic C++20 routing engine for normal
GRIB and climatology routes. The existing OpenCPN UI, configuration files,
departure-time optimisation, multi-waypoint sequencing, chart-safety service,
route analysis, overlays, reports and export paths remain the integration
surface.

The legacy engine remains available for cumulative climatology distributions,
which have probabilistic semantics not represented by the deterministic
engine. It can also be selected for diagnosis or rollback by launching OpenCPN
with `WR_USE_LEGACY_ENGINE=1`.

## Solver pipeline

Each configured leg is solved independently and chronologically:

1. Validate environmental coverage and vessel performance.
2. Propagate adaptive forward isochrones using immutable value nodes.
3. Apply spatial/heading cells, deterministic Pareto dominance and bounded
   route-family preservation.
4. Retry with finer time, heading and frontier resolution when useful.
5. Attempt destination-side reverse-isocrone recovery when forward convergence
   stalls or a candidate fails replay.
6. Use a time-dependent, multi-label graph fallback when recovery fails.
7. Permit a bounded stationary wait only where no feasible movement exists,
   allowing tidal or forecast gates without turning the solver into an
   unbounded departure optimiser.
8. Independently replay the selected route forward at dense chronological
   intervals before it can be displayed or exported.

## Arrival-time routing

The Basic configuration page offers mutually exclusive **Route by departure
time** and **Route by arrival time** modes. Departure mode remains the default.
In arrival mode the selected date and time are the required destination
arrival, and the engine calculates both a departure and a route.

Arrival planning is destination-anchored without pretending that sailing
physics are reversible. It estimates an initial departure from direct distance,
projects observed ETA errors back onto the departure axis, brackets the
deadline where possible, and adaptively refines promising times. Every proposed
departure is nevertheless solved in normal chronological order by the complete
forward engine. A candidate can only be returned after the usual independent
forward replay and chart-safety validation.

The selected route is the latest forward-validated departure whose ETA is no
later than the planned arrival minus the configured arrival safety margin.
Routing effort controls the bounded number of full route evaluations. The
search-horizon setting limits how far before the planned arrival the planner
may look. When starting from the live boat position, it will not invent a
departure before the current time; saved positions and waypoints remain
available for retrospective analysis.

The existing **Optimise departure time** batch control is shown checked and
disabled in arrival mode because adaptive departure selection is intrinsic to
that mode. The fixed departure-optimisation batch is not run on top of it.
Routing status reports the selected departure, planned arrival, schedule
margin, and number of evaluated and feasible routes.

Forward search reserves part of the global generated-state budget for recovery,
so a difficult first stage cannot starve the graph fallback. The graph keeps
time, tack, propulsion mode, sail plan, motor duration, fuel and risk in its
labels. When currents are enabled, it deliberately uses Dijkstra ordering:
favourable current has no configured upper speed bound, so a polar-only A*
heuristic would not be admissible.

Generated-state accounting counts dynamically feasible propagated states,
not headings rejected immediately by the polar or hard wind-angle policy.
For passages up to 120 NM, each layer retains a sector-balanced maximum of 256
states while preserving tack, propulsion and directional families. The first
three coastal layers run at 30 minutes or finer, offshore propagation uses the
configured cadence, and destination convergence returns to the configured
minimum step. Exploratory motion is integrated on the 15-minute
forecast/cache cadence; independent acceptance replay remains dense at five
minutes or finer near chart hazards. Replay permits at most 0.15 NM of
numerical endpoint difference from the exploratory integration and
independently chart-checks that reconciliation segment before accepting it.

Recovery is deliberately lineage-safe. A forward or reverse completion which
fails independent replay is removed before graph seeding. Before graph
fallback, at most 16 of the best frontier prefixes are independently replayed
and no more than eight passing prefixes are admitted as seeds. The graph
rejects dominated labels before making semantic chart queries and never
expands a destination label whose complete lineage failed replay. A complete
graph route still receives an independent end-to-end replay before acceptance.

Graph fallback uses adaptive corridor widening. It first searches the focused
cross-track corridor used by the previous fast path, then admits deferred
labels in a wider intermediate stage, and finally removes that graph-only
corridor. The configured **Max Diverted Course** remains authoritative for
every admitted segment, so a value below 180 degrees still limits diversion;
at 180 degrees the final graph stage has no additional geometric corridor.
Deferred labels consume the same bounded graph-label budget but do not trigger
semantic chart queries until their stage is activated. This preserves the
cheap common case without excluding a safe route around a large obstruction.

## OpenCPN integration and safety

`OpenCpnAdapter.cpp` maps the established plugin configuration and boat polars
onto the native request. Weather is sampled from the existing GRIB/climatology
timeline through a bounded frame cache and a quantised sample cache. Polar
selection, motor threshold, day/night efficiency and tack, gybe and sail-plan
penalties retain their existing meanings.

Every candidate segment and every final replay segment uses the working
OpenCPN build's semantic chart-safety service when enabled. These calls retain
native S57/CM93 and licensed plugin-vector (including o-chart) object
semantics, drying/minimum-depth policy, exclusion boundaries and time-aware
cyclone-track checks; GSHHS is only the explicit stock fallback.
Isochrone contour drawing uses the cheaper shoreline test because a contour
edge is an inspection graphic, not vessel motion. This prevents visualization
from consuming the bounded semantic-query budget needed by the route itself.

The final route is still passed through the plugin's main-thread plotted-route
chart validation before display/export. This last gate requires fine
authoritative vector evidence and fails closed instead of accepting a GSHHS
fallback. Search acceptance never relies on an isochrone contour or inspection
trace.

## Runtime behaviour

The existing progress dialog reports forward propagation, reverse recovery,
time-dependent graph fallback and independent validation. Stop requests share
an atomic cancellation token with the engine and wake any pending GRIB request.

The route editor's **Advanced** tab exposes **Departure-time route workers**.
Each worker calculates one complete candidate route; `0` selects the bounded
automatic policy. The value is stored with the configuration and as the
default for newly created routes. The same tab exposes the plugin's persistent
**Chart-safety RAM cache** budget; `0` selects the hardware-derived automatic
budget.

The same tab exposes **Routing effort** levels of 100%, 150%, 200% and 400%.
The policy scales generated states, retained states and recovery graph labels
together, independently for every route and departure candidate. The standard
100% level preserves the previous final-route limits.

Chart-safety corridor scouting is bounded by deterministic generated- and
retained-state counts. A 120-second watchdog remains only as a fault guard: if
it fires, the partial scout is discarded and routing falls back to direct
corridor preparation. Consequently CPU scheduling or a warm cache cannot
change the partial scout geometry used by the final solve.

Weather samples are cached at 15-minute and 0.01-degree keys, which remain
finer than the 0.025-degree Irish Sea acceptance GRIB. Up to 512 copied GRIB
timeline frames retain a rolling 128-hour working set. This is a memory bound,
not a route-duration bound: later frames remain requestable, and an evicted
frame is fetched again if a search revisits it. The routing engine's separate
maximum duration remains 30 days. Beyond the valid GRIB range, weather either
comes from configured climatology/data-deficient policy or the route fails for
lack of weather; cache eviction does not select that policy. Exact
route-to-cursor lineages are capped at 48 evenly distributed traces per layer
to bound memory on long passages while keeping finer resolution than practical
cursor selection.

Successful and failed runs write one structured `WR_MODERN_NATIVE_SUMMARY` log
entry containing solver path, elapsed time, generated/retained states, graph
labels, waits, land checks and validation samples.

## Verification and benchmarking

Build and run the complete plugin suite:

```sh
cmake --build /path/to/OpenCPN/build --target weather_routing_pi_tests -j2
LD_LIBRARY_PATH=/path/to/OpenCPN/build/lib \
  /path/to/OpenCPN/build/plugins/weather_routing_pi/test/weather_routing_pi_tests
```

Run the repeatable synthetic Irish Sea benchmark:

```sh
tools/benchmark_modern_native_engine.sh /path/to/OpenCPN/build 5
```

The benchmark covers deterministic routing, independent replay and route
construction without conflating live GRIB messaging or chart-cache warm-up.
On the development machine used for this change, the synthetic Irish Sea
scenario takes about 2.2 seconds per solve in an optimised build. Treat that
number as a local baseline rather than a hardware-independent performance
claim.

For end-to-end GRIB and semantic chart testing, use the existing scenarios and
instructions in [headless_scenarios.md](headless_scenarios.md).
The chart-backed Holyhead--Dun Laoghaire acceptance run used the 26 July 2026
iGRIB forecast, the Nicholson 35 conservative polar and the S57/CM93 semantic
service. At a 27 July 10:00 UTC departure it completed with forward isochrones
in 29.15 seconds of engine time, producing a 59.48 NM route with 21 legs. The
hard Holyhead--Mouth of Lough Foyle case at the same departure completed by
reverse recovery in 149.0 seconds, producing a 163.32 NM route with 27 legs.
Both passed dense independent chronological replay and final plotted-route
semantic chart validation. These are reproducible development-machine
acceptance results, not promised runtimes on other hardware or forecasts.

The exact pre-change source is tagged
`rollback/pre-modern-native-engine-20260725` in the plugin repository.

## Source provenance

The standalone routing core began from the SuperCPN implementation at the
revision recorded in `third_party/supercpn-weather-routing/NOTICE.md`, then was
adapted for this native plugin: host-provided performance, bounded waiting,
recovery resource reservation, time-aware boundaries, OpenCPN integration and
bounded visualization storage. The retained Apache-2.0 notice and licence are
included there; the combined plugin remains distributed under GPLv3.
