# xWeatherRouting 1.22 comfort comparison prototype

This prototype retains completed, validated routes and compares their passage time and whole-route discomfort. It does not yet change the engines to search for comfortable alternatives. Its branch is `prototype/xweather-1.22-comfort`, based on released 1.21 commit `28438ceb86b479ca3c0ab1398695ab07b75c562e`.

## Using the prototype

Open the **xWeatherRouting 1.22 Prototype** desktop launcher. This starts a private copy of the API 1.23 chart-safety test OpenCPN, with its own configuration, weather-routing data and plugin library. The working wrapper `/home/paul/bin/opencpn-gribmerge`, its desktop entry, and its 1.21 plugin are not modified.

1. Load a GRIB, choose a voyage and compute **All (slow)**. Alternatively compute departure candidates, then select a completed member of that departure family.
2. Right-click the routing results table and select **Compare Fastest / Comfort...**.
3. Select any row to put that retained validated route on the chart and update the usual routing table. Sort by passage time, discomfort exposure, average discomfort, worst known leg or another column.
4. Move the slider to select the lowest exposure within the displayed extra-time allowance. The left endpoint selects the fastest; the right selects the minimum exposure among comparable retained results. Intermediate positions allow a fraction of the time difference between those endpoints. Because the routes are discrete, the chart changes in steps.

Auto retains its stop-at-first-success behaviour. All can retain at most four engine results for a fixed departure: Quick, Standard, Alternative and Professional. A single suitable result, or a fastest result which is also the least exposed, displays that no trade-off is available. Sorting or slider movement never invokes an engine.

## Metrics and worst leg

The first model uses the same empirical wind/angle/wave severity and Good / Bumpy / Difficult thresholds as the 1.21 route table. It is a routing comparison measure, not a calibrated measure of vessel motion or a new safety threshold. The inherited signed-angle behaviour is preserved; calibrating that model can be a separate development pass.

For each delivered leg, exposure is its condition severity multiplied by its duration in hours. Whole-route exposure is the sum of these values, and average discomfort is exposure divided by passage duration in hours. This makes the result independent of whether an otherwise identical interval is split into ten-minute or longer legs. Lower exposure is better. Continuous severity distinguishes routes within a displayed category.

Every retained candidate also records:

- Time in Good, Bumpy, Difficult and unknown conditions.
- Wave-data coverage, missing-wave duration and the longest continuous Difficult spell.
- The **worst known leg**, independently of the total: category, numerical severity, leg index, start/end coordinates and start/end time. Its duration is retained too. This permits later weighting of the peak, or a separate worst-leg constraint, without recalculating the search.
- Delivered geometry, elapsed time, engine provenance, shoreline provenance and validation status.

Worst-leg severity is available for sorting and inspection now; no additional peak penalty is imposed in this pass. Search frontier/isochrone geometry is not retained for each alternative, keeping the cache compact.

Missing wave or wind information is **unknown**, not calm. A partially covered route remains selectable as a validated fastest result, but cannot win the full-condition comfort ranking. Wind + wave ranking requires wind and wave height at every delivered route-leg sample (100% coverage). Coastal wave-grid gaps can prevent this even when the GRIB contains wave fields; coastal routing is not inherently excluded. Coverage and missing-wave duration are weighted by each leg's duration, rather than a continuous measurement of every point along the passage.

The explicit **Rank by wind comfort only** checkbox excludes waves from exposure, average discomfort and difficult-duration ranking metrics. The separate **Worst known (wind + waves)** column, its sorting and the detailed **Worst known leg, including available waves** summary always use the full-condition metric, irrespective of ranking mode. If no leg has both wind and wave-height data, that metric remains unknown. The wave-coverage percentage and missing-wave duration also remain visible in both modes. Missing conditions never become calm conditions or an estimated wave value.

Existing hard weather and land limits continue to apply before retention. Changing comparison mode never relaxes validation. With incomplete coverage, the dialog explains the full-condition requirement and offers wind-only ranking; unknown difficult exposure is labelled incomplete. Departure, ETA and worst-leg timestamps in this comparison are explicitly formatted as UTC, including when the desktop uses BST.

## Scope and limitations

Only completed routes accepted by engine validation and the existing host checks are included. Departure-family comparison uses passage duration, not earliest calendar arrival: departures may differ, and both departure and ETA are shown in UTC. Existing completed multi-leg departure itineraries are compared as complete timed itineraries using their selected leg results; arbitrary mixtures of engine legs are not constructed. Unmodelled waiting intervals are unknown.

Arrival-planning internal probes are not retained as interchangeable alternatives; only the delivered winner is retained for that mode. Candidates remain in memory for the current calculation and are not restored after restarting OpenCPN. Recalculation, removal or clearing invalidates an open comparison; the user is prompted to reopen it after computation.

This pass finds the least exposed route **among the retained results**. The later phase must preserve faster and gentler partial paths deliberately to discover routes that the current engines discard. It will require separate bounded-search and performance work.

## Validation and measurements

The local build includes automated tests for time-weighted exposure, worst-leg retention, contiguous difficult spells, unknown coverage, slider endpoints/intermediate selections, dominance, ties, and validated-result retention with Auto stopping behaviour. The full existing suite passes 352 tests.

An isolated real OpenCPN harness compares 1.21 and 1.22 with the same compiler, build mode, boat, GRIB and route settings. It exercises Auto, All and a three-departure family, plus a wave-enabled GRIB and a failed departure outside forecast coverage. Matching runs must preserve the chosen engine and fastest passage time. The new headless result JSON includes retained geometry, comfort metrics, coverage and worst-leg details.

GUI handler checks exercise row selection, wind-only toggling, slider positions and sorting; the selected result's plot, ETA and identity must agree. A separate disposable fixture deletes a route with its comparison still open to check safe invalidation. Logs must retain exactly the original four engine attempts despite all the UI selections.

Detailed local results, build logs and test harnesses are in `/home/paul/src/OpenCPN/output/xweather-1.22-prototype`. Small fixtures can measure retention/scoring and chart-switch costs, but do not establish overhead for long passages or chart-aware coastal searches. Compare Holyhead–Mouth of Foyle against 1.21 with identical forecasts/settings before drawing those conclusions.
