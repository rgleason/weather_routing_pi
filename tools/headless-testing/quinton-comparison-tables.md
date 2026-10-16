# xWeatherRouting and Cowboy comparison

No valid Cowboy rescue of the three named failures has been demonstrated in these tests.

The comparisons use the supplied San Francisco/Cape forecasts and fresh standard OpenCPN ECMWF forecasts for California–Hawaii and Tuamotu. They use the saved settings and the bundled demonstration polar referenced by Boat.xml, so they are not exact reproductions of every reported run. The plugin version used for each original test remains unverified.

## Reported results and Cowboy comparisons

Distances and passage times below are **Quinton’s reported figures**, not Cowboy’s. Passage time is time at sea, not computation time. “Not replayed” does not imply either success or failure.

| Route | Quinton’s result | Distance NM | Passage h:mm | Quinton computation | Cowboy comparison | Cowboy search s | Cowboy validation |
|---|---|---:|---:|---|---|---:|---|
| Farallon west → Pinole | Completed | 48.75 | 16:17 | Not recorded | Produced a candidate | 8.0 | **Not certified:** forward replay failed; chart audit encountered unavailable coverage |
| Pinole → Farallon west | Completed | 46.75 | 18:56 | Not recorded | Produced a candidate | 10.8 | **Rejected:** CM93 land crossing |
| Waialua → Maalaea | Failed initially; completed at zero clearance | 135.31 | 26:49 | Not recorded | Not replayed | — | Different clearance constraints between reported attempts |
| Alcatraz east → Kaneohe | No route | — | — | About 10 min reported | No completion with fresh ECMWF | 50.9 | No completed candidate to validate |
| Panama offshore → Galapagos | Completed | 1,010.45 | 185:04 | Not recorded | Not replayed | — | No comparable replay result |
| Galapagos → Nuku Hiva | Completed | 3,231.96 | 413:18 | Not recorded | Not replayed | — | Demonstrates a reported ocean completion |
| Hao offshore → Rangiroa | Failed with charts | — | — | Not recorded | Reported destination enclosed, but could not connect to it | 14.1 | **Not an arrival** |
| Rangiroa → Hao offshore | Failed with charts | — | — | Not recorded | Produced a 511.2 NM candidate | 7.8 | **Rejected:** chart clearance violation; forward replay also failed |
| Kraalbaai → Cape Town | No route | — | — | Not recorded | No route | 0.10 | Both endpoints fail the laptop’s chart check at the saved 0.1 NM clearance |
| Cape Town → Hout Bay | Completed | 16.00 | 02:32 | Not recorded | No route in Full GSHHG comparison | 0.11 | Shoreline/host settings differ from an exact original replay |
| Hout Bay → Simonstown | Completed; described as slow | 44.73 | 05:56 | Not recorded; described as slow | No route in Full GSHHG comparison | <0.01 | Original computation duration unknown |
| Simonstown → Knysna offshore | Completed | 281.22 | 37:30 | Not recorded | Not replayed | — | Incomplete/estimated waves |
| Simonstown → Knysna Lagoon | Completed, but reported crossing land | 282.59 | 37:38 | Not recorded | Not replayed | — | Available Full GSHHG blocks the endpoint; this is a dataset qualification, not a claim that it is physically land |
| Cape Town → Knysna offshore | Completed | 324.34 | 45:56 | Not recorded | Not replayed | — | Incomplete/estimated waves |
| Naples → Stromboli | Completed | 167.99 | 26:47 | Not recorded | Not replayed | — | No usable waves reported |

**Timing scope:** Cowboy times are standalone search/connection times to the stated outcome. They exclude initial GRIB/polar loading, the subsequent independent forward replay, and the CM93 audit. A fast rejected candidate or early failure is not a fast validated completion. Quinton’s approximately ten-minute figure is a reported observation, not an instrumented timing.

Cowboy searches using GSHHG in these comparisons. Its candidates were subsequently checked against current motion validation and the laptop’s CM93 charts. **Passing GSHHG alone does not establish chart validity.** Chart findings refer to the datasets and chart-safety implementation available on the test laptop, not a verified copy of Quinton’s chart installation. A bounded test without completion is not proof that a route is impossible.

## Per-engine timings from local xWeatherRouting runs

All values are **seconds elapsed inside the named engine invocation**, including reconstruction/validation and any host-service waits during that invocation. They exclude preparation performed before the engine started and the later separate CM93 audit. Professional includes its internal Quick attempt and subsequent solver stages.

The logs also contain an initial short Quick scout used for chart preparation. That scout is excluded here; the table uses the main route attempt. Small times below generally describe early failures under blocking constraints, not fast successful routings. **Not reached** means an earlier engine was still running when the overall test was stopped. **Not run** means that engine was not selected for the individual test.

| Route / test settings | Quick s | Standard s | Alternative s | Professional s | Outcome |
|---|---:|---:|---:|---:|---|
| Kraalbaai → Cape Town; saved clearance | 0.170 | 0.030 | 0.005 | 14.194 | No engine completed |
| Hao → Rangiroa; saved minimum depth | 0.027 | 0.027 | 0.038 | 0.038 | No engine completed; attempted exit had unknown depth |
| Rangiroa → Hao; chart checks | 103.745 (cancelled) | Not reached | Not reached | Not reached | Quick cancelled at overall time limit; later engines not reached |
| Alcatraz → Kaneohe; chart checks | 132.518 (cancelled) | Not reached | Not reached | Not reached | Quick cancelled at overall time limit; later engines not reached |
| Kraalbaai → Cape Town; zero clearance | 0.166 | 0.028 | 0.007 | 9.401 | No engine completed |
| Kraalbaai → Cape Town; zero clearance, ten-minute timestep | 0.906 | 0.016 | 0.007 | 13.719 | No engine completed |
| Hao → Rangiroa; depth requirement disabled | 72.393 (cancelled) | Not reached | Not reached | Not reached | Quick cancelled at overall time limit; later engines not reached |
| Alcatraz → Kaneohe; Standard, explicit climatology, GSHHG only | Not run | 72.265; repeat 38.692 | Not run | Not run | Completed under GSHHG; subsequent CM93 audit rejected the candidate |
| Alcatraz → Kaneohe; Standard, forecast only, GSHHG only | Not run | 119.921 (cancelled) | Not run | Not run | Stopped at the diagnostic time limit |

## Local chart-enabled xWeatherRouting replay timings

These are measured wall times from the route-test preparation/start boundary, including chart preparation, weather requests, search and validation performed during the run. They exclude application startup. They are **not directly comparable to Cowboy’s standalone search-only times**. All runs below used the laptop’s chart installation.

| Route / mode | Measured route wall time | Outcome and qualification |
|---|---:|---|
| Alcatraz east → Kaneohe, All | 180.8 s | Stopped at the diagnostic time limit while Quick was still running; later engines were not assessed |
| Kraalbaai → Cape Town, All | 27.4 s | All attempts ended without a validated route |
| Hao offshore → Rangiroa, All | 63.9 s | All attempts ended without a validated route; unknown chart depths affected the attempted exit |
| Rangiroa → Hao offshore, All | 150.0 s | Stopped at the diagnostic time limit while Quick was still running; later engines were not assessed |
| Alcatraz east → Kaneohe, Standard with climatology | 180 s test limit | Stopped during chart preparation before Standard began solving; not a solver-failure timing |

The last run logged approximately **14,900 chart tiles and 171 seconds of tile-preparation work**. That preparation time is part of the run’s elapsed time, not an additional duration to add to it.

## Standalone controlled offshore benchmark timings

These runs use the separate controlled 1,858 NM offshore case: steady wind, a common HR40 polar and a configured three-hour timestep. They do not represent Quinton’s real-weather cases or include OpenCPN/chart preparation. Engine-specific internal sampling and resource policies differ. Native timings include their own reconstruction and validation; Cowboy’s later independent replay is outside its search timer.

| Engine / allowance | Engine time s | Outcome |
|---|---:|---|
| Cowboy | 4.0 | Completed; independent replay subsequently passed |
| Quick, 65,536 nodes | 29.5 | Stopped at retained-lineage limit |
| Standard | 5.6 | Validated completion |
| Quick, 131,072 nodes | 101.0 | Validated completion |

Alternative and the complete plugin Professional workflow were not timed on this controlled case. No timings are inferred for them.

## Settings experiments

| Case | Change tested | Measured time | Result | Implication |
|---|---|---|---|---|
| Alcatraz → Kaneohe | Quick with a three-hour timestep | 3.7 s — standalone engine | Reached about 369 NM from the destination, then reported missing forecast coverage | A larger node allowance cannot supply missing weather |
| Alcatraz → Kaneohe | Standard with explicit climatology fallback and GSHHG checks | 72.4 s — route wall time; export repeat 38.8 s | Completed: **2,233.6 NM, 294:35**; subsequently rejected by CM93 near departure | Forecast duration matters, but the completed candidate still needs chart validation |
| Kraalbaai → Cape Town | Zero clearance | 18.4 s — route wall time | Still failed | Clearance is not the only obstruction |
| Kraalbaai → Cape Town | Zero clearance plus ten-minute timestep | 25.1 s — route wall time | Still failed | Smaller steps did not resolve the available-chart constraints |
| Hao → Rangiroa | Disable the 1.5 m minimum-depth requirement | Stopped at 120.1 s — route wall time | Progressed beyond the immediate depth obstruction; no completion within the bounded test | Unknown chart depths explain part of the failure; disabling depth checks does not prove a safe solution |
| **Separate controlled offshore benchmark** | Quick: 65,536 → 131,072 nodes, at three-hour timestep | 101.0 s — standalone engine | **Validated completion**, about 101 seconds | Genuine evidence that the allowance can prevent completion on a solvable case; not proof for Quinton’s named routes |

**Timing scope:** Settings experiments mix standalone engine timings and application route wall timings, as labelled. Native engine timings include the engine’s own reconstruction and validation. Route wall timings include preparation and host-service waits, but exclude application startup and the later separate CM93 audit. The two successful Standard timings came from separate runs which reproduced the same route; they show observed variation, not a controlled speed comparison.

The controlled node-allowance experiment can already be tried through Quick’s **Search memory budget (MiB)** setting: **256 → 512**. No code change is required. This setting is a search-storage allowance, not a measurement or limit of total OpenCPN RAM usage.

Professional already runs internal Quick with a larger allowance when RAM permits. The default on this 64-bit test system admitted **262,144 nodes**. Increasing the saved Quick budget can also increase that internal allowance, subject to resource admission. No benefit from increasing it further has been established for these named failures.

Clearance and depth reductions above were diagnostic experiments, not recommendations to weaken safety constraints. No production plugin or routing-engine code was changed.
