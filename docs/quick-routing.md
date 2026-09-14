# Main and Quick engines in 1.17.7

The plugin contains two native C++20 search engines. Select **Main** or **Quick**
in **Basic → Routing engine**. Main is the default and retains its existing
multi-stage search and recovery algorithms. Quick is a separate, bounded beam
search. Each calculation uses one engine; there is no automatic fallback.

## Settings and upgrades

An upgrade preserves existing routes and last-used configuration defaults.
Configurations from 1.17.6 have no engine field and load as Main with their
existing tuning. Saved Main and Quick settings remain independent when switching.
New routes follow the existing workflow: copy the selected route, or use saved
last-used defaults when no route is selected. With no saved defaults, Main starts
with its established factory search settings.

**Advanced → Engine settings** shows the selected engine's tuning. Main has its
own time step, heading separation, search angle, effort and optional reverse
reachability setting. Quick has its own nominal offshore step (10–360 minutes),
heading separation (5–30 degrees), maximum search angle and memory budget.
Quick continues to refine sampling automatically near departure and destination.

The preset chooser is deliberately passive. **Reset engine to preset** opens an
Apply/Cancel preview; only **Apply preset** changes the active engine's search
settings. The existing editor otherwise saves changes immediately; its OK button
closes it. Resetting a preset preserves memory budgets, shared safety limits,
boat/polar settings and weather permissions. Manual search edits are labelled
Custom. Changing only the memory budget does not change the search preset.

1.17.7 initially exposes **Balanced** and the **Custom** status. Balanced uses
Main's established defaults (1 hour, 5 degrees, 100% effort, 120-degree maximum
search angle, optional reverse recovery off). Quick Balanced uses the tested
adaptive policy (nominal 3 hours, 20 degrees, 120-degree maximum search angle).
Additional effort presets require calibration before being offered.

Saved preset IDs and revisions describe stored values; loading or upgrading
never reapplies a preset. An unknown saved engine is preserved and rejected at
calculation time until the user explicitly selects a supported engine.

Quick's default **256 MiB** memory budget is configurable from 1–4096 MiB
(4095 maximum in the 32-bit dialog). This is a **per-route ceiling on tracked
search storage**, not a reservation or a limit on total OpenCPN memory. Raising
it does not automatically widen the search. Weather, chart and shoreline data,
physics-provider allocations, route output and OpenCPN require additional memory.

Headless scenarios support explicit engine and independent Quick tuning:

```json
"route": {
  "routingEngine": "quick",
  "quickMemoryBudgetMiB": 256,
  "quickOffshoreStepMinutes": 180,
  "quickHeadingStepDegrees": 20,
  "quickMaximumSearchAngle": 120
}
```

The earlier `quickRoute` boolean remains readable; `routingEngine` takes
precedence. Headless candidate results record the computed engine and search
settings, including preset/revision. Native logs identify Quick's actual solver
as `quick_beam`. `WR_QUICK_SUMMARY` records budget, peak tracked search storage,
weather calls, attempted motions, attempts, states and closest approach.
Completed routes identify their engine; editing settings marks them for
recomputation while preserving the previous calculation's search provenance.

For isolated integration tests, `WR_HEADLESS_DATA_DIR` supplies an absolute private
plugin-data directory. Set this as well as the host's private profile: some hosts'
plugin API data path does not follow `--configdir`.

## Search and memory design

- A separate `QuickRoutingEngine` implementation and CMake library target own the
  Quick policy. `MotionKernel.h` bridges to the physical operations already used
  by Main: propagation, manoeuvre accounting, propulsion, waiting, coast/boundary
  checks and destination connection. Main's search algorithms are unchanged.
- The first attempt retains at most 96 frontier states; a second, bounded retry
  may retain 192. Candidates are ranked by objective cost plus an approximate
  remaining-time estimate. Spatial/heading thinning and approach sectors preserve
  some alternatives. This pruning is heuristic, not a proof of dominance.
- The nominal offshore step is 3 hours. Regional routes use at most 1 hour, initial
  layers 30 minutes, and close approach or active coastal egress 10 minutes.
  Heading sampling is normally 20°, 10° on retry and 5° during egress, supplemented
  by destination and incoming headings and small approach offsets. Physical
  integration uses the shared kernel; an offshore search step is not an unchecked
  straight jump across land.
- The default total allowance is 600,000 generated states (also bounded by the
  caller's request), 120 million counted weather calls, and four attempted motions
  per allowed generated state. The first attempt receives two thirds of the state
  allowance; the retry uses what remains. At most three complete candidate routes
  are submitted for final validation. These are library policy options, separate
  from the user-facing memory ceiling.
- Retained labels are compact, trivially stored physical states with parent
  indices and interned profile identifiers, rather than full `RouteLeg` objects.
  Reference counts reclaim ancestors when no retained descendant needs them.
  Search containers use a tracked `std::pmr::memory_resource`; allocation is
  refused before exceeding its limit. Sorting does not allocate a separate heap
  buffer. The pool reuses freed slots.
- Only a selected lineage is reconstructed into full legs. Each stored action is
  rerun through the physical kernel, checked against its recorded state, then the
  completed candidate goes through the same independent chronological validator
  and authoritative validation-provider hook as Main. A failed reconstruction or
  validation cannot be presented as a completed route.
- Quick's OpenCPN adapter limits its weather sample cache to 32,768 entries and
  its interpolated GRIB-frame cache to 64 MiB, versus Main's existing 500,000 sample
  entries and 192 MiB/512 MiB frame ceilings on 32/64-bit builds. The frame cache
  retains one oversized frame when necessary to deliver a requested reply; this
  is not a strict process-memory guarantee. Switching back restores Main's cap.
- Quick skips Main's chart-corridor scout precomputation. It still prepares and
  checks its selected candidate using the authoritative validation provider.
- Arrival-time planning uses Quick forward solves, at most six evaluations and
  1.8 million generated states across those evaluations. The planner retains only
  the best feasible full route plus candidate summaries. This retention option is
  off for Main.

## Limits and expectations

Quick can miss a feasible passage, a useful departure window or a faster route
that Main finds. Its common 6-knot remaining-time estimate and early pruning are
particularly approximate around complex obstacles and weather-dependent detours.
It does not implement Main's reverse/frontier/graph recovery, nor the time-bucketed
multi-rate scheduler contemplated in the earlier design appraisal. It uses a
synchronous frontier with adaptive steps. The first independently validated
candidate wins; it does not prove global optimality.

The initial comparisons include examples where Quick's ETA is about 8.5% later
than Main's, alongside examples where it is earlier. Do not describe it as always
more accurate, or as established to outperform the stock plugin: that requires a
separate stock-plugin comparison. Completion is acceptance against the configured
model, data and constraints, not a guarantee about conditions at sea.

Existing-route analysis, cumulative-climatology routing and a forced legacy
engine setting are rejected when Quick is selected; select Main for those
configurations. Memory exhaustion, work exhaustion and cancellation fail cleanly
without silently launching a different engine.

## Embedding in OpenCPN or another C++ application

The standalone library has no GUI, GRIB-decoder or OpenCPN dependency. Supply the
same provider interfaces as Main and link the separate Quick target:

```cmake
find_package(weather_routing_engine CONFIG REQUIRED)
target_link_libraries(my_router PRIVATE
  weather_routing_engine::quick_weather_routing_engine)
```

```cpp
#include <supercpn/weather_routing/QuickEngine.h>
namespace wr = supercpn::weather_routing;
wr::QuickRoutingOptions options;
options.memoryBudgetMiB = 256;
wr::QuickRoutingResult result =
    wr::QuickRoutingEngine{}.route(request, providers, options);
```

Both engines are included in the plugin's existing C++ build. The Quick target
links to the shared library containing the physical kernel. Public options and
results are in `QuickEngine.h`; policy changes belong in `QuickEngine.cpp`.

## Reproducible verification

From the plugin directory:

```sh
cmake -S vendor/weather-routing-engine -B /tmp/wr-engines -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/wr-engines -j2
ctest --test-dir /tmp/wr-engines --output-on-failure
/tmp/wr-engines/tests/weather_routing_head_to_head main ocean
/tmp/wr-engines/tests/weather_routing_head_to_head quick ocean 256

cmake -S . -B /tmp/wr-plugin -DOCPN_BUILD_TEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/wr-plugin -j2
ctest --test-dir /tmp/wr-plugin --output-on-failure
```

The standalone suite includes deterministic coastal/ocean routes, changing wind,
tidal current, islands, margins, dateline/high-latitude navigation, depth,
climatology and prediction-current fallback, motoring, missing/constrained weather,
invalid inputs, cancellation and limited work/memory. Plugin tests additionally
exercise authoritative chart rejection, coastal egress, cancellation during work,
tracked allocation exhaustion, JSON configuration and cache-budget changes.

The workspace evidence and head-to-head report are in
`artifacts/quick-engine-implementation/`. Those measurements distinguish tracked
search bytes, whole-process RSS, virtual address space and route quality. Linux
32-bit checks are not Windows release qualification; Windows and GUI release
packaging remain separate checks.

## Shoreline detail

Advanced offers 0 — Crude through 4 — Full, all bundled offline. Main inherits
existing shoreline preferences (Full for a fresh install); Quick starts at Crude.
Each remembers its own manual choice. On an enhanced core with chart safety enabled
and enforced, the editable Scout shoreline resolution uses a separate saved choice,
initially Crude. Increasing scout detail does not change the authoritative chart
or depth checks. Preset resets preserve all shoreline choices. The headless route
keys are `shorelineResolution` for the selected engine and
`chartShorelineResolution` for enforced chart-mode scouting (integers 0–4).

Lower resolutions omit small coastal features and may permit land crossings
visible at higher resolutions. The tile-cache limit is per loaded dataset, separate
from Quick's search-memory budget; neither limits total OpenCPN memory.
