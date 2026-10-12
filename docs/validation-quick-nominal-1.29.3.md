# xWeatherRouting 1.29.3 qualification

Local qualification completed 12 October 2026. The user approved release publication after local GUI testing. Platform build and publication evidence is recorded separately from this local qualification.

## Policy and regression boundaries

Quick uses the configured nominal route timestep (one hour by default); the existing synchronous endpoint reductions remain. Its hidden three-hour offshore override is removed. Standard retains its previous timestep policy. Professional retains its separate contour incumbent’s previous step through a request-only override: the saved `DeltaTime`, spatial cell size and intermediate bridge tuning are not changed.

During qualification, a 27 September Conwy noon departure exposed an unintended change to Professional’s internal contour recovery. Restoring only the request timestep fixed that regression. All 17 real Auto departures requiring Professional were replayed after the final correction and completed with final plot validation. The other 178 departure results were in the unchanged Quick/Standard paths.

The bounded shoreline exception remains fixed around the original endpoints. Reach is clamped to 0.5–2 NM and normally twice the land margin. Full clearance applies outside these fixed areas; actual land, required depth and chart hazards remain checked. The previous coast-following and long-chord regressions remain in the passing component suite.

## Irish Sea Auto matrix

Each row contains 13 distinct departures using the saved GUI route settings, Nicholson 35, current or historical wind/current GRIBs, full shoreline resolution, 0.4 NM land clearance, 3 m minimum depth and chart-aware propagation. Sailing angles, wind multiplier, maneuver penalties, motor threshold and departure windows were retained. Counts show the engine delivering the validated route, after the earlier engines failed.

| Departure date | Holyhead destination | Quick | Standard | Professional |
|---|---|---:|---:|---:|
| 2026-07-28 | Conwy | 10 | 3 | 0 |
| 2026-07-28 | Dublin | 8 | 5 | 0 |
| 2026-07-28 | Foyle | 0 | 11 | 2 |
| 2026-08-29 | Conwy | 3 | 5 | 5 |
| 2026-08-29 | Dublin | 9 | 3 | 1 |
| 2026-08-29 | Foyle | 0 | 11 | 2 |
| 2026-09-27 | Conwy | 7 | 1 | 5 |
| 2026-09-27 | Dublin | 10 | 3 | 0 |
| 2026-09-27 | Foyle | 1 | 12 | 0 |
| 2026-10-17 | Conwy | 13 | 0 | 0 |
| 2026-10-17 | Dublin | 0 | 13 | 0 |
| 2026-10-17 | Foyle | 8 | 4 | 1 |
| 2026-10-19 | Conwy | 13 | 0 | 0 |
| 2026-10-19 | Dublin | 0 | 13 | 0 |
| 2026-10-19 | Foyle | 8 | 4 | 1 |

**195/195 distinct departures completed: 90 Quick, 88 Standard, 17 Professional. All 195 delivered routes passed final chronological and plot safety validation.** The 17 final Professional replays are repetitions, not extra distinct departures. The October 17/19 matrix accounts for 78 departures: 42 Quick, 34 Standard, 2 Professional.

The problematic 17 October 06:00 local Conwy departure now completes with Quick: 27,077 seconds passage, 37.247 NM. The previous build needed Professional for that departure. All 13 Conwy departures on each October date now complete with Quick.

The historical matrix uses 28 July, 29 August and 27 September departures with their corresponding July 27, August 28 and September 26 GRIBs. The current matrix uses the October 11 combined Irish Sea GRIB. Where a route exceeds forecast coverage, the explicitly configured fallback sources are recorded in the host logs; this is not a claim that every second uses GRIB data.

Real Professional deliveries exercise its forward search and composite contour bridges. This corpus did not deliver a route through reverse or graph recovery; those stages have component regression coverage and were exercised unsuccessfully by the bounded westbound Atlantic comparison. No constraints or search budgets were weakened to force a particular engine.

Standard retains some long detours already present in the previous build. Successful qualification is a feasibility/safety check, not a claim that every route is optimal.

## Ocean tests and older GRIB audit

| Route | Auto result | Passage | Distance |
|---|---|---:|---:|
| Santa Cruz offshore to Kaneohe | Quick exhausts allowance; Standard completes | 1,707,785 s | 2,164.960 NM |
| Horta offshore to Lizard | Quick exhausts allowance; Standard completes | 893,734 s | 1,358.318 NM |
| Lizard to Provincetown | Both previous build and final 1.29.3 exhaust search resources | No validated arrival | N/A |

Ocean tests enforce full shoreline geometry and 0.4 NM clearance. Charts were disabled and minimum depth set to zero for these offshore fixtures; they do not qualify chart-aware ocean depth coverage.

The Pacific sailing-only fixture uses Nicholson 35 with motor and currents disabled. Its old/new Standard results have identical passage, distance and route fingerprint. Wind attribution is 1,290,600 seconds GRIB plus 417,185 seconds configured climatology fallback. Optional waves are unavailable. Whole-host peak resident memory was about 777.8 MiB new versus 797.4 MiB old; the 256 MiB search allowance is not a whole-process cap. Concurrent runs make wall-clock timings unsuitable as a controlled performance benchmark.

The Pacific source GRIB predates the xGRIB dateline fixes. Its longitude grid was inspected: 196.25–251.75 degrees east, monotonically ordered, equivalent to -163.75–-108.25 degrees; it does not cross the dateline. The original combined file is rejected by the current xGRIB 0.3.9 decoded-file memory guard (512 MiB). Its subsequent timeline request can crash xGRIB with both old and new routing builds, before routing begins. This is an existing xGRIB problem separate from the Quick change.

For the sailing-only Pacific test, only the exact 10 m U/V wind messages were copied to a smaller GRIB. `grib_compare` confirms these messages are identical to the original: no resampling or wind modification. The original rejected file was not silently treated as valid coverage. The Atlantic grid also does not cross the dateline; these tests do not establish dateline-crossing correctness.

The Azores test uses the combined Atlantic forecast and the saved vessel, maneuver penalties and motor settings with explicitly substituted offshore endpoints. All 893,734 wind seconds use GRIB. Current attribution is 853,200 seconds GRIB, 37,800 seconds configured XTD fallback, and 2,734 seconds assumed zero. Optional waves are unavailable.

The full westbound Atlantic comparison ended normally in both builds. Old: 2,546.21 seconds host wall time; final 1.29.3: 2,455.99 seconds. The final Professional search counters match exactly: 16,559,218 generated states, 267,933 retained states, 148,028 graph labels, 107,486 validation samples and closest approach 792.006 NM. Forward, reverse, frontier and graph recovery remain bounded. Both report resource exhaustion, rather than a timeout or crash. This is a pre-existing unsolved test and does not establish physical infeasibility.

## Automated checks and packaging

- Final C++/contract suite: **484/484 passed** (36.02 seconds). Includes timestep settings, impossible polar speed prevention, low-wind/zero handling, waiting, endpoint safety, chronology and recovery coverage.
- Python packaging suite: **15/15 passed in each repository**, including the final-package check which excludes CPack's intermediate staging archives.
- Stock Flatpak 5.14.0-1+91f3b67, ABI `flatpak-x86_64:25.08`: corrected package imports; the previous incorrectly suffixed package is rejected; Quick, Standard and Professional smoke routes complete.
- Android arm64 package: target/API compatibility and 16 KiB ELF alignment gates pass.
- Prior label audit covered 44 artifacts; Flatpak and Jammy label families needed correction. Other labels retain their expected target mapping. Full remote platform builds remain part of the later publication step.

Desktop GUI verification confirms 60-minute default, an edited 15-minute nominal Quick setting, unchanged 180-minute Standard offshore setting, and retained 15-minute shared nominal value when switching to Professional. The displayed build is 1.29.3; the Conwy 06:00 route completes in the GUI.

## Installed build and evidence

The working laptop runs the final qualified 1.29.3 library. Runtime mappings and build diagnostics confirm the loaded binary, rather than merely a copied file. All 809 personal routing files and navigation logical rows are preserved; other launcher plugin prefixes are unchanged. Previous plugin prefixes and private backups are retained.

On the attached Samsung SM-X210, the package imports through the normal plugin manager. All three engines complete a two-mile light-wind motoring fixture with a 10-minute configured timestep. Quick’s editor correctly displays that nominal value. With the one-hour default, all three engines complete a 25 NM fixture; Quick propagates 20 legs / 203,986 generated states and independently validates the route in 4.066 seconds.

A deliberately fine 10-minute Quick search of the 25 NM fixture reaches its retained-state allowance after about 31.5 seconds. Standard and Professional solve it. Auto reproduces the bounded Quick stop (29.761 seconds), falls back to Standard (0.528 seconds) and delivers a validated route. Finer configured steps can consume the unchanged Quick search allowance faster; honoring the timestep does not guarantee standalone Quick success on every route.

The tablet’s original preferences were restored exactly before restart. All 749 original routing files, navigation logical rows, the other plugin libraries and 20 other plugin installation records are preserved; no test routing files remain. After restart, runtime mappings verify the exact qualified library is loaded, normal import metadata reports 1.29.3, and bundled help matches the package. SQLite database bytes changed during app use, but the logical navigation rows are unchanged.

Android library SHA-256: `bb03f504a0eff0361f3aabbb9aaa596d09b2240dd451782c9ce6ba84517c42b3`.

Native library SHA-256: `9f1bdad06f70ad406c9f160267c64a79d872e11797f8df228f1de6b32243346d`.

Local evidence: `output/xweather-quick-nominal-20261012/release-qualification.json`, `host-qualification.json`, `professional-regression-cases.json`, `ctest-final-policy.log`, packaging logs, Flatpak acceptance receipt, ocean host logs, and desktop/tablet installation receipts. Private profile backups and navigation data must not be published.

The same source, tests and documentation are mirrored into the Rick integration worktree while retaining its repository-specific metadata. This report records the local qualification completed before the remote release matrix and publication.
