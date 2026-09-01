# Weather Routing Headless Scenarios

This is a developer/test entry point for running Weather Routing scenarios
without driving the Weather Routing GUI. It currently uses the existing
`WeatherRouting` / `RouteMapOverlay` computation path; it does not introduce a
second routing algorithm.

## Build Prerequisites

Build OpenCPN and the Weather Routing plugin before running a scenario:

```sh
cd ~/src/OpenCPN/build
cmake --build . --target opencpn weather_routing_pi -j"$(nproc)"
```

Run from the OpenCPN source tree so relative plugin paths resolve as expected:

```sh
cd ~/src/OpenCPN
```

If another OpenCPN instance is running, headless runs may exit through the
single-instance IPC path. Stop the GUI instance before running scenario tests.

## Runtime Assumptions

The scenario runner uses the same Weather Routing plugin state and OpenCPN
services as the GUI path. It assumes:

- the Weather Routing plugin can be loaded from `OPENCPN_PLUGIN_DIRS`;
- suitable GRIB/weather data is already available in the OpenCPN profile;
- the requested time range is covered by the loaded GRIB data, or the scenario
  configuration enables and has access to a suitable climatology fallback;
- the selected/default polar and boat settings are usable;
- chart-backed safety tests require the configured OpenCPN chart database;
- OpenCPN's host seam extracts immutable raw chart-classification tiles, while
  xWeatherRouting owns their RAM/disk lifetime, margin/depth masks, cache
  invalidation and segment evaluation. Route workers therefore never call
  mutable chart objects;
- raw tile extraction intentionally remains a host responsibility so every
  chart format supported by OpenCPN, including formats a plugin cannot parse,
  has the same authoritative classification.

The scenario can name a boat/polar file through `route.boatFile`. GRIB data is
still supplied by the existing OpenCPN/Weather Routing configuration or the
headless GRIB override below.

For repeatable integration tests, `WR_HEADLESS_GRIB_FILE` can name an existing
GRIB file. The runner asks the GRIB plugin to open it through the plugin's
public message interface before routing starts. Weather Routing does not parse
the file or depend on GRIB internals. Without this variable, a fresh headless
process may report `no weather data at route time/window` until another
component has initialized the GRIB timeline.

## Environment Variables

`WR_HEADLESS_SCENARIO` points to a JSON scenario file.

`WR_HEADLESS_OUTPUT` optionally points to the JSON result file. The runner writes
an initial `running` result when the scenario starts and overwrites it with the
final result when the run completes or fails.

`WR_HEADLESS_GRIB_FILE` optionally names the GRIB file to open through the GRIB
plugin's existing JSON message interface. The file must exist and cover the
scenario's route area and the portion of the departure/arrival window expected
to use forecast data. Routes are not limited to the in-memory GRIB working-set
duration: old frames are evicted and reacquired as needed. Whether a route may
continue beyond the forecast's valid time range is controlled separately by
its climatology/data-deficient configuration. A scenario with climatology
disabled deliberately fails closed when its forecast ends.

`WR_HEADLESS_PLANNED_ARRIVAL_TIME` changes the selected scenario to
arrival-time routing and accepts an ISO timestamp (a trailing `Z` means UTC).
`WR_HEADLESS_ARRIVAL_HORIZON_MINUTES` controls how far before that deadline
departures may be considered, and
`WR_HEADLESS_ARRIVAL_SAFETY_MARGIN_MINUTES` sets the required early-arrival
buffer. `WR_HEADLESS_ARRIVAL_MAX_EVALUATIONS` can bound the number of full
probe routes in automated smoke tests. This path otherwise runs the same
reverse-guided, forward-validated planner as the GUI.

The existing `WR_HEADLESS_ROUTE_TEST` path remains supported. If
`WR_HEADLESS_SCENARIO` is set without `WR_HEADLESS_ROUTE_TEST`, the plugin still
starts the headless runner.

Example:

```sh
cd ~/src/OpenCPN

WR_HEADLESS_SCENARIO="$PWD/plugins/weather_routing_pi/testdata/scenarios/holyhead_dunlaoghaire.json" \
WR_HEADLESS_OUTPUT=/tmp/wr_result.json \
OPENCPN_PLUGIN_DIRS="$PWD/build/plugins/weather_routing_pi:/usr/lib/opencpn:/usr/lib64/opencpn" \
./build/opencpn

cat /tmp/wr_result.json

rg "WR_HEADLESS|FINAL_ROUTE_SAFETY|WeatherRouting propagation summary|WR_GRID_THREAD_VIOLATION" \
  ~/.opencpn/opencpn.log | tail -200
```

## Verifying Plugin Load

The log should show the Weather Routing plugin loaded from the build tree, for
example:

```text
PluginLoader: Loading PlugIn: ~/src/OpenCPN/build/plugins/weather_routing_pi/libweather_routing_pi.so
WR_HEADLESS_ROUTE_TEST timer_scheduled ...
WR_HEADLESS_SCENARIO loaded ...
```

If the log shows only the installed
`/usr/lib/opencpn/libweather_routing_pi.so`, the test is not using the
just-built integration candidate. Check `OPENCPN_PLUGIN_DIRS` and disable the
installed plugin for the test profile.

If the process exits immediately with an IPC warning, another OpenCPN instance
or stale IPC socket is probably blocking startup.

## Inspecting Logs

Useful log filters:

```sh
rg "WR_HEADLESS|FINAL_ROUTE_SAFETY|WeatherRouting propagation summary|WR_GRID_THREAD_VIOLATION" \
  ~/.opencpn/opencpn.log | tail -200

rg "WR_GRID|WR_ROUTE_MASK|WR_CERT_SAFE_CACHE|chart_api_calls_during_query" \
  ~/.opencpn/opencpn.log | tail -200
```

For chart-backed safety, worker segment queries should not call chart APIs.
Worker query log lines should show `chart_api_calls_during_query=0` when
`worker_thread_query_without_chart_api=1`.

## Scenario Schema

The first milestone supports:

- `schemaVersion`
- `name`
- `start.name`, `start.lat`, `start.lon`
- `end.name`, `end.lat`, `end.lon`
- `startTime`, ISO UTC
- `departureOptimization.enabled`
- `departureOptimization.beforeMinutes`
- `departureOptimization.afterMinutes`
- `departureOptimization.stepMinutes`
- `departureOptimization.concurrentRoutes` (`0` selects Auto; explicit values
  are limited to `1`–`12`)
- `environment.useCurrents`
- `route.routingEffortPercent` (`100`, `150`, `200` or `400`)

Routing effort scales the generated-state, retained-state and recovery-graph
budgets together for every departure candidate. It does not impose a
wall-clock timeout, and all candidates in one optimisation use the same
selected policy. The default is `100`; higher levels trade CPU time and memory
for a wider search on difficult routes.
- `environment.useGrib`
- `route.boatFile` (`~/` is expanded using the current user's home)
- `route.timeStepSeconds`
- `route.headingFromDegrees`, `route.headingToDegrees`,
  `route.headingStepDegrees`
- `route.maxDivertedCourseDegrees`, `route.maxCourseAngleDegrees`,
  `route.maxSearchAngleDegrees`
- `route.maxTrueWindKnots`, `route.maxApparentWindKnots`
- `route.optimizeTacking`, `route.upwindEfficiency`,
  `route.downwindEfficiency`, `route.nightEfficiency`
- `route.useMotor`, `route.motorSpeedThresholdKnots`,
  `route.motorSpeedKnots`
- `safety.mode`: `none`, `gshhs`, or `chart`
- `safety.enforce`
- `safety.landMarginNm`
- `safety.minimumDepthM`
- `safety.persistentCertifiedCacheEnabled`
- `reverseReachability.enabled`
- `reverseReachability.searchBackIsochrones`
- `reverseReachability.horizonHours`
- `reverseReachability.diagnostics`
- `stabilityCorridor.enabled`
- `stabilityCorridor.source` (currently `departureCandidates`)
- `stabilityCorridor.minimumRoutes`
- `stabilityCorridor.maxEtaPenaltyMinutes`
- `stabilityCorridor.gridResolutionNm`
- `stabilityCorridor.innerAgreementThreshold`
- `stabilityCorridor.outerAgreementThreshold`
- `stabilityCorridor.clusterRoutes`
- `stabilityCorridor.writeGeoJson`

`minimumDepthM` is expressed in metres. A positive finite value enables the
depth constraint in propagation, chart-safety prewarm and final plotted-route
validation. This requires `safety.mode` to be `chart` and a compatible OpenCPN
chart-safety service; the route fails closed if depth-aware safety is requested
but unavailable. Zero disables the depth constraint.

`departureOptimization.concurrentRoutes` uses the same scheduler policy as the
Advanced route editor. Auto reserves two logical CPUs for OpenCPN and the
operating system and uses at most four concurrent route candidates. The
global Weather Routing concurrent-thread setting remains a hard upper bound.

`reverseReachability` is optional and defaults off. This first milestone is a
bounded final-approach recovery/diagnostic pass, not arrival-time routing and
not a bidirectional replacement for the forward isochrone solver. When enabled,
the route engine keeps normal forward propagation, then after a failed direct
final approach it builds a destination-reachable funnel from recent forward
isochrones. Each reverse edge is accepted only if normal forward sailing
physics can sail that segment inside the available time interval. Any recovered
route still has to pass the normal final plotted-route safety validation.

Example block:

```json
"reverseReachability": {
  "enabled": true,
  "searchBackIsochrones": 6,
  "horizonHours": 0,
  "diagnostics": true
}
```

`stabilityCorridor` is optional and does not change route computation. It is a
post-processing diagnostic over completed departure candidates which pass
authoritative final-route validation. Routes are grouped into spatial families;
the representative line is a real medoid route, not an averaged route. When
`writeGeoJson` is true, the runner writes a sibling
`*.stability.geojson` file containing separate inner/outer raster-cell polygons
and the medoid line for each eligible family.

```json
"stabilityCorridor": {
  "enabled": true,
  "source": "departureCandidates",
  "minimumRoutes": 3,
  "maxEtaPenaltyMinutes": 120,
  "gridResolutionNm": 0.5,
  "innerAgreementThreshold": 0.7,
  "outerAgreementThreshold": 0.4,
  "clusterRoutes": true,
  "writeGeoJson": true
}
```

The agreement thresholds describe route agreement, not meteorological
probability. Cells which fail the active chart-safety check are omitted. The
corridor remains descriptive and is not used to accept or rank routes.

Example result section:

```json
"stabilityCorridor": {
  "status": "complete",
  "validRoutes": 9,
  "excludedRoutes": 4,
  "routeFamilies": 2,
  "selectedFamilyId": 0,
  "dominantFamilyRoutes": 7,
  "medianWidthNm": 2.4,
  "maximumWidthNm": 8.7,
  "etaSpreadMinutes": 43.0,
  "innerThreshold": 0.7,
  "outerThreshold": 0.4,
  "representativeCandidateId": "candidate-0",
  "geoJsonPath": "/tmp/wr_result.stability.geojson",
  "calculationTimeMs": 18
}
```

## Result Schema

The result file contains:

- `schemaVersion`
- `scenario`
- `status`
- `failureReason`
- `candidates[]`
  - `departure`
  - `state`
  - `eta`
  - `elapsed`
  - `distanceNm`
  - `finalSafety`
  - `failureReason`
  - `offsetMinutes`
  - `reverseRecoveryUsed`
  - `reverseRecoveryStatus`
  - `reverseLayersBuilt`
  - `reverseNodesGenerated`
  - `reverseNodesFeasible`
  - `reverseConnectionFound`
  - `reverseConnectionTime`
  - `reverseFailureReason`
  - `reverseFinalValidationPass`
- `diagnostics`
- `stabilityCorridor`
  - `status`
  - `validRoutes`, `excludedRoutes`
  - `routeFamilies`, `selectedFamilyId`, `dominantFamilyRoutes`
  - `medianWidthNm`, `maximumWidthNm`, `etaSpreadMinutes`
  - `innerThreshold`, `outerThreshold`
  - `representativeCandidateId`
  - `geoJsonPath`
  - `calculationTimeMs`
  - `failureReason`

Diagnostics are intentionally sparse in this first milestone. Fields are omitted
when the existing computation path does not expose them cleanly.

Example result:

```json
{
  "schemaVersion": 1,
  "scenario": "Holyhead to Dun Laoghaire",
  "status": "complete",
  "candidates": [
    {
      "departure": "2026-07-10T06:00:00Z",
      "state": "complete",
      "eta": "2026-07-10T13:51:39Z",
      "elapsed": 28299,
      "distanceNm": 54.52,
      "finalSafety": "pass",
      "offsetMinutes": 0,
      "reverseRecoveryUsed": false,
      "reverseConnectionFound": false,
      "reverseFinalValidationPass": false
    }
  ],
  "diagnostics": {}
}
```

Failure results still write JSON:

```json
{
  "schemaVersion": 1,
  "scenario": "Holyhead to Dun Laoghaire",
  "status": "failed",
  "failureReason": "no_completed_routes",
  "candidates": [
    {
      "departure": "2026-07-10T06:00:00Z",
      "state": "failed",
      "finalSafety": "fail",
      "failureReason": "No reachable route points",
      "offsetMinutes": 0
    }
  ],
  "diagnostics": {}
}
```

Use the reverse diagnostics example with:

```sh
cd ~/src/OpenCPN

WR_HEADLESS_SCENARIO="$PWD/plugins/weather_routing_pi/testdata/scenarios/holyhead_dunlaoghaire_reverse.json" \
WR_HEADLESS_OUTPUT=/tmp/wr_reverse_result.json \
OPENCPN_PLUGIN_DIRS="$PWD/build/plugins/weather_routing_pi:/usr/lib/opencpn:/usr/lib64/opencpn" \
./build/opencpn

cat /tmp/wr_reverse_result.json

rg "WR_REVERSE_REACHABILITY|FINAL_ROUTE_SAFETY|WR_HEADLESS" \
  ~/.opencpn/opencpn.log | tail -200
```

Use the stability-corridor example with:

```sh
cd ~/src/OpenCPN

WR_HEADLESS_SCENARIO="$PWD/plugins/weather_routing_pi/testdata/scenarios/holyhead_dunlaoghaire_stability.json" \
WR_HEADLESS_OUTPUT=/tmp/wr_stability_result.json \
WR_HEADLESS_GRIB_FILE=/path/to/environment.grb \
OPENCPN_PLUGIN_DIRS="$PWD/build/plugins/weather_routing_pi:/usr/lib/opencpn:/usr/lib64/opencpn" \
./build/opencpn

cat /tmp/wr_stability_result.json
python3 -m json.tool /tmp/wr_stability_result.stability.geojson >/dev/null

rg "WR_STABILITY_CORRIDOR|FINAL_ROUTE_SAFETY|WR_GRID_THREAD_VIOLATION" \
  ~/.opencpn/opencpn.log | tail -200
```

## Known Failure Modes

- `No reachable route points`: the existing routing engine failed to build a
  route under the active weather, polar, constraints, and safety settings.
- `Final route did not reach destination`: propagation produced geometry that
  could not be accepted as a completed destination route.
- `Chart safety grid data unavailable...`: chart-backed safety needed cells or
  masks that were not available after prewarm/retry.
- Immediate startup/exit with IPC warnings: another OpenCPN instance or stale
  IPC state is present.
- Empty or missing result file: the plugin did not load, the scenario did not
  parse, or OpenCPN exited before the headless timer fired.
- `WR_GRID_THREAD_VIOLATION`: unsafe chart API access from a worker thread; this
  is a correctness bug.

## Comparing Safety Modes

Edit the scenario `safety` block to compare modes:

```json
"safety": {
  "mode": "none",
  "enforce": false,
  "landMarginNm": 0.0,
  "minimumDepthM": 0,
  "persistentCertifiedCacheEnabled": false
}
```

```json
"safety": {
  "mode": "gshhs",
  "enforce": false,
  "landMarginNm": 0.0,
  "minimumDepthM": 0,
  "persistentCertifiedCacheEnabled": false
}
```

```json
"safety": {
  "mode": "chart",
  "enforce": true,
  "landMarginNm": 0.0,
  "minimumDepthM": 0,
  "persistentCertifiedCacheEnabled": true
}
```

Run the same scenario repeatedly with
`persistentCertifiedCacheEnabled` set to `false`, then `true`, then `true`
again. Compare log counters such as `fineTilesBuilt`,
`WR_CERT_SAFE_CACHE`, `persistent_cache_used_in_query`, and final validation
results.

Persistent certified safe-area cache entries are an optimisation only. Completed
routes still require final route safety validation.

## Exit Behaviour

On normal completion the process should write the result JSON and exit. In a
successful headless run the log normally ends with:

```text
WR_HEADLESS_ROUTE_TEST end ...
WR_HEADLESS_ROUTE_TEST process_exit code=0
```

If OpenCPN remains running after the headless result has been written, inspect
the shutdown log for plugin teardown or IPC messages. Do not treat a stale
process as a successful test until the result JSON and expected `WR_HEADLESS`
log lines have been inspected.

## Limitations

This runner is a first-class wrapper around the current headless test path, not
yet a standalone engine library. The next refactor stages should move request,
result, execution, and environment adapter boundaries out of `WeatherRouting`
incrementally while keeping the GUI and headless runner on the same route
algorithm.
