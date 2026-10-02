# xWeatherRouting 1.23 comfort exploration prototype

This prototype retains completed, validated routes and compares their passage time and whole-route discomfort. All (slow) now also runs a separate bounded search for faster and gentler alternatives, using the same physical motion and independent validation as Standard. Its branch is `prototype/xweather-1.22-comfort`, based on released 1.21 commit `28438ceb86b479ca3c0ab1398695ab07b75c562e`.

## Using the prototype

Open the **xWeatherRouting 1.23 Prototype** desktop launcher. This starts a private copy of the API 1.23 chart-safety test OpenCPN, with its own configuration, weather-routing data and plugin library. The private launcher remains available separately. For this development pass, the working wrapper `/home/paul/bin/opencpn-gribmerge` also points to a separate versioned 1.23 installation at the user's request; its desktop entry and other plugin prefixes are unchanged. The original 1.21 plugin is retained for rollback. The existing internal branch and runtime directory names are retained for compatibility with the parallel engine work.

1. Load a GRIB, choose a voyage and compute **All (slow)**. Alternatively compute departure candidates, then select a completed member of that departure family.
2. Right-click the routing results table and select **Compare Fastest / Comfort...**.
3. Select any row to put that retained validated route on the chart and update the usual routing table. Sort by passage time, discomfort exposure, average discomfort, worst known leg or another column.
4. Move the slider to select the lowest exposure within the displayed extra-time allowance. The left endpoint selects the fastest; the right selects the minimum exposure among comparable retained results. Intermediate positions allow a fraction of the time difference between those endpoints. Because the routes are discrete, the chart changes in steps.

Auto retains its stop-at-first-success behaviour. All retains the successful results from Quick, Standard, Alternative and Professional, plus at most four additional validated comfort alternatives for a fixed departure when exploration is enabled. A single suitable result, or a fastest result which is also the least exposed, displays that no trade-off is available. Sorting or slider movement never invokes an engine.

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

The explicit **Rank by wind comfort only** checkbox excludes waves from exposure, average discomfort and difficult-duration ranking metrics. It defaults to ticked when no preference has been saved, and saves changes immediately in the plugin configuration so the choice survives reopening the dialog and restarting OpenCPN. It does not generate or download wave data. The separate **Worst known (wind + waves)** column, its sorting and the detailed **Worst known leg, including available waves** summary always use the full-condition metric, irrespective of ranking mode. If no leg has both wind and wave-height data, that metric remains unknown. The wave-coverage percentage and missing-wave duration also remain visible in both modes. Missing conditions never become calm conditions or an estimated wave value.

Existing hard weather and land limits continue to apply before retention. Changing comparison mode never relaxes validation. With incomplete coverage, the dialog explains the full-condition requirement and offers wind-only ranking; unknown difficult exposure is labelled incomplete. Departure, ETA and worst-leg timestamps in this comparison are explicitly formatted as UTC, including when the desktop uses BST.

## Bounded comfort exploration

**Search additional comfort alternatives on next All run (slower)** in the comparison dialog defaults to enabled and saves its setting immediately. The dialog can be opened before computation to change this setting. Disabling it and recomputing restores the original four-engine comparison for measuring overhead. It does not start a calculation when toggled.

For each departure in **All (slow)**, the usual engines run first and preserve their fastest validated result. An extra Standard-based beam search then retains fast, gentle and balanced partial paths in separate selection lanes. It accumulates the same duration-weighted empirical discomfort used by the comparison table and maintains a bounded completed time/exposure Pareto set. This actively explores paths the ordinary fastest search may discard; it is not simply reranking the original four results. The Professional algorithm is not modified.

The initial bounds per departure are:

- At most four extra candidates, deduplicated against the original results by delivered route fingerprint.
- At most 50% longer than the original fastest passage, with an absolute allowance of 12 extra hours; the existing maximum passage duration still applies.
- 50,000 generated states, 2 million counted weather calls, at most 24 candidate validations, and a search-memory budget capped at 64 MiB or the smaller saved Standard budget.
- A cooperative 20-second search allowance. Provider/service calls and independent or host validation cannot be forcibly interrupted at that deadline, so total added wall time can exceed it.

The search uses the **Rank by wind comfort only** preference snapshotted when each computation starts. Wind-only search ignores waves in its objective but preserves available waves on the delivered legs and in the separate worst-leg metric. Full-condition search cannot score motions without wave height; it never treats missing waves as calm. Existing wind, wave, propulsion, depth, shoreline, boundary and chart constraints still apply in either mode. Every retained extra passes independent route replay and the existing delivered-chord host checks.

With departure optimisation and All selected, the extra pass runs independently for every successfully completed departure candidate. It runs in the existing worker, without extra parallel workers. Auto and individually selected engines retain their present behaviour. Internal arrival-deadline probes, scouts and failed departures do not launch extra searches. Cancellation discards the whole operation; exhaustion or failure of the extra search preserves the original validated result and any accepted extras. The comfort bounds never reduce the ordinary engines' existing budgets. A regression host fixture forces the comfort allowance to one weather call and still returns the ordinary fastest route and all four original candidates. All still initially displays the fastest result; the slider and row selection switch between cached alternatives without searching again.

The extra candidates are labelled **Standard (comfort: wind)** or **Standard (comfort: wind + waves)**. Headless reports record this search variant, and `WR_COMFORT_SEARCH` logs its elapsed time, work, accepted candidates, duplicates, host rejections, missing-data motions and stop allowance. No more comfortable route is guaranteed: forecast coverage, constraints and bounded pruning can leave the candidate set unchanged.

## Scope and limitations

Only completed routes accepted by engine validation and the existing host checks are included. Departure-family comparison uses passage duration, not earliest calendar arrival: departures may differ, and both departure and ETA are shown in UTC. Existing completed multi-leg departure itineraries are compared as complete timed itineraries using their selected leg results; arbitrary mixtures of engine legs are not constructed. Unmodelled waiting intervals are unknown.

Arrival-planning internal probes are not retained as interchangeable alternatives; only the delivered winner is retained for that mode. Candidates remain in memory for the current calculation and are not restored after restarting OpenCPN. Recalculation, removal or clearing invalidates an open comparison; the user is prompted to reopen it after computation.

The comparison finds the least exposed route **among the retained results**, including the bounded extra search results. Beam thinning and the completed-candidate cap remain heuristic; this does not establish a globally most comfortable passage. Broader exploration and calibration of the empirical severity model remain separate future work.

## Validation and measurements

The local build includes automated tests for time-weighted exposure, worst-leg retention, contiguous difficult spells, unknown coverage, slider endpoints/intermediate selections, dominance, ties, and validated-result retention with Auto stopping behaviour. The full suite passes 357 tests, including a synthetic rough-weather corridor where exploration discovers a slower, gentler detour, missing-wave policy checks, cancellation, budget exhaustion and independent rejection of unsafe alternatives.

An isolated real OpenCPN harness compares 1.21 and this prototype with the same compiler, build mode, boat, GRIB and route settings. It exercises Auto, All and a three-departure family, plus a wave-enabled GRIB and a failed departure outside forecast coverage. With exploration disabled, matching runs must preserve the chosen engine and fastest passage time; with exploration enabled the fastest result can improve. The new headless result JSON includes retained geometry, search variant, comfort metrics, coverage and worst-leg details.

GUI handler checks exercise row selection, wind-only toggling, slider positions and sorting; the selected result's plot, ETA and identity must agree. A separate disposable fixture deletes a route with its comparison still open to check safe invalidation. Logs must retain exactly the original four engine attempts despite all the UI selections.

Detailed local results, build logs and test harnesses are in `/home/paul/src/OpenCPN/output/xweather-1.22-prototype`. Small fixtures can measure retention/scoring and chart-switch costs, but do not establish overhead for long passages or chart-aware coastal searches. Compare Holyhead–Mouth of Foyle against 1.21 with identical forecasts/settings before drawing those conclusions.
