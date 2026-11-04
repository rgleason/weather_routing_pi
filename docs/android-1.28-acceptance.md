# Android 1.28 acceptance — 9 October 2026

The existing Android Plan, Routes, Results and Tools workspace, route cards,
touch controls and configuration sheets remain the base. Version 1.28 adds the
shared route visibility and result-clearing behavior, with departure display
choices adapted to that workflow.

## Tablet checks

The Samsung SM-X210, Android 15, runs the stock OpenCPN development app 5.14.1
and plugin API 1.21. A deterministic offshore, uniform-wind forecast and the
Nicholson 35 boat produced five completed departures at 30-minute intervals.
This is a controlled regression fixture, not an operational forecast.

The following checks passed on the final locally built arm64 library:

- Selected departure follows the highlighted comparison candidate, shows only
  that departure, and remains Selected when using the Show on chart button.
- All departures shows the whole group and remains All when returning to the
  chart. The route cards report the current display choice.
- Changing a route card's Visible on chart checkbox switches the group to
  Manual; selecting another candidate preserves the individual choices.
- Changing only the display choice in Plan → Time and pressing Done retains
  all five completed results. The Android Done handler no longer reapplies
  unchanged hidden configuration controls and invalidates a completed candidate.
- Clear computed results clears the selected candidate, preserves its route
  configuration, and leaves the other four completed candidates intact.
  Recomputing restores the cleared candidate's result. Hiding during the
  recomputation remains effective at completion.
- The comparison selector, explanation and Time controls fit in portrait and
  landscape. A 45-minute step entered through the Android numeric keyboard
  survives Done and reopening Time; Done remains reachable with the keyboard.

Selected, All and Manual were also exercised through the mobile comparison
sheet and route cards before the final Done-handler correction; their behavior
was unchanged by that correction. Screenshots, package hashes and private
profile-preservation evidence remain in the local acceptance output.

The final import package was installed through the tablet's normal Plugin
Manager. After a cold restart, the mapped library matches the tested library
hash, the bundled help matches the package and installation metadata reports
1.28.0. All 749 original writable routing files match their pre-test hashes,
the navigation database's logical rows are unchanged and the other plugin
libraries are unchanged. Original preferences and screen rotation were restored.
Private backups and synthetic forecast files are not committed or published.

## Builds and shared behavior

The Android release library builds with NDK 26.1.10909125 against the stock
Android core. ELF LOAD/RELRO alignment checks pass for a 16 KiB layout. The
tablet's running kernel uses 4 KiB pages; execution on a 16 KiB kernel is not
claimed. This change does not require an enhanced Android core.

All 420 local Linux tests passed. CircleCI pipeline 145, workflow
`87e14a38-9787-4ea6-9447-1980a22abf59`, passed native Windows Server 2022 jobs
1481 (32-bit) and 1482 (64-bit), with 423 tests each. Both loaded the packaged
plugin in isolated stock OpenCPN 5.14.2 profiles, checked the departure display
contract in four passes, and exercised visibility, clearing, queue handling and
three recomputation cycles. The tested revision was
`a39a25940d6bb1616c671064f9884c7e142c951c`, containing the shared 1.28 behavior
from `f6fb949ea2ee758e3e56986ce48f08434460fe8e`. The subsequent source changes
are Android-only, plus documentation.

This records local Android and native Windows qualification. It is not a claim
that the full release matrix or catalogue publication has completed.
