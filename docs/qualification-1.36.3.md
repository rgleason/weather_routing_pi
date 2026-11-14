# Local qualification: automatic adaptive settings

Date: 14 November 2026. Linux, wxWidgets 3.2.12 plugin with the existing private
OpenCPN 5.15 test host. Production candidate: 1.36.3; development candidate: 2.2.4.
Windows/platform qualification remains pending. No stable release is claimed here.

* Full suites: 713 enabled development tests and 630 production tests passed.
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
