# Local qualification: automatic adaptive settings

Date: 14 November 2026. Linux, wxWidgets 3.2.12 plugin with the existing private
OpenCPN 5.15 test host. Production candidate: 1.36.3; development candidate: 2.2.4.
Windows/platform qualification remains pending. No stable release is claimed here.

* Full suites: 714 enabled development tests and 631 production tests passed.
  The existing disabled ocean timing comparison remains disabled.
* Private-host contracts passed on both branches, including startup preference,
  restart, applicable disabled controls, unchanged resource ceiling, cross-route
  preference isolation, and exact 5370-second / 7.25-degree manual values. Real
  XML output retained both automatic/manual flags and those exact numeric values.
* Manual-host Quick, Quick2 and Standard completed with no adaptive preflight.
  Auto stopped after its first accepted Quick result. All attempted all six
  selected engines and selected the minimum accepted completed ETA.
* A real TPXO forecast completed with Auto, Quick2 and Standard. Sampled current
  reached 0.994 m/s with vector change 0.473 m/s/hour; Quick/Quick2 selected hourly
  inputs. Advisory sampling took approximately one millisecond in these runs.
* Arrival planning completed with Auto, Quick2 and Standard with existing
  per-departure and transaction work ceilings retained.
* A–B–C–A completed all three legs; two enabled legs used the controller, while
  the manual middle leg retained its 5370-second input. Departure times chained
  from the preceding arrivals. Return passages remain valid.
* Rick's reconstructed England-to-islands passage used his logged endpoints,
  departure, example boat, Climatology wind, currents off and stated limits.
  Quick stopped on unavailable wind during its search, then Quick2 completed
  with authoritative final safety passing. Auto stopped immediately. Result:
  3792.914 NM, 2119900 seconds passage time, arrival 8 December 2026 02:00:40 UTC.
  Solver attempts took approximately 30.4 seconds, including a 46 ms first
  preflight. This is not an exact reproduction or Windows performance comparison:
  Rick's original configuration files are unavailable. Wave heights were unknown
  and the illustrative boat / Climatology qualifications remained visible.

Portable-core qualification used 501 cases (142 real-GRIB, 16 negative controls).
Quick first-success and comparison each validated 426 routes; all first-success
runs stopped after acceptance. Quick2's corrected port retained 461 paired
validated tracks. Standard's paired completed tracks matched; one short-deadline
censoring case completed identically in a longer paired rerun. No accepted
negative controls, independent replay failures or process errors occurred in the
corrected campaigns. Concurrent screening times are not performance benchmarks.

Original vendor kernels retain their recorded hashes. The separate adaptive
sources, adapter and explicit headless input extension are recorded in the
development footprint. Both branches preserve the completed provider-backoff
fixes; development also preserves its JPEG2000 and date/time-control corrections.
The working OpenCPN binary, second-row wrapper, desktop entry and plugin hashes
were checked unchanged. Only private test profiles/installations were used.

Additional source-mode qualification found and fixed an advisory integration gap:
a selected but unavailable forecast with allowed Climatology fallback supplied
no preflight VMG estimate. Quick2 kept one-hour inputs and hit its two-million
proposal guard. Permitted prepared fallback estimates now select six-hour
nominal inputs on the reconstructed ocean passage; Quick2 completes with
1,216,866 proposals and exactly the explicit-Climatology track in version 2.
The classic branch also completes through its copied external Climatology
provider. No extra forecast frame requests or provider-cache insertions are
performed by preflight. Prepared-callback contention is handled without waiting.

A vector-reference-frame test preserves known forecast currents when only wind
needs advisory fallback. A combined fixture with real TPXO currents and later,
non-overlapping wind dates samples 0.994 m/s current and 0.473 m/s/hour vector
change and selects hourly stepping. The ordinary combined wind/current TPXO
case remains the separate control for actual forecast-current route use.

That mixed fixture exposed an existing authoritative-provider limitation:
OpenCpnWeatherProvider::current gates current availability on the wind sample's
availability. Valid forecast currents can therefore fall back when forecast
wind is absent. This behavior is present in manual mode and is not changed by
this numerical-settings pass. Actual route-source warnings still report current
fallback. Pure current-only files are also rejected by the version-2 loader's
existing surface-wind requirement. These are separate provider/import follow-ups;
the mixed fixture verifies advisory sampling, not delivered forecast-current use.

Native Windows compilation initially found a missing direct array include and
an identifier colliding with Windows' near macro. Both were corrected, with a
compile contract that includes the public adaptive headers after near/far macros.
The Windows trial packager now derives its version from CMake and links to the
qualified source, avoiding a stale hard-coded version check.
