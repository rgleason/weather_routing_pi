# Quick3 completion and weather-source correctness

This pass targets Weather Routing 1.37.0 and xWeatherRouting 2.4.0. It preserves the six independent routing methods and their internal numerical adaptation.

## User behaviour

With Automatic adaptive settings enabled, Auto tries bounded Quick3 completion after the last selected Quick/Quick2 method if neither has returned an accepted route. Auto then returns a validated Quick3 solution immediately. All also tries Quick3 at that point and continues comparing every remaining selected method; the earliest accepted arrival remains its initial winner. Quick3 is a completion stage, not a seventh manually selectable routing method. Switching Automatic settings off follows the established manual calculation path and preserves the user's values.

A selected Quick3 result is named **Quick3 recovery** in the route state, progress window and retained-route comparison. The diagnostic report preserves Quick/Quick2 attempts and adds a separate Quick3 attempt with elapsed wall time and known work counters. A generated Quick3 candidate does not become the winner merely because it exists; an equal or earlier existing winner remains selected. Comfort searches use the actual source method's configuration and identify their own solver rather than attributing unrelated searches to Quick3.

## Bounded implementation

Quick/Quick2 can publish small immutable prefix snapshots. Quick3 considers nearby proposed endpoints (at most 40 NM), independently replays each prefix to establish its real position and time, then tries cheap destination-directed connections and a small forward frontier. It retains approach-side/time alternatives. This prototype is not a backward-time weather integrator.

The default phase allowance is 3 seconds cooperatively checked, 4,000 motion calls, eight seeds and 2,048 retained local states, with a maximum 12-hour tail. A synchronous weather/chart-provider call already in progress can extend the wall-clock deadline. The phase does not borrow a later method's numerical allowance, cancel the parent operation when its own allowance ends, or enlarge the global search budget.

Every accepted joined route receives fresh chronological validation from the original departure to the actual destination, with the original vessel, current, wind, wave, manoeuvre, clearance and depth policies. The host also checks delivered plotting chords using the candidate's prepared shoreline/context. Invalid earlier prefixes are rejected. No artificial final line or automatic motoring is added. Explicit motor settings, moving-time state and the independent validator's existing conservative motor-time limit remain enforced.

## Weather and memory corrections

Completed-route source totals and warnings come from chronological validation rather than just each leg's first weather sample. This exposes fallback inside an otherwise GRIB-labelled leg, including its measured duration and recorded transition location/time. Missing and estimated wave information and current fallback reporting use the same measured results.

Forecast-boundary buckets retain requested times instead of rounding outside coverage into a valid 15-minute cache bucket. Version 2 uses its forecast extents; legacy providers certify bucket edges/centre using available frames. Interior sampling and each branch's wind interpolation remain unchanged.

The frame cache is governed by its existing admitted byte budget and physical/process memory guards. Raising the metadata count ceiling from 512 to 32,768 prevents small frames from being discarded while most of the user's admitted RAM budget is unused. Missing-frame entries remain separately bounded. Cache allocation is demand-driven, not an up-front reservation.

## Separate handoff experiment

`WR_QUICK_HANDOFF_PROTOTYPE=1` enables an additional narrow experiment: Auto may stop failed Quick numerical retries on an offshore passage of at least 800 NM after repeated chronological repair failures, provided Quick2 is selected. Individual methods and All retain their existing attempts. This experiment is **off by default**; it is separate from Quick3 and from the cache/reporting corrections.

In the fixed Hanalei-to-Nuku-Hiva production-host control it skipped the 43.7-second hourly retry and returned the same Quick2 track/ETA. Seventeen longer corpus controls retained identical acceptance and ETA outcomes, including one triggered handoff. Concurrent screening timings are not comparable serial performance measurements. These observations justify further experiments, not a universal default rule.

## Qualification and reproduction

The development repository's `tools/adaptive-settings-lab` accepts `completion_prototype`, `typed_auto` and `handoff_prototype` scenario flags. It honours corpus numerical controls and analytic weather origins, records initial/final statuses and completion work, and audits completed tracks using a fresh uncached provider wrapper and cancellation token. Provider adapters differ from the actual OpenCPN host, so host tests are recorded separately.

Qualification includes the 501-case corpus (142 real-GRIB inputs), all six methods, negative controls, fresh replay, typed automatic-controller comparisons, longer-route follow-up and actual installed-plugin host checks. Artificial 3/6-second screening deadlines under concurrent CPU load are bounded stress tests, not claims that every route should complete within those times or evidence of a speed regression when a deadline outcome differs. Final counts and immutable input/binary receipts are in the pass's qualification record.

The vendored solver implementations are unchanged. The immutable-prefix archive helper adds an optional admission radius, so Auto rejects endpoints farther than 40 NM before copying their paths; its default unrestricted behaviour and All partial-route inspection remain unchanged. The development freeze manifest explicitly qualifies the source-reporting/forecast adapter changes and optional controller handoff; the small archive-header change is explicitly qualified in that manifest as well. No claim is made that the two branches now have identical ETAs: their established wind interpolation policies are deliberately preserved.

## Upgrade and shutdown

Upgrading from 1.36 to 1.37 retains the same bundled data and skips the legacy import prompt that could offer to overwrite personal boats, polars or route configurations. A teardown guard prevents worker shutdown from refreshing settings after the host config service has ended. The retained-candidate UI contract also exercises selecting Quick3 and switching back without recalculation.
