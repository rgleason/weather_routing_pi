# Quick candidate-repair regression — 24 September 2026

## Finding and minimal fix

Quick can exhaust its candidate-validation allowance close to the destination
because reconstruction of an **earlier** leg repeatedly fails. The displayed
remaining distance therefore does not identify the failing leg.

The explicit tack/gybe connector previously tried only 1, 5 and 15 degrees
inside the configured true-wind-angle limits. Factory limits are 40–160
degrees, but the bundled `Test-TWS-0-20+60.pol` supports only 50–150 degrees.
For downwind repair this left only the 145-degree trial usable. Wind changes
during a leg can invalidate that trial, despite another explicit gybe being
feasible. Replaying many related candidates then repeats the same failure.

`vendor/original-routing-engine/src/Engine.cpp` now additionally tries 25, 35
and 45 degrees inside the limits. Existing trials retain their order; extra
work occurs only when they fail. This remains a small bounded search, not an
exhaustive heading scan. It does not guarantee every feasible connection will
be found.

No candidate allowance, wind-angle constraint, polar validity rule, integration
tolerance, land-margin check or chronological validation rule is relaxed.
Accepted gybes remain two explicit sailed legs, not an optimized-speed straight
chord. No changes are made to Standard or Professional.

## Reproduction and results

This is a reproduction with our Pacific GRIB and factory polar, **not an exact
replay of Quinton's unavailable GRIB/configuration**.

- Start: 36.930149, -122.017321; destination: 21.500583, -157.753438.
- Departure: 2026-09-20 08:00 UTC; three-hour contour steps; 10-degree heading
  increments; 120-degree search angle; 40–160-degree wind-angle constraints.
- GRIB: `environment_ecmwf_ifs_copernicus_global_20260920_1635.grb` from the
  local generated-GRIB folder.
- Factory polar; currents and wave limit disabled; no shoreline avoidance in
  this open-ocean reproduction. Separate shoreline contracts were tested.
- Host test: 256 MiB Quick budget and 2048 MiB GRIB timeline cache.

| Test | Before | After |
| --- | --- | --- |
| Standalone engine, real Pacific GRIB | Candidate allowance reached; closest approach 0.921 NM; 2.46 s | Exact endpoint, chronological validation passed; 1.90 s |
| Actual OpenCPN host, private profile/display, real xGRIB provider | Candidate allowance reached; closest approach 0.931 NM; 12.33 s | Exact endpoint, chronological validation passed; 84.63 s |

The host run delivered 162 legs, 7,686 validation samples and an ETA of
2026-10-03 09:34:20 UTC. Endpoint longitude 202.246562 in its JSON is equivalent
to -157.753438. The test used the actual built OpenCPN and plugin adapters under
Xvfb, not a manual GUI usability test. The user's running OpenCPN was untouched.

**Do not describe the host result as a two-second route.** Its weather-provider
diagnostics record 79.68 s inside cache-miss weather lookups, including GRIB
broker waits. The run requested 1,255 distinct timeline keys and published
1,801 complete frames, including 546 reloads after eviction. Precise candidate
reconstruction/replay requests many more times than coarse contour expansion.
This identifies a substantial remaining host data-path cost; these measurements
do not establish the cost of each individual component or an apples-to-apples
performance comparison with catalogue Original. The minimal connector fix
addresses completion, not this broader performance bottleneck.

## Regression assurance and limitations

`OriginalEngine.DefaultPolarChangingWindReachesActualDownwindDestination` uses
the real factory polar, three smooth changing-wind cases, actual endpoint
checks and an additional minute-by-minute screen of exported chord wind angles.
It fails against the old three-angle connector with the reported candidate
allowance error, and passes with the extra trials. Earlier constant-speed boat
fixtures did not exercise the factory polar's narrower angle envelope.

- Plugin CTest: **312/312 passed**, 18.56 s locally.
- Existing standalone Original contracts passed, including full-resolution
  GSHHG, required missing environmental data, cancellation and resource limits.
- Four additional short default-polar cases completed in three repeated runs;
  median standalone solve times were approximately 0.046–0.068 s. Three of
  those cases exhausted the allowance before this change.

The standalone benchmark's coarse, cross-engine speed screen flagged two of
1,960 samples (maximum ratio 1.081). That screen compares a whole-leg mean
speed with local instantaneous polar speed, so it is not an independent
time-integrated validation of changing weather. It is retained as a screening
caveat, not silently counted as a clean audit. The engine's chronological
validator passed. Neither that validator nor these tests proves continuous
navigational safety or global route optimality.

Local host fixtures and before/after logs are retained under
`artifacts/quick-repair-20260924/` in the enclosing OpenCPN workspace. This
patch does not bump a version, publish a release or replace the working plugin.
