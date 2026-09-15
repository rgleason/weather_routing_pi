# o-charts integration benchmark — 2026-08-29

This controlled benchmark used the Holyhead (outside TSS) to Mouth of Lough
Foyle scenario, the same UKMO/Copernicus GRIB, Nicholson 35 polar, thirteen
hourly departures, routing effort 200%, a 0.4 NM land margin and a 5 m minimum
depth. Every run used a 30-minute wall-clock ceiling. Chart-aware production
concurrency was four; the GSHHS control used eight, so total wall time is not a
direct chart-overhead measurement.

## Preparation

| Cache state | Requested | Disk/RAM reuse | New tiles | Preparation |
| --- | ---: | ---: | ---: | ---: |
| Cold initial footprint | 554 | 0 | 554 | 133.896 s |
| Expanding warm footprint | 716 | 698 | 18 | 19.889 s |
| Fully warm restart | 716 | 716 | 0 | 0.277 s |

The fully warm run made no semantic provider/object-extraction calls. Its main
716-tile disk load took 147 ms. The persistent store ended with 2,295 live
semantic tiles in a 41.8 MiB file. The expanding run demonstrates that scout
geometry is a prefetch hint rather than a search boundary: a changed scout
footprint safely extends the store, while production remains free to request
additional tiles.

The cold preparation classified 931,274 cells: 430,031 land, 497,764 water,
3,479 drying and zero unknown. Provider failures were zero.

## Matched chart-aware searches

Both cold and warm chart-aware runs completed the same departures and reported
no failed candidates:

| Offset | Cold search | Warm search | Distance | Final safety |
| ---: | ---: | ---: | ---: | --- |
| -60 min | 364.318 s | 263.513 s | 179.555 NM | pass |
| -120 min | 365.226 s | 251.895 s | 183.209 NM | pass |
| -180 min | 379.978 s | 341.977 s | 192.048 NM | pass |

For each completed departure, the cold and warm JSON payloads were byte-for-
byte identical after selecting distance, elapsed passage time, ETA, final
safety and all route points/timestamps. Generated and retained state counts
also matched. Persistence therefore changed timing, not route semantics.

## GSHHS control and authoritative replay

The matched GSHHS-only control completed seven departures within the ceiling.
Its routes were replayed segment by segment against the persisted o-chart
semantic mask at the same 0.4 NM/5 m safety settings, including the normal
zero-margin recheck for endpoint-adjacent margin hits. Every route had at
least four non-endpoint unsafe segments; the routes were therefore not valid
chart-safe comparisons. One route also encountered one uncached segment, but
already had nine authoritative unsafe segments, so its route-level result was
still unsafe.

This replay explains the completion-count difference without treating GSHHS
land clearance as authoritative proof. It does not claim that the remaining
departure times are impossible: chart-aware candidates still active at the
ceiling remain incomplete, not failed, and may find different safe routes with
more search time.

## Regression result

- GSHHS-only routing made zero chart-safety API calls.
- Cold and warm chart-aware runs had zero unavailable-chart fallbacks.
- Production searches retained thousands of states and were not clipped to a
  direct or scout corridor.
- The fully warm authoritative preparation target was met at 277 ms.
- All 185 xWeatherRouting tests and all 9 core chart/depth service tests passed
  with the benchmarked binaries at this checkpoint.

## Wider-envelope GUI acceptance

The final live GUI run used the filled reachability prewarm introduced after
the controlled comparison above.  For the 137.501 NM passage it covered every
point satisfying the logged 184.988 NM maximum-path ellipse, with a 61.875 NM
maximum cross-track extent.  This is cache coverage only and did not constrain
the production solver.

The expanding persistent-cache run requested 3,652 semantic tiles, reused
2,168, built 1,484 and failed zero.  Preparation took 309.641 s.  The following
route/scout footprint requests reused 554, 554 and 362 tiles in 8 ms, 9 ms and
7 ms respectively.  No provider extraction or unexpected production tile
build occurred after prewarm.  The semantic cache finished at 73,678,075 bytes.

All thirteen hourly departures completed.  Each final route passed
authoritative validation in one round with no missing samples; every production
chart-safety API call had authoritative chart data and there were zero
unavailable-chart fallbacks.  Searches generated 473,318--812,537 states and
retained 7,802--12,209 states, confirming that the broad weather searches were
not reduced to the short-lived scout paths.  The completed implementation
passes 188 xWeatherRouting tests and all 9 core chart/depth service tests.

The prior 716-tile fully warm measurement demonstrates sub-second persistent
cache loading.  A restart benchmark of the larger 3,652-tile envelope remains
useful for measuring its exact warm-load time; it is not needed to establish
the successful routing, validation, persistence or no-fallback results above.

The clean standalone qualification contains 188 compiled xWeatherRouting
tests plus three build/contract checks (191 CTest entries in total); all pass.
