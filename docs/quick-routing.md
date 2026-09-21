# Quick, Standard and Professional — 1.18.0

Choose one engine on **Basic → Routing engine**, in this order:

| Engine | Implementation | When to try it |
|---|---|---|
| Quick | Hardened Original contour search | First attempt; low search overhead with independently validated final connections |
| Standard | Previously called Quick; bounded adaptive beam search | An alternative search strategy with bounded recovery |
| Professional | Previously called Main; multi-stage search | Broader search, reverse/frontier recovery and graph fallback |

There is no automatic switch to another engine. No engine guarantees the fastest
possible route or safe navigation independently of its data, polar and constraints.

## Installation, upgrades and saved settings

A first installation with **no saved last-used configuration** defaults to Quick.
Existing routes and last-used defaults retain their algorithm and numerical values:
old Main becomes Professional, and old Quick becomes Standard. Legacy XML without
an engine field remains Professional. New routes copy the selected route, or use
last-used defaults when no route is selected.

Serialized identifiers are intentionally unchanged: `original` means new Quick,
`quick` means Standard, and `main` means Professional. Unknown identifiers are
preserved and explicitly rejected, not silently mapped to an engine.

All three engines remember separate tuning, shoreline resolution and GRIB cache
limits. Changing engines does not copy or reset another engine's settings.
**Advanced → Engine settings** displays the selected engine's controls.
**Reset engine to preset** previews an explicit Apply/Cancel action and changes
only that engine's search tuning; safety, boat, weather and memory settings remain.

## First-use tuning and resources

Quick and Standard start at 180-minute nominal offshore steps, 10-degree heading
separation, a 120-degree maximum search angle and a 256 MiB search allowance.
Professional starts at one hour, 10 degrees and 100% effort. Balanced preset
revision 2 records these resolved values; upgrading never reapplies a preset.

Quick's step becomes smaller near its destination. Its maximum search angle is
relative to the previous sailing heading, as in the Original contour algorithm.
Standard additionally refines departure/coastal search and has bounded retries.
Professional's maximum search angle controls its destination-bearing search cone.

Quick converts its search allowance to a conservative retained-state ceiling;
it is not an exact allocator-level memory cap. Standard accounts tracked search
allocations against its memory ceiling. Both exclude weather, shoreline/chart
caches, output and other OpenCPN memory. Quick also limits generated states,
geometry operations, weather queries, replay candidates and wall time. Exhaustion
or cancellation returns a failure, never a completed partial route.

The preferred GRIB timeline cache is 2,048 MiB on 64-bit systems for all three
engines, reduced by physical-memory admission when needed. On 32-bit systems
Quick/Standard default to 64 MiB and Professional to 192 MiB. Cache preferences
and preset resets are independent.

All engines initially select Intermediate shoreline detail. Crude, Low and
Intermediate are bundled; install High or Full through **Shoreline data**.
Enforced chart safety has its own scout-shoreline selection and requires a
compatible enhanced host. GSHHG shorelines do not provide water depths.

## Quick acceptance and limitations

Quick retains fast contour expansion, then repairs candidate paths into timed
sailing legs and replays them with the shared independent chronological validator.
Enclosing the destination alone is not completion. Final tacks and gybes are
explicit legs; Quick does not use the implicit Optimize Tacking polar shortcut.

Every GSHHG segment and the complete configured safety buffer are checked,
including small islands between the old offset lines. Quick does not silently
waive a shoreline margin at departure or arrival: move an endpoint into genuinely
clear water or deliberately review the margin if it rejects an endpoint.

With currents enabled, missing current data cause failure. An active Max Swell
limit requires wave data when using GRIB weather; missing waves cause failure.
Supply the data or deliberately disable the unwanted constraint (Max Swell zero
disables that limit). “Last Valid if Data Deficient” is not a missing-current or
missing-wave waiver. Selected climatology can extend wind coverage only when
the provider actually supplies data; the result identifies climatology use.

Quick supplies bounded diagnostic isochrones and route-to-cursor traces. These
are approximate search previews, not validated navigable routes. Only a completed
final route has passed acceptance. Quick has no Professional graph/reverse search
or stationary-waiting strategy, and can miss feasible or faster routes.

Quick and Standard reject existing-route analysis, cumulative climatology modes
and forced legacy routing; use Professional for those configurations.
Departure optimisation and planned-arrival routing use the selected engine, with
bounded evaluation counts. Shared scheduling still limits concurrent workers.

## Headless scenarios

Use stable IDs, not display titles:

```json
"route": {
  "routingEngine": "original",
  "quickMemoryBudgetMiB": 256,
  "quickOffshoreStepMinutes": 180,
  "quickHeadingStepDegrees": 10,
  "quickMaximumSearchAngle": 120,
  "maxSwellMeters": 0
}
```

The historical `quick*` scenario tuning keys configure the selected fast engine;
saved XML uses distinct `Original*` and `Quick*` blocks. The earlier `quickRoute`
boolean continues to select Standard, and an explicit `routingEngine` wins.
Results record the stable engine ID and the settings used for that calculation.

For isolated host tests, set both a private `--configdir` and
`WR_HEADLESS_DATA_DIR`; some host plugin-data paths do not follow the profile.

The GUI-independent Quick component and its contract tests are documented in
[vendor/original-routing-engine/README.md](../vendor/original-routing-engine/README.md).
