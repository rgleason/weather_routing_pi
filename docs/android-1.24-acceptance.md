# xWeatherRouting 1.24 Android acceptance — 2026-10-03

The established Android baseline was extended with the 1.24 optional fastest / comfort search. Its Plan, Routes, Results and Tools workspace, native selection controls and touch navigation remain in use. This is not a new desktop-to-Android UI port.

## How to use it

1. Select a route and open **Plan > Engine**.
2. Select Quick, Standard, Professional, Auto or All, then enable **Find fastest / comfort alternatives**.
3. Set **Additional search allowance (%)** and **Maximum additional time (seconds)**. The smaller allowance applies after the normal valid result is secured, per departure. For example, 200% permits twice the original computation time as extra work, subject to the seconds cap. Zero disables extras. These bounds do not limit finding the first route; Auto explores using its successful engine.
4. Compute, then open **Fastest / Comfort** from Results. Select a retained candidate or adjust the slider to allow additional passage time for comfort. Chart selection uses the retained validated route immediately without another search.

Up to four useful extra candidates are retained per departure; identical or dominated outcomes need not appear. This applies to a single departure and departure optimisation. All also retains its successful engine results. Additional computation time and additional passage time are separate limits.

Wind-only ranking remains the default and is saved. Full wind-and-wave ranking requires complete coverage. Wave coverage and missing-wave duration are displayed, and the worst known leg includes available waves regardless of ranking mode.

## Device and package

- Samsung SM-X210, Android 15, arm64; OpenCPN development app `org.opencpn.opencpn.dev`, 5.14.1-pob220-import-fix, plugin API 1.21.
- Android NDK 26.1.10909125 and the cached matching Qt/wxQt core support were used.
- Import package: `xweather_routing_pi-1.24.0-android-arm64-16-import.tar.gz`, 7,932,837 bytes, with root `metadata.xml` and plugin assets.
- Archive SHA256: `36b9bbea57ee8165b8f02e1449420b958a5b0f1635ad54db491ef31d6d3f4996`.
- Packaged and installed library SHA256: `01779bf61ac96909f1a6e4bca49388604cb289432e02e92168430c50e8376d08`.
- Source branch: `android/xweather-1.24`, based on desktop 1.24 commit `abeed6a`; shared runtime fixes are commit `360c931` (desktop cherry-pick `1adfbfa`).

## Verified results

All 376 native regression tests passed. The desktop build also passed all 376 tests after the shared fixes.

On the tablet, Quick, Standard, Professional, Auto and All completed both climatology and real-GRIB single-departure fixtures with extra exploration enabled. Original validated routes remained available when bounded exploration accepted no extra candidate or exhausted its allowance.

An offshore All fixture at 2026-10-03 12:00 UTC retained five candidates, including a useful extra Alternative result. Fastest selected the extra Alternative at 2:07:09; the comfort endpoint selected Standard at 2:07:14 with lower wind discomfort exposure (approximately 0.0253 versus 0.0263 severity-hours). Both had 100% wave coverage. Preview logs reported no search restart and approximately 3–5 ms for selection/refresh. This demonstrates candidate retention and selection, not representative long-passage performance.

The same run took 428 ms for the normal All result and set a 1,712 ms extra allowance at 400%, below its five-second cap. Logged engine exploration elapsed times totalled approximately 1.1 seconds. These measurements are from a short route and should not be extrapolated to long coastal passages.

Portrait and landscape layouts, candidate picker, sort selection, slider endpoints, chart preview, Android Back, typed limits and saved settings were exercised. Auto's typed 200% and three-second cap persisted in configuration XML. The progress footer shows a full-width “Stop exploration; keep results” button above Hide and Stop all computations. Physical soft-stop behaviour on a long tablet search remains untested; the shared cancellation contracts pass the native tests.

Status popup, subsequent candidate selection, chart rendering and recomputation pass together. Hot import through the OpenCPN plugin manager passed twice after dialogs and calculations had been used; the final package was imported and its installed hash verified. Cold launch with the original profile loaded version 1.24.0.

## Runtime corrections discovered during testing

- Initialise chart cursor coordinates as unknown and reject absent/invalid coordinates. Touch selection can occur before any cursor callback. Angle normalisation now handles huge and nonfinite values without an unbounded loop; a regression test covers this.
- Reuse the actual WeatherRoute entry for the status dialog. The previous temporary owning WeatherRoute deleted a borrowed live RouteMapOverlay when it went out of scope. A debugger watchpoint established that ownership failure.
- Flush deferred Qt widget deletions during plugin deinitialisation while its code is still loaded, preventing native event filters and delegates surviving hot replacement. An older installed plugin still executes its own deinitialisation code on the first upgrade.
- Avoid Qt's `slots` macro in shared search code and use the older wxQt-compatible UTF-8 overload.

## Preservation and remaining scope

The tablet's original six-route configuration and full preferences were restored byte for byte while OpenCPN was stopped. Hash verification found no changes or missing files among all 749 original weather-routing files (configuration plus 748 support files). Temporary route fixtures, copied GRIB, debugger and test load marker were removed; automatic rotation was restored. The backup and all test evidence remain on the host under `artifacts/xweather-1.24-android-20261003`.

The existing xGRIB 0.3.4 plugin crashes when its toolbar opens; captured native traces point into `libxgrib_pi.so`. Real-GRIB acceptance therefore used the bundled GRIB provider temporarily. Original provider settings were restored, and neither xGRIB nor the OpenCPN APK was replaced. Successful xGRIB handoff remains unverified pending that separate provider fix.

Long coastal passage stress tests, physical soft-stop during prolonged exploration, other screen sizes and the production APK remain further acceptance work. GSHHG does not supply charted-depth validation. Changes are committed locally; nothing is pushed or published by this pass.
