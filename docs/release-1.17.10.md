# Weather Routing 1.17.10

Weather Routing 1.17.10 accelerates chart-aware route preparation while
preserving the Main engine's solver and authoritative chart-safety rules.
Coastal, depth-enabled and final-validation checks still use the exact fine
chart path and continue to fail closed when a chart cannot prove safety.

For land-only routing, preparation now begins with a distance-scaled scout
corridor and expands with the live solver frontier instead of always building
the entire filled reachability envelope first. Open-water misses prepare a
bounded, aligned 0.2-degree block through the existing 0.05-degree
authoritative classifier. The block becomes a coarse safe certificate only
after all 16 fine masks exist and are clear. Mixed, missing, coastal,
depth-enabled and forced-fine cases cannot use that shortcut.

Already cached authoritative tiles are made resident without copying their
cell arrays or asking OpenCPN to publish them again. Progress and aggregate
chart-grid statistics now include cached tiles and all bounded host calls.
The former filled-envelope preparation remains available for diagnostic
comparison with `XWEATHERROUTING_FLAT_CHART_PREWARM=1`.

## Regression and performance evidence

Cold-cache runs used identical OpenCPN and xWeatherRouting binaries, an
isolated profile, the same CM93 charts, GRIB, Nicholson 35 polar and routing
settings. Only the diagnostic filled-envelope switch differed.

| Route and result | Adaptive 1.17.10 | Filled-envelope control |
|---|---:|---:|
| Niue to Vava'u, 0 m minimum depth, median wall time | 54.573 s (4 runs) | 60.977 s (3 runs) |
| Initial raw tiles | 1,196 | 4,074 |
| Routed distance | 237.18613040278294 NM | 237.18613040278294 NM |
| Generated / retained states | 668,368 / 29,587 | 668,368 / 29,587 |
| Route fingerprint | `910b3a2b05d4b1c2` | `910b3a2b05d4b1c2` |
| Final chart safety | Pass | Pass |

The adaptive median was 10.5% faster in this case and reduced initial tile
preparation by 70.6%. Every adaptive sample was faster than every control
sample. All seven runs produced the identical route geometry, candidate hash,
search counts, distance, ETA and final authoritative safety result.

The established Holyhead-to-Conwy coastal regression also reproduced the
exact reference route: 50.77761312661434 NM, 39,519 seconds simulated time,
23 route points, fingerprint `b6d94c026232209a`, and final safety Pass. Its
measured wall time was 34.007 seconds versus the recorded 36.007-second
reference.

With the original 2 m minimum depth at the precise Vava'u endpoint, adaptive
and control modes retain the same intentional fail-closed result. CM93 does
not provide usable depth proof at that point, and both modes report the same
endpoint coverage diagnostic in about 8.3 seconds.

The full xWeatherRouting suite passes 298 tests, and the OpenCPN chart-safety
scheduling suite passes all 12 tests.
