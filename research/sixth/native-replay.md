# Reproducing the native integration checks

The standalone comparison needs no OpenCPN profile. The additional native
checks require a built OpenCPN with chart services, the experimental plugin,
the GRIB plugin, a separately configured private profile and chart catalogue,
stored GRIB and polar inputs, and Xvfb on headless Linux.

The recorded replay uses `build/opencpn` from the parent OpenCPN repository,
`build-atlas-plugin/libxweather_routing_pi.so` from this worktree, and the
parent build's GRIB plugin. A private plugin directory contains symlinks to
these two libraries. No library is installed into the user's normal plugin
directory. The test profile points at the local CM93 chart collection and a
temporary SENC directory. Its boat XML selects the real Hallberg-Rassy 40
test polar by absolute path. Credentials, normal user data and certificates
are not needed or copied into the research artifacts.

Start with [the chart scenario](live-chart-qualification.json) and substitute
the boat path and covered historical forecast available on the machine. The
recorded input is
`environment_ukmo_ukv_tpxo_irish_sea_24h.grb`; its hash is in the final
comparison provenance. The scenario uses currents, enforced chart safety,
5 m minimum depth, and disables persistent certified routing-tile caching.
Wave coverage is optional; it is unavailable in this forecast.

Use `tools/headless-testing/run_headless.py` with `--engines atlas`,
`--plugin-dir` pointing at the private plugin directory, `--profile` pointing
at the private profile, `--grib` pointing at the stored forecast,
`--scenario` pointing at the substituted JSON, `--timeout 120`, and
`--xvfb --xvfb-binary` pointing at an available Xvfb executable. Each output
directory must be new. Native Xvfb socket binding required a sandbox
exception in this session; no external control or network service was used.

The `--opencpn` executable can be a small launcher which exports
`WR_HEADLESS_DATA_DIR` and `WR_SHORELINE_DATA_HOME`, then execs the actual
OpenCPN binary with its original arguments. Set those variables **inside**
the launcher: the headless wrapper deliberately removes inherited
`WR_HEADLESS_*` variables before launch. Use separate initially empty routing
data roots for the two chart variants. Do not reuse the normal application
data directory. The recorded launcher chooses full/fast roots using
`WR_ATLAS_FULL_CHART_GUIDANCE`.

Run serially with `WR_ATLAS_FULL_CHART_GUIDANCE=1` for full chart guidance,
then `WR_ATLAS_FULL_CHART_GUIDANCE=0` for GSHHG hints, using the same built
plugin and physical scenario. OS/SENC caches were not flushed. An intentional
cancelled scout/prewarm is separate from the subsequent completed Atlas
solve. In each log inspect `WR_ATLAS_SUMMARY`, the final native-candidate
`WR_GRID` summary, final route checks and the output JSON's `finalSafety`.
The [A/B extraction](chart-guidance-ab.json) retains both solver and chart
counts. Timing the whole process also includes startup/preparation/shutdown.

For membership dispatch, [the portfolio scenario](live-portfolio-scenario.json)
explicitly opts Atlas into Auto and All. This avoids changing saved default
memberships. Its replay is a dispatch/integration check, not the controlled
six-engine standalone timing matrix.
