# Android 1.27 acceptance — 7 October 2026

The existing Android Plan, Routes, Results and Tools workspace is retained.
Version 1.27 includes the shared corrected 1.26 routing code and the feature-only
upgrade guard for unchanged boats and polars. The new geometry and isolated-danger
query improvements live in the optional companion OpenCPN core, not in an Android
renderer dependency. Installing 1.27 on stock OpenCPN does not add chart-awareness.

## Build and host compatibility

The local arm64 package builds against the stock OpenCPN Android link library,
NDK 26.1.10909125 and API 1.21. Bundled shoreline integrity and ELF LOAD, RELRO
and offset checks pass with 16 KiB layout. The attached Samsung SM-X210 runs
Android 15 and OpenCPN development app 5.14.1 with a 4 KiB kernel. Execution on
a 16 KiB kernel is not claimed.

The 1.27 library cold-loads successfully on this unmodified Android host.
Chart-awareness remains unavailable. Saved chart-awareness preferences set to
on do not enable chart queries on this host. An explicit 3 m minimum-depth
requirement fails with an unsupported-service explanation; it is never
silently replaced by shoreline-only checking.

## Routing regression

On the same tablet, host, current xGRIB library, Nicholson 35 boat, full GSHHG,
0.1 NM clearance and checked-in uniform-wind forecast, all five ordinary modes
match the installed 1.26 exactly in selected engine, passage time and route
fingerprint. The forecast is a deterministic synthetic fixture, not an
operational forecast or a recreation of Quinton's passage.

| Mode | Selected engine | Passage seconds | Route fingerprint |
| --- | --- | ---: | --- |
| Quick | Quick | 6462 | `4b73ef80af7c08c6` |
| Standard | Standard | 5854 | `95babb84474e59c7` |
| Professional | Professional | 5973 | `4e8e49dd28966170` |
| Auto | Quick | 6462 | `4b73ef80af7c08c6` |
| All | Standard | 5854 | `95babb84474e59c7` |

All five pass final validation. An impossible-wind control fails. With a 1 NM
land margin, the Conwy Bay coastal control retains Quick's explicit departure
buffer refusal and Professional's validated local escape: 4947 seconds, four
legs and fingerprint `6f4a205b4c87fa68`. Actual land and minimum-depth controls
remain active; this is a GSHHG coastal test, not chart-depth qualification.

Departure optimisation computed all five candidates: the three covered by the
forecast completed and the two before forecast coverage failed explicitly with
`wind_forecast_required`. Computation returned to an idle, usable mobile
workspace. These checks use the locally built 1.27 library; the exact CI package
will be imported and checked again before publication.

## Release gate and preservation

The user's preferences, navigation database, original 749 routing files,
installation records and other plugin library hashes were backed up before
testing. Private backups and logs remain local and are not release assets.
The original profile will be restored and verified, retaining 1.27 after normal
plugin import. The complete 22-job platform/API matrix, canonical package
review, import of the CI Android package and final profile-preservation check
remain publication gates. Real licensed S-63 chart-depth tests and an enhanced
Android chart-safety host are outside this stock-host acceptance.
