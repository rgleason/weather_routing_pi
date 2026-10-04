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

## 1.18.5 chart follow-up, including polar fix f0a2546

26 September 2026, same SM-X210 / Android 15 / OpenCPN 5.14.0/API1.21.
The final build includes `f0a2546457b93d778b7a9bfc02b3c73db0fec3b8` (polar
wind-range extrapolation fix). The UI commit follows that commit; it does not
replace or omit it. This changes routing performance policy, so earlier route
fingerprints above are historical results, not expected fingerprints for this
new build. All 29 focused polar and deterministic Irish Sea engine tests passed,
including the five new polar wind-range cases.

Final installed library SHA-256:
`1907ec949ef044f0a43651603a8c4c4907829d1f16b65c9b01c6daf17c6cbd7d`.
Imported through the host Plugin Manager; the stripped archive library matched
the installed library. The common 1.18.5 version and existing Android CircleCI
cohort are retained. No remote xWeatherRouting publication was performed.

- The user's accidental arrival deadline was changed to their intended fixed
  departure, 27 September 14:00 UTC. Cards, Plan and Results now explicitly
  distinguish Departure from Arrival deadline; the time editor headings fit.
- A real 96-hour Irish Sea/North Channel GRIB was generated on the tablet,
  bounds 50.5–56.5 N / 8.5–2.5 W, UKV wind hourly through f54 and every three
  hours through f96, with GFS waves and Copernicus NWS currents. Individual
  wind coverage and bounds were independently verified with ecCodes.
- Before f0a2546, Foyle completed at 172.6 NM / 31h16m and was demonstrated
  on the chart, retained after Close, and saved once as an OpenCPN track.
  That saved track survived cold starts. It remains preserved and can appear
  alongside a newly calculated overlay; it is not the new route's fingerprint.
- After f0a2546 and the final xGRIB 0.3.2 import, Foyle completed in 12.34 s:
  **158.3 NM / 30h09m**, 21 legs, 832 validation samples, fingerprint
  `ce0fbe263dceb934`. Eight requested workers were capped to one at 677 MiB
  available memory. The configuration used GRIB weather; currents remained
  disabled in the routing configuration despite being present in the forecast.
- Results → Show on chart framed the complete Foyle course. Reopening and
  closing the workspace retained its visible course. No duplicate track was
  saved for the new calculation.
- The 13-candidate Dun Laoghaire comparison finished processing all candidates:
  11 completed, two returned `search_incomplete` at the early departures.
  These are recorded as incomplete, not misreported as successful routes.
  The base candidate showed ETA 28 September 06:55 UTC. Show on chart selected
  the completed candidate in the main workspace, framed it, and immediately
  hid both comparison and progress sheets while other candidates kept running.
  Only the chosen candidate was made visible, not all 13 alternatives.
- Only OpenCPN 5.14 remains installed. Stock 5.10.2 was removed at the user's
  request using `pm uninstall -k`, retaining its data and backed-up APK/navigation
  XML. The development package's home shortcut was verified.
- xGRIB 0.3.2.0 adds the UTC calendar/time selector and optional encrypted
  Copernicus password remembrance. Real encrypted Close/cold restore followed
  by a second successful 761-message, 48.95 MiB generation passed. xGRIB's
  detailed device evidence and Back fixes are in its 0.3.2 usability notes.

Final packages, logs, screenshots, forecast read-back and hashes are in
`artifacts/xweather-chart-fix-20260926/` and
`artifacts/xgrib-0.3.2-android-20260926/` in the parent workspace. The earlier
xGRIB 0.3.1 alpha publication succeeded for all 11 targets; these new local
versions have not been published. The host Plugin Manager chooser ANR after
Qt text editing remains a host limitation, avoided with cold-start imports.


## Route setup scrolling correction — 1.18.6, 26 September 2026

The user reported that Safety stopped at Optimise Tacking. Reproduced on the
SM-X210: the scrollbar moved while reparented static-box controls remained in
place. Recalculating virtual size alone was insufficient. The Android scroll
bridge now lays out controls at the updated wx scroll offset, with deferred
layout after native scroll events. Setup sections refresh geometry after
styling, on opening and when selected.

Built and packaged against the same pinned OpenCPN 5.14/Qt/wxQt host. Imported
1.18.6 through Plugin Manager, then installed the exact final archive library
after a normal shutdown for the final help-height/caption-width corrections.
The final installed library SHA256 is
`5d2f2e6018b9a818e6e60729836ad9ee71ae88b1cc76425c2177c03275033011`.

Content swipes reach Sail Plan Change Time, the final Safety setting, with a
visible bottom margin in landscape and portrait. Optimise Tacking was fully
visible and enabled on a Standard route, toggled off, then restored to its
original enabled value. The chart-depth help is completely readable in both
orientations. The Plan workspace and Routes cards also continued to scroll.

This is an Android UI change; routing mathematics are unchanged. The source
remains descended from the user's `f0a2546` polar correction. No additional
routing computation was required for this UI-only correction. It remains
local pending a separate xWeatherRouting publication request.

## Android 1.23 integration — 2 October 2026

Branch `android/xweather-1.23` starts from the tested Android branch at
`33e57f0` and merges the shared 1.23 source at `ca3e22e`. The existing Plan,
Routes, Results and Tools workspace, file chooser adapters, saved route
handling, scrolling, chart actions and Android memory policies are retained.

Auto, Quick, Standard, Professional and All (slow) use the existing touch
engine picker. Fresh last-used profiles and bundled first-install examples
select Auto; previously saved profiles and legacy route XML preserve their
engine selections. Professional uses the upgraded shared engine and exposes
its integrated Quick controls in the existing Engine section.

Results → Fastest / Comfort opens a modeless Android sheet with a touch
slider, sorting and candidate pickers, saved wind-only and extra-search
preferences, scrolling whole-route metrics, UTC timestamps, missing-wave
coverage and worst-known-leg information. Done and Android Back close the
sheet; Show selected on chart uses the existing chart action. Selection
uses retained validated geometry and never launches another search.

Device validation uses the same Samsung SM-X210, Android 15 and OpenCPN
development 5.14/API 1.21 host as the baseline. Three isolated 7.16 NM
offshore configurations completed and validated using installed Most Likely
climatology: Auto selected Quick (4,149 seconds); All selected Standard
(4,139 seconds) and retained Quick, Standard, Alternative and Professional;
Professional completed in 4,139 seconds. All ran the bounded comfort pass;
this short fixture produced no extra unique route or comfort trade-off.
These are integration checks, not long-passage performance measurements.

Tablet testing exposed wxQt's column-text getter returning column zero for
every column. Comparison presentation now retains its own row values, as the
earlier Android comparison sheets do. Mode switching refits the Engine page
and wraps its descriptions with Qt font measurements. The shared tests also
check first-install sample configuration defaults.

The installed xGRIB library crashed on toolbar opening with the same stack
as the pre-existing 2 October 01:08 crash. It was not changed by this task;
GRIB handoff could not be accepted through that installed library. Native
routing and comfort device checks therefore use climatology. Shared GRIB,
engine, comfort, migration and lifecycle regression tests remain applicable.

Backups, package hashes, build/test logs and screenshots are retained in the
parent workspace's `artifacts/xweather-1.23-android-20261002/`. Device test
configurations are temporary; original routing XML is restored after testing.
This update is local and does not publish to a plugin catalogue.
