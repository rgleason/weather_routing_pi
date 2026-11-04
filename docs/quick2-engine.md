# Quick2 implementation and qualification scope

Quick2 is independently expressed GPL code developed from the local algorithm
investigation. No FairlySoon source, shaders or executables are included. Its
implementation is `vendor/quick2-routing-engine/src/Engine.cpp`; the host adapter
is `src/engine/native/OpenCpnAdapter.cpp`.

## Search and safety

A serial CPU beam expands up to 128 frontier labels using 96 angular proposals
plus destination and incoming-heading hints. Each parent's sampled weather and
polar candidates cheaply predict displacement. Ranking uses accumulated time
(or elapsed time plus shared discomfort) and a remaining-distance hint. The hint
is not an admissible lower bound. Angular diversity and local spatial competition
bound the expensive exact integrations. Local Mercator cells distinguish tack,
propulsion, sail plan and departure-egress state. There is no global visited mask
that can paint away later arrivals or alternative approaches.

Every admitted move uses the shared `checkedMotion` kernel with five-minute
integration slices. The original land/boundary provider is called by that kernel;
the host provides time-dependent chart/depth enforcement when enabled. Departure
and arrival access retain the shared bounded endpoint policy. Near a destination,
several frontier parents may try an exact connection. The engine reconstructs
parent choices, replays them, prepares authoritative geometry and invokes the
independent chronological validator. The host then checks the delivered plotting
chords. A stricter replay rejection cannot become an accepted route.

Defaults bound proposals to two million, lineage to fifty thousand labels and
history to the request's retained-state allowance. All exits report spent
proposals, including failures and cancellation, so extra-search accounting cannot
undercharge failed attempts. Preview publication uses the shared bounded,
exception-isolated observer; it makes no weather/land queries and does not affect
selection. Endpoints outside 85 degrees latitude and unsupported ensemble/fuel
objectives are explicitly rejected.

```text
frontier = [departure]; history = [root]
repeat while frontier and work allowance remain:
    attempt exact goal connections from near-goal parents
    reconstruct and independently validate any completed candidate
    return the first accepted candidate
    cheaply predict headings from each frontier parent's weather/performance
    rank proposals with elapsed/objective cost and remaining-distance hint
    exact-integrate a bounded angular/spatially diverse selection
    retain local state alternatives in the next frontier
return an explicit incomplete/resource/weather/validation outcome
```

This is approximate candidate generation, not a guarantee of shortest distance,
earliest arrival, globally best comfort or complete channel exploration. Waiting
is attempted only after no moving survivors; this is not a comprehensive tidal
schedule optimiser. A finer grid or larger beam can increase cost without fixing
a missed route. Coarse water-distance guidance and retaining multiple completed
goals are future quality experiments; they have not replaced the current chart
approach.

## Engine orchestration

The Auto controller stores Quick2 results outside all subsequent solvers. Each
attempt gets its own configuration, request, weather, land and vessel adapter.
Standard receives no incumbent, corridor or visited-state information from
Quick2. Standard's configured work allowance is not reduced. Its existing
arrival-planning accounting is separate from Quick2's new arrival allowance.
A successful later engine ends Auto with the earliest retained accepted arrival;
a Quick2 success alone never skips a selected Standard attempt. All uses the
same serial order and attempts every enabled engine.

Tests compare Standard alone and after Quick2, including exact delivered legs,
timestamps and generated/query counts. Headless host appraisals separately
compare the retained Standard geometry, elapsed time and comfort metrics.

## Comfort and further model work

Quick2 directed search uses elapsed time plus the existing shared severity
integral; candidate acceptance and comparison rescore delivered legs. The
Fastest / Comfort selector keeps its current mapping and saved preferences.
The full FairlySoon model has not been silently adopted or declared inferior:
wave direction/period, gusts and convective fields need explicit availability,
provider support and vessel-specific appraisal before a new default is justified.

The investigation separately compared policy sensitivities and observed ranking
disagreements. A future versioned evaluator should be shared by candidate search
and comparison across engines, assessed first on the same candidate set and then
through independently policy-directed searches. Preserve candidates useful under
either model during that appraisal. Missing fields must remain unknown, rather
than become calm conditions. Explicit extra-passage-time selection is a separate
possible improvement; v1.32 preserves the current slider semantics.
