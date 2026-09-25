# Weather Routing / xWeatherRouting 1.17.4

This release deliberately uses version 1.17.4 as requested for these branches;
it is not a rollback of the changes previously labelled 1.17.7–1.17.11.
It includes the recent memory-aware GRIB cache and reviewed first-use defaults,
compact shoreline package/optional High and Full manager, and upstream build fixes.

The Basic page now offers Quick (Hardened Original), Standard (former Quick),
and Professional (former Main). A first installation defaults to Quick;
upgrades preserve the existing algorithm and all saved tuning. Each engine
has independent settings, shoreline choice and GRIB cache preference.

Quick retains the catalogue contour-search geometry but requires a real final
connection and independent chronological validation. It fixes directional final
iteration, implicit tacking chords, incomplete shoreline-buffer checking and
unsafe missing-data assumptions. Search state and providers are request-local;
cancellation and resource exhaustion cannot produce a completed partial route.
The headless component remains independent of wxWidgets.

See [engine settings and limitations](quick-routing.md) for the data requirements,
strict endpoint margins, resource accounting and interpretation of search previews.

## Verification

The retained standalone audit contains 166 executions: paired catalogue/control
routes, buffer/full-shoreline/dateline regressions and real Pacific GRIB cases.
All hardened positive cases completed with independent validation; negative
coverage/endpoint cases rejected correctly. The Santa Cruz–Kāneʻohe forecast-only
case correctly stopped when forecast coverage ended, and is not claimed solved.

Release integration checks:

- Both plugin identities build successfully: xWeatherRouting against the working
  enhanced host API, and Weather Routing against the standalone stock API.
  Each passes all 311 tests.
- The independent core contracts pass, including address/undefined-behaviour,
  leak and thread sanitizer runs.
- A real 153 NM Pacific route with ECMWF/Copernicus GRIB, currents and Full GSHHG
  completes with independent validation in all three engines. Wave limiting was
  explicitly disabled because this input does not contain waves. A separate
  enabled-wave-limit test correctly rejects that input without exporting a route.
- Quick passes Tonga Full-shoreline and enforced CM93 chart checks, planned-arrival
  selection, and parallel departure scheduling. Land/depth endpoints and departures
  outside required current coverage reject without reporting completion.
- Isolated OpenCPN GUI checks verify the choice order, first-use Quick default,
  independent tuning, reset/cancel behaviour, saved-route reopening and last-used
  engine persistence across an application restart.

These integration runs are correctness checks, not a controlled speed ranking.
They do not establish that a forecast-only Santa Cruz–Kāneʻohe route can complete
past the end of its GRIB coverage. Existing Main/Standard missing-data policies
are unchanged; Quick's stricter requirements are explained in the engine guide.
