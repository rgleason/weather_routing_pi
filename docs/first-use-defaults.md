# First-use routing defaults (local development)

The Santa Cruz–Kāneʻohe test exposed repeated GRIB timeline reconstruction
with Quick's 64 MiB cache. At the diagnostic snapshot, Quick had made over
10,700 frame requests for only 54 distinct timestamps; roughly 22.5 minutes
of its first 24.25 minutes were spent servicing GRIB requests. Main had
completed the same endpoints at 3-hour/10-degree sampling in 7 minutes 27
seconds with a 2,061 MiB cache. These were different search settings, so this
is evidence of a cache bottleneck, not a controlled engine speed comparison.

## Candidate defaults

| Setting | Main | Quick |
| --- | --- | --- |
| Preferred GRIB timeline cache, 64-bit process | 2,048 MiB | 2,048 MiB |
| First-use true-wind-angle range | 40–160° | 40–160° |
| Heading separation | 10° | 10° (adaptive) |
| Shoreline detail | Intermediate (2) | Intermediate (2) |
| Maximum search angle | 120° | 120° |
| Maximum course angle | 180° | 180° |
| Maximum diverted course | 120° | 120° |
| Time step (unchanged) | 1 hour | 3 hours offshore (adaptive) |
| Search memory budget (unchanged) | Existing resource policy | 256 MiB |

Cache sizes are ceilings, allocated on demand. A larger cache requires enough
available physical RAM to hold it and leave 2 GiB plus twice its size free.
For example, 2,048 MiB needs 8 GiB available; 1,024 MiB needs 5 GiB;
512 MiB needs 3.5 GiB. When the requested size cannot be admitted, the policy
steps down through smaller allowances. Runtime memory guarding also applies
to a reduced allowance above the historical floor.

The historical low-memory/unknown-memory floors remain 512 MiB for Main and
64 MiB for Quick. The 32-bit defaults remain 192 MiB for Main and 64 MiB for
Quick, with a 192 MiB maximum. This does not promise that a large global GRIB
will route efficiently on a low-memory device.

Saved settings take precedence, including explicit 64 MiB caches, Crude
shorelines and older sampling settings. An existing installation must change
these settings explicitly to trial the new defaults. Balanced search presets
are revision 2 and reset heading separation to 10°; reset preserves cache
budgets, wind-angle constraints and shoreline choices.

The 40–160° range constrains sailing headings and is not appropriate for every
boat. It remains editable. Intermediate shorelines omit small coastal features;
nearshore routes can still require High or Full detail.

Before release, compare the Pacific case with identical endpoints, weather,
polar and shoreline: first change only Quick's cache from 64 to 2,048 MiB,
then compare 20° and 10° headings. Record runtime, cache reloads, completion,
arrival time and memory. Policy and persistence tests cannot establish these
end-to-end performance results. Version bump and publication are deferred.

## Local validation

- Linux xWeatherRouting plugin and test executable build successfully.
- All 303 CTest cases pass, including cache admission at simulated 32-bit and
  64-bit process widths, low/unknown RAM, saved-settings preservation, shoreline
  checks, routing and arrival-planning regressions.
- The running OpenCPN installation has not been replaced. Pacific-route timing
  with the revised defaults and GUI verification remain release checks.

## Basic and Advanced review

This is a review of factory behaviour for a fresh profile, not a migration of
saved routes. Only the agreed defaults above and explicit initialisation of
`UseCurrentTime=false` have been changed. The other recommendations below are
proposals, not implemented changes. Producing a completed route is not enough:
the route must reflect the selected vessel, available environmental data and
the user's operating limits.

Confirmed for the stock-OpenCPN first-use profile: Detect Land (GSHHG) stays
enabled; both loaded-chart awareness/enforcement boxes stay disabled; minimum
charted depth stays at 0 m; Optimise Tacking stays disabled until the user has
reviewed their vessel model and manoeuvre penalties. These were already the
factory values, so no migration or change to saved preferences is needed.

### Basic

| Control | Current first-use value | Recommendation / reason |
| --- | --- | --- |
| Start / end source | Named positions; first two available | Keep explicit selection. Check endpoint validity and explain coarse-shoreline rejection; do not silently move an endpoint. |
| Routing mode | Departure time | Keep; arrival planning runs several forward calculations. |
| Departure date/time | Time of configuration creation | Keep explicit time; tell the user when it is outside the loaded forecast. Do not silently change to a historical GRIB time. |
| Use current time | Off, now explicitly initialised | Keep. An entered departure should remain fixed unless this is enabled. |
| Time-zone display | UTC; local-zone preference separately saved | Keep. Calculations use UTC regardless of display choice. |
| Planned arrival | Creation time + 24 hours, inactive in departure mode | Placeholder only; user must choose an achievable arrival when enabling this mode. |
| Optimise departure | Off | Keep; avoids multiplying first-use computation. |
| Departure optimisation range / interval | ±6 hours / 60 minutes | Keep as initial trial values, only used when enabled. |
| Arrival safety margin | 30 minutes | Keep as an editable scheduling buffer, not a guaranteed safety allowance. |
| Boat | Bundled `Boat.xml`, referencing an Example/Test polar | Require a clearly labelled example/boat-selection prompt in a later usability pass. A fast successful calculation with the wrong polar is misleading. |
| Routing engine | Main | Keep: broader recovery options. Quick remains an explicit faster/smaller-search alternative, not a guaranteed speed improvement. |
| Max diverted course | 120° | Agreed and implemented for new configurations. Some backtracking routes need 180°. |
| Max true / apparent wind | 50 / 50 knots | Leave existing behaviour for this pass, but these are permissive calculation limits, not recommended sailing limits. A user should explicitly review them; no universal boat-independent value is defensible. |
| Max swell | 20 metres | Same issue: extremely permissive. Requires user review; absent wave data cannot establish that a sea-state limit was checked. |
| Detect Land | On | Keep. Do not disable it to make a route complete. |
| Check loaded charts / Require chart checks | Off / off | Keep as explicit opt-ins for supported hosts and suitable chart coverage. Portable first use should not depend on the enhanced host API. |
| Detect Boundary | Off | Keep; requires the boundary provider and configured exclusion areas. |
| Currents | Off | Candidate change: On when available, with explicit reporting when zero current is assumed. The provider currently falls back to zero when current data are missing. Compare weather-only and mixed-weather/current cases before adopting. |
| Optimise Tacking | Off | Keep for now. The option reaches the polar speed evaluator in both native engines; changing it alters the motion model and deserves a separate comparison. |
| GRIB | On | Keep; loaded data must cover the departure and relevant region. |
| Climatology | Most Likely | Keep provisionally for long-passage planning when the provider is installed. Clearly identify where forecast ends and climate-based routing begins. Missing climatology cannot extend forecast coverage. Avoid cumulative modes as a default: they select the legacy engine and are unsupported by Quick. |
| Last Valid if Data Deficient | Off | Keep. Reusing old weather should be an explicit user decision. |

### Advanced

| Control | Current first-use value | Recommendation / reason |
| --- | --- | --- |
| Main time step | 1 hour | Keep as general coastal starting point. The Pacific case supports testing a separate Ocean preset at 3 hours, not assuming one step fits every passage. |
| Main / Quick heading separation | 10° / 10° | Agreed and implemented; test Quick against the former 20° sampling after isolating the cache improvement. |
| Main effort | 100% | Keep initially; higher tiers cost more and do not cure a poor cache or invalid input. Measure failure rate before raising globally. |
| Search angle / course angle | 120° / 180° | Agreed. Search angle is relative to the current destination bearing; course-angle and diverted-course checks constrain different route geometry. |
| Reverse reachability recovery | Off | Confirmed: leave unticked by default; users can enable it deliberately for difficult arrivals. Arrival mode and some enforced-chart paths already enable it internally; this decision does not change those engine behaviours. |
| Quick offshore step | 180 minutes, adaptive | Keep. It refines near endpoints/coasts; this is not its step everywhere. |
| Quick search memory | 256 MiB | Keep while testing. Separate from weather-frame cache and total OpenCPN memory. |
| GRIB timeline cache | 2,048 MiB target on 64-bit | Agreed and implemented with memory admission/step-down. Existing smaller user settings remain unchanged. |
| Max latitude | ±90° | Keep as a permissive filter; it is not a promise of polar coverage, ice avoidance or high-latitude suitability. |
| Wind vs Current | 0 (disabled) | Keep. The code uses an opposing-vector criterion, not the simple speed difference described by its tooltip; clarify the label before suggesting a nonzero universal value. |
| Cyclone-track avoidance | Off; 1 month / 0 days window | Keep explicit opt-in, requiring historical cyclone data. Historical tracks are not a live cyclone forecast or a guarantee of avoidance. |
| Inverted Regions | Off | Keep; legacy-only, disabled for native Main/Quick. |
| Anchoring | Off in configuration | Needs a separate wiring/semantics audit: the native adapter does not forward this checkbox to `RoutingOptions::allowWaiting`, whose default is true. Do not claim that this checkbox currently disables native waiting. |
| Departure-time route workers | 0 (Auto) | Keep. Auto currently caps at four and reserves two logical CPUs; authoritative-chart work has additional limits. It is CPU-based, not a full per-job RAM admission policy. |
| Chart-safety RAM cache | 0 (Auto) | Keep; separately managed host cache. |
| Integrator | Newton | Keep; legacy-only control, disabled for native engines. |
| Wind strength | 100% | Keep; avoid silently scaling environmental inputs. |
| Tack / gybe / sail-change penalties | 0 / 0 / 0 seconds | Neutral mathematical defaults; prompt for vessel/crew-specific values. Nonzero penalties improve realism but arbitrary factory values can bias the answer. |
| Shoreline | Intermediate (2) for Main and Quick | Agreed. High/Full remain available for coastal detail. Enforced-chart scouting separately retains Crude (0); chart geometry is authoritative in that mode. |
| Land clearance | 0 NM | Do not invent a universal clearance. Require visible user review: zero means no extra stand-off, not that a route near a simplified coast is safe. A nonzero default needs tests against coastal endpoints and narrow passages. |
| Minimum charted depth | 0 m (disabled) | Keep until vessel draft/clearance and supported chart checks are configured; GSHHG alone has no depth information. |
| Motoring | Off; inactive threshold 2 kn / motor speed 5 kn | Keep off; enabling a fictional engine to obtain a result would change the voyage model. User must supply appropriate values. |
| Upwind / downwind / night efficiency | 100% / 100% / 100% | Keep neutral; adjust to the vessel's actual performance. |
| Courses relative to true wind | 40–160° | Agreed. Editable; this range excludes sailing outside it and may not suit every polar. |
| Use polar optimal angles | Off | Keep pending a wiring audit: the flag is used by legacy `Position.cpp`, but is not passed by the native adapter. It is distinct from Optimise Tacking. |

### Persistence and remaining work

`OnNew` copies a selected configuration when one exists; otherwise it builds
factory defaults and overlays LastUsedConfiguration. Existing XML routes retain
their explicit values. Search, weather, safety, polar, penalty and cache choices
are saved by the configuration editor. Time-zone and chart-service preferences
are saved separately. Route-specific endpoints, times, routing mode and departure
optimisation settings are preserved in route XML, but are not all carried into
the global last-used template. This distinction should be explained rather than
promising that every field automatically becomes the default for unrelated new
routes.

The review found that `UseCurrentTime` lacked an initialiser in both the
constructor and factory path. It is now explicitly false; loading a saved XML
value still overrides it.

Recommended next comparisons: equal-cache Main/Quick coastal and ocean routes;
reverse recovery off/on for difficult arrivals; currents off/on with complete,
missing and partial current coverage; 1-hour/3-hour Main steps. Separately review
Anchoring and Use polar optimal angles wiring and the wind/current tooltip.
The overall concurrent-thread preference (outside these tabs) defaults to the
CPU count for ordinary independent jobs; memory-aware batch admission deserves
its own review before claiming robust defaults for many simultaneous routes.

Implementation references: `src/WeatherRouting.cpp` (factory, last-used and XML
settings); `src/RouteMap.cpp` (constructor); `src/ConfigurationDialog.cpp`
(editing and persistence); `src/engine/native/OpenCpnAdapter.cpp` (effective
engine inputs); `src/WeatherDataProvider.cpp` (data fallback);
`include/DepartureScheduler.h` (concurrency); `include/ChartSafetyDefaults.h`;
`data/boats/Boat.xml` and its example polar.
