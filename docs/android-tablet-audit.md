# Android tablet audit for xWeatherRouting 1.18.4

Tablet acceptance record, 26 September 2026. Device: Samsung SM-X210, Android 15,
arm64, OpenCPN 5.14 development build (API 1.21), xGRIB 0.3.1. The
production OpenCPN package and other Android screen sizes are not yet tested.

## Verified on this tablet

| Area | Observed result |
| --- | --- |
| Workspace | Plan/Routes/Results/Tools touch navigation; root window remains visible after cold restart. Route cards support multiple selection; dragging over Edit scrolls without opening the editor. |
| Forecast provider | Real 96-hour Irish Sea / North Channel GRIB generated on the tablet with xGRIB. UKV wind hourly through 54h then 3h; GFS waves. Routing retains the selected provider after xGRIB Close. |
| Nicholson 35 routes | Quick Conwy and Dun Laoghaire completed. Standard completed Conwy, Dun Laoghaire and Lough Foyle with the Irish forecast; Latest fixed 14:00 UTC run: Lough Foyle arrived 27 September 19:46 UTC (29h46m). Professional Conwy completed with 480 validation samples and 14,688 land rejections. |
| Atlantic fallback | Latest Standard run, departure 26 September 14:00 UTC: 2,883.14 NM, 20d10h35m06s, computed in 3m46s. CSV shows GRIB through 30 September 00:10 UTC, then Climatology. Earlier departure also completed (3,017.15 NM / 21d6h33m51s). |
| Batch | Fractional 0.5-day range / 0.25-day spacing produced three departures exactly six hours apart; a 2,401-route batch cancelled promptly and retained its original template. Two departures one hour apart × two wind strengths (80/120%) produced exactly four routes. All four computed, with distinct geometry fingerprints. Boat removal/addition and picker Back worked after editing time fields. |
| Memory | Shared cache/worker policies apply to desktop and Android. At 434 MiB headroom, a 2,048 MiB request admitted 54 MiB; eight workers were capped at one. Both Nicholson coastal routes completed; requested preferences remained 2,048 MiB, and batch release freed 52.7 MiB retained frames. Windows/macOS readers are not natively built or tested here. Shared shoreline tile retention now adapts too, keeping complete geometry and the original per-tile allocation bound. |
| Planned arrival | Nicholson Quick Holyhead–Conwy found departure 16:21:07 UTC and ETA 22:13:29 UTC, meeting the 22:43:32 arrival with a 30-minute margin. Save track and GPX success sheets remained visible and dismissible. |
| Time persistence | Legacy local XML now opens at the correct UTC instant on Android. New XML carries departure/arrival epoch seconds. Cold restart retained batch departures at 11:25 and 12:25 UTC. |
| Stability | GLES corridor crash reproduced and fixed. Three validated alternatives, 416 cells; pinned chart overlay, close and chart pan worked without a new crash. Candidate 2 selection changed the actual displayed route. |
| Inspectors | Route position scrolls through weather fields; plot controls scroll, position/zoom sliders move and readouts persist after finger lift. Route table Previous/Next works; Columns Cancel and Save preserve/apply choices. |
| Empty states | No routes disables Compute all; no positions disables Delete all positions. Confirmation Back/No retained the isolated disposable data; Yes removed only the test fixture. |
| Polar editor | Isolated Nicholson test copy: edits/save/cancel/reopen, angle add/remove, True wind calculation and three editor pages checked. Original conservative polar retained. |
| Outputs | Latest Atlantic CSV: 168 data rows, 34 columns; saved to public Downloads and read back. GPX: 168 points. Independently compared every Atlantic host track timestamp and route ETD against GPX: all 168 matched, including planned departure and arrival. Combined route/track UTC checks also passed. API 1.21 combined routes store explicit ETDs and incoming-leg speeds on destination points. Nicholson planned-arrival GPX first/last instants matched departure/ETA. |
| Host route selection | Full-canvas touch chooser: Cancel/Back and Use route worked. Coastal route created seven multi-leg configurations with unique endpoint GUIDs and inherited Professional settings. Seven legs completed sequentially; every next departure matched the preceding ETA. |
| Native pickers | ANR traced to blocking Java picker/Qt IME callback circular wait. Standalone JNI worker adapter added to both plugins; WR polar selection after text editing and xGRIB Open returned normally. Cross-plugin typing → xGRIB Open → Irish selection passed with stable PID and no new ANR. xGRIB SAF import returned normally and imported bytes matched the source hash. Generator 96h/hourly/UKV preferences survived a cold start. WR now uses its own touch file browser for boat/polar/output files. |

Quick failed to reach Lough Foyle and Atlantic under its search limits. Standard
completed those routes. These are engine outcomes, not successful Quick tests.
Older Atlantic-forecast coastal runs are excluded from Irish forecast acceptance.

## Automated checks

Additional output checks: compact Atlantic simplification reduced 168 points
to 161 with 0.093 NM maximum deviation and passed shoreline validation. Applied
GPX retained endpoint coordinates and UTC times; Save track still emitted all
168 full-route points with matching timestamps. Completion while filtered out
now restores distance, ETA and weather after Clear all filters. Native report
pages render cleanly in both orientations; multi-route scrolling and rotation also passed with clean background gutters.

Desktop regression: 323/323 passed after shared low-memory policy, direct-leg course iteration and empty-polar safety corrections. The final VMG guard also passed all 23 polar tests.
xGRIB regression: 33/33 passed; private TLS isolation check passed.
CircleCI artifact preparation: 5/5 tests passed, covering ten platform pairs.
Android builds succeed locally; remote CI and publication have not been run.

## Additional accepted device flows

- Two successive seven-leg optimisation applications (Best, then Selected)
  each left exactly seven routes; group Weather editing retained all individual
  GUIDs and departure epochs.
- Planned-arrival controls, fractional batch times, invalid bounds, missing
  connections, preview Back, Reset and cancellation passed. The final isolated
  1,001-route progress test rotated to portrait while active; Cancel retained
  the original template and 384 generated routes. All were disposable fixtures.
- Polar generation, True/Apparent measurement input, save/reopen, invalid input,
  empty dimensions, removal of the last angle and VMG guards passed. The editor
  preserves saved speed precision. Both original Nicholson boat and conservative
  polar match their backups by SHA-256.
- Chart-pick dragging pans; a stationary tap selects coordinates. Name and
  coordinate validation, own-position rename/delete and Back passed. Display,
  filtering, Help, Manual browser return and About were exercised.
- Cursor plot and inspector explicitly identify the unvalidated preview; its
  time axis uses its own duration and unavailable polar data remains explicit.
  Actual route position, sliders, editable columns and inspector scrolling passed.
- GLES stability corridor: three alternatives, 364 cells (219 inner, 270 outer,
  45 unsafe excluded); chart zoom/pan, portrait rotation, candidate selection,
  unpin, repin and deletion of the owning candidate passed without a crash.
- Shoreline manager is reachable from Tools. High and Full downloads, checksum
  verification, selection/Apply and cold persistence passed. Active Full Cancel
  stopped after 7,985,574 bytes and retained the existing verified dataset.
  Intermediate resolution/cache 64 MiB were restored. Only owned backup copies
  were removed; the original Intermediate dataset was preserved.
- Swiping over buttons, readout labels and noneditable dropdowns scrolls the
  sheet; stationary taps open the action/dropdown. Dropdown selection passed.
- GPX collision handling preserved existing files (1) through (10) and created
  (11). Independent XML comparison confirmed all eight coordinates and UTC
  instants. Temporary collision fixtures were removed after hash verification.

## Scope and remaining release gates

The accepted device is the SM-X210 with OpenCPN 5.14/API 1.21. Production
OpenCPN 5.10.2, another screen size/font scale, Windows/macOS native builds and
remote CircleCI remain separate release gates. Android packages are local;
publication has not been performed.

The host Plugin Manager's blocking native chooser can still ANR after Qt text
editing. Cold-start imports avoid that host issue; the plugin file adapters and
cross-plugin chooser sequence were tested independently. This port does not
claim to fix the host Plugin Manager.

Final package/import evidence and chronological details are recorded in the
workspace artifacts `artifacts/xweather-android-20260926/session-progress.md`.

## Final installation

Final xWeatherRouting and xGRIB 0.3.1 archives were imported through the host
Plugin Manager and both installed library hashes matched their archives. Cold
startup retained six positions and five Nicholson configurations. xGRIB and
xWeatherRouting are enabled; bundled GRIB is disabled. Both final packages are
in public Downloads. Package/library hashes and source commits are recorded in
`artifacts/xweather-android-20260926/final-packages.json`.

The final installed coastal run reproduced Quick Conwy, Standard Dun Laoghaire,
Standard Lough Foyle and Professional Conwy fingerprints. All completed, with
eight requested workers capped to one at 431–494 MiB available RAM.

Disposable editor, batch and collision files were archived before removal.
Earlier host route/track test outputs remain preserved pending separate cleanup
approval; automatic review rejected a host navigation-data cleanup operation.

The final installed Atlantic Standard run also completed in 227.9 seconds:
2,883.14 NM, 20d10h35m06s, 167 legs, fingerprint `e9811b87fb8eb6f7`.
The result reports GRIB + Climatology with 408h25m of Climatology coverage;
it matches the earlier independently checked CSV/GPX/host output run.

The complete Android CircleCI build script also passed locally using the pinned
core/support/NDK cache. It validated AArch64, SONAME/core dependency, bundled
shoreline data and import metadata. Its packaged library is byte-for-byte
identical to the final tablet-tested library. Remote CircleCI is still unrun.
