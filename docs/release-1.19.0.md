# xWeatherRouting 1.19.0

This release adds the Android arm64 edition to the same CircleCI build and
reviewed alpha publication matrix as the desktop editions. It targets OpenCPN
5.14 with API 1.21 and uses the pinned Android core, support libraries and NDK.

The Android interface is redesigned around Plan, Routes, Results and Tools,
with touch route cards, full-screen editors, scrollable settings, departure and
arrival time controls, route comparison and chart actions. Boat and polar
editing, batch planning, reports, plots, route tables, shoreline management,
file browsing and navigation exports have tablet-specific layouts and actions.
Route setup remains scrollable to its final control in both orientations.

Shared memory policies adapt GRIB caches, shoreline retention and worker limits
to available memory while preserving requested settings and complete forecast
and shoreline geometry. Android picker handling avoids the blocking JNI/Qt
interaction observed during tablet testing. New saved routing times include
explicit UTC instants, with compatibility handling for older files.

The release includes polar wind-range extrapolation fix `f0a2546`, preventing
impossible speeds from scant polar data. Tablet verification after that fix
includes a Nicholson 35 Holyhead–Lough Foyle route, chart display and a
multi-departure Dun Laoghaire comparison. Earlier Atlantic GRIB-to-Climatology
tests are recorded separately in the chronological tablet audit.

Device acceptance is recorded for a Samsung SM-X210 running Android 15 and
OpenCPN 5.14. Other Android devices and the older OpenCPN 5.10 host are not
qualified by those checks. Remote CI verifies each supported build and package;
it does not replace device UI testing. See `android-tablet-audit.md`,
`android-redesign.md` and `android-plugin-porting-notes.md` for evidence and
lessons for future ports.

The user-facing and OpenCPN package version is `1.19.0`. The alpha workflow
builds ten platform pairs, including Android arm64, and publishes twenty
objects after package review and its existing manual approval hold.
