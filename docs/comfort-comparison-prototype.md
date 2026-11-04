# xWeatherRouting 1.24 comfort exploration prototype

This prototype retains completed validated routes and compares passage time with whole-route discomfort. Optional extra searches now work with Quick, Standard, Professional, Auto and All (slow). The isolated branch is `prototype/xweather-1.24-alternatives`, based on the 1.23 Professional/Android integration commit `8b178de`.

## Using the prototype

Open **xWeatherRouting 1.24 Prototype**. It uses the API 1.23 test OpenCPN, a separate profile and a separate plugin prefix in `output/xweather-1.24-prototype`. The working OpenCPN wrapper and installed 1.23 library are unchanged.

In **Configuration > Basic > Routing engine**, select an engine and optionally tick **Search additional fastest / comfort alternatives**. Set **Additional search allowance (%)** and **Maximum additional time (seconds)**. Defaults are 200% and 20 seconds. The ordinary route is secured first; extra computation uses the smaller allowance. For example, a 3-second ordinary calculation at 200% permits 6 additional seconds, limited by the seconds cap. This is separate from Professional's effort setting. Zero in either field disables extra work. Controls and their saved values apply per route and per departure candidate. They are disabled for arrival planning and while computing.

New configurations default to exploration off. Existing All configurations without the new field inherit the previous saved All-exploration preference, keeping the 1.23 behaviour. XML saves all three fields explicitly; last-used defaults remember them for newly created routes. Wind-only ranking remains enabled by default and remembered separately.

1. Load a GRIB, choose a voyage and compute the selected engine with optional exploration enabled. Alternatively compute departure candidates, then select a completed member of that departure family.
2. Right-click the routing results table and select **Compare Fastest / Comfort...**.
3. Select any row to put that retained validated route on the chart and update the usual routing table. Sort by passage time, discomfort exposure, average discomfort, worst known leg or another column.
4. Move the slider to select the lowest exposure within the displayed extra-time allowance. The left endpoint selects the fastest; the right selects the minimum exposure among comparable retained results. Intermediate positions allow a fraction of the time difference between those endpoints. Because the routes are discrete, the chart changes in steps.

Auto keeps its normal stop-at-first-success fallback, then optionally explores using that successful engine. Individually selected engines use themselves for extra search. All retains its four original engine results and shares one additional allowance across its successful engines. Up to four extra candidates are retained per departure; fewer, including zero, are possible. A single comparable result, or a fastest result which is also least exposed, displays that no trade-off is available. Sorting or moving the slider never invokes an engine.

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

The ordinary engine selection, existing safety constraints, effort and recovery behaviour remain unchanged. Extra search starts only after the ordinary result passes engine validation and delivered-chord host checks. Failed normal searches do not spend the comfort allowance and are never restricted by its limits.

Standard uses its existing comfort-aware partial-path selection lanes as well as refined and diversified searches. Quick and Professional use their own solvers for refined searches and detours through weather-ranked intermediate lanes. These deliberate reruns do not alter vessel speed to reward comfort, and do not substitute Standard routes for another engine. They are bounded search heuristics, rather than continuation of every native frontier or proof of a global optimum. Joined detours receive new whole-route metrics and full chronological replay from the original departure, including manoeuvre, fuel and shoreline constraints. Intermediate lanes receive no extra coastal-buffer exemption.

Bounds per departure are:

- Four extra retained candidates, deduplicated against ordinary results. Dominated extra candidates are discarded; the original results remain available.
- Passage duration at most 50% longer than each engine's original passage, capped at 12 extra hours and the existing route-duration ceiling.
- Two million generated states, twelve million counted weather calls and twelve solver attempts. All shares the time and work allowances across successful engines.
- Existing native memory limits remain; extra Standard search uses at most 64 MiB and retained native state/graph-label ceilings are 32,768.
- The configured percentage allowance and seconds cap apply to the complete extra phase, including validation. Deadlines are cooperative: an in-flight provider or host call can overrun. Attempts receive smaller slices so one refinement cannot spend the complete allowance before detours are tested.

**Stop exploration; keep results** in the progress dialog stops currently active extra phases and preserves the ordinary route and already accepted extras. It does not suppress later queued departures. **Stop all computations** retains its existing whole-operation cancellation behaviour. Expiry, exhausted work, missing data or failed optional attempts preserve the ordinary result.

The comparison's **Rank by wind comfort only** preference is snapshotted at computation start. Available waves remain in the independent worst-known-leg metric and delivered legs. Full-condition comfort candidates require complete wind and wave-height data. Existing hard wind, wave, propulsion, land, boundary and chart constraints still apply in either mode.

Departure optimisation explores each successful departure in its existing worker; it creates no extra workers. Internal arrival-deadline probes, scouts and failed departures do not explore. Candidates remain in memory for the current calculation. The chosen engine still initially displays the fastest validated result; a newly discovered faster extra may become that result. Other extras are inspected through **Compare Fastest / Comfort...**.

Extra rows retain the actual engine provenance and a `comfort-wind` or `comfort-waves` search variant. `WR_COMFORT_SEARCH` reports allowance, elapsed time, attempts, work, validation and accepted results. Additional candidates and improved comfort are not guaranteed.

## Scope and limitations

Only completed routes accepted by engine validation and the existing host checks are included. Departure-family comparison uses passage duration, not earliest calendar arrival: departures may differ, and both departure and ETA are shown in UTC. Existing completed multi-leg departure itineraries are compared as complete timed itineraries using their selected leg results; arbitrary mixtures of engine legs are not constructed. Unmodelled waiting intervals are unknown.

Arrival-planning internal probes are not retained as interchangeable alternatives; only the delivered winner is retained for that mode. Candidates remain in memory for the current calculation and are not restored after restarting OpenCPN. Recalculation, removal or clearing invalidates an open comparison; the user is prompted to reopen it after computation.

The comparison finds the least exposed route **among the retained results**, including the bounded extra search results. Beam thinning and the completed-candidate cap remain heuristic; this does not establish a globally most comfortable passage. Broader exploration and calibration of the empirical severity model remain separate future work.

## Validation and measurements

Native tests cover time-weighted exposure, worst-leg retention, missing coverage, comparison selection, all three individually selectable engines finding gentler alternatives in a rough-weather corridor, independent replay, extra budget exhaustion, soft stop and whole cancellation. Headless scenario parsing checks percentage/seconds limits and explicit enablement.

Isolated OpenCPN regression cases use the same boat, GRIB and settings for ordinary and optional searches. Results contain retained geometry, actual engine identity, search variant, comfort metrics and coverage. Host GUI contracts exercise configuration event handlers and persistence, soft-stop actions, cached row selection, slider movement, sorting, worst-leg invariance and safe invalidation. Logs and local harnesses are in `output/xweather-1.24-prototype`. Short fixtures demonstrate behaviour, but long coastal/chart-aware passages still require user study before installing this prototype into the working OpenCPN.
