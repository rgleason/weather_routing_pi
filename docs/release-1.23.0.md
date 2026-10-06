# xWeatherRouting 1.23.0 alpha

This release combines the improved Professional routing engine and retained
Fastest / Comfort comparison with the established Android touch interface.
Android starts from its tested baseline; Plan, Routes, Results, Tools, file
selection, chart actions and saved routing workflows retain their controls.

The engine picker offers Auto, Quick, Standard, Professional and All (slow).
Fresh last-used profiles and bundled examples default to Auto. Existing saved
engine choices and per-engine settings remain intact. Professional includes
its integrated Quick controls in the existing Engine settings section.

All retains validated candidates from four engines and can run an additional
bounded comfort search. Results → Fastest / Comfort compares passage time,
discomfort exposure, weather coverage and the worst known leg. Wind-only
ranking is enabled by default and remembered. Selecting a retained candidate
uses its validated route geometry without starting another search. Android
provides touch candidate/sort pickers and a scrolling comparison sheet.

Local validation passed all 363 CTest cases and five publication-contract
tests. On a Samsung SM-X210 running Android 15 and OpenCPN development 5.14,
Auto, All and Professional completed independent short climatology routes.
Candidate selection, chart display and portrait/landscape presentation were
checked. Six original configurations and saved plugin settings were restored;
the supporting routing files match the pre-update backup.

The unchanged installed xGRIB library crashes on toolbar opening, matching a
pre-existing crash. Device GRIB handoff is therefore unverified; the device
routing checks used climatology. The short routes validate integration and do
not establish long-passage performance or a comfort trade-off.

The alpha release workflow builds eleven platform targets against API 1.21
and API 1.22 headers. All 22 builds must pass before the deployment hold.
Eleven API 1.21 package/XML pairs are published to
`pob220/xweather-routing-alpha-oss`; API 1.22 builds provide compatibility
evidence. Archives embed root `metadata.xml` for manual OpenCPN import.

The historical `macos-universal` job produces the existing native ARM64 Mac
target. Android and Flatpak jobs compile and verify packages; regression
suites run on the native Linux, Windows and Mac targets. This alpha release
does not publish to the beta or master catalogue.
