# xWeatherRouting 1.25.0 alpha

Existing OpenCPN routes can now be checked without changing their waypoints or
creating a weather-routing configuration. Choose **Check this route** from the
native route menu. The report checks whole rhumb-line legs for land, drying
areas, charted hazards, land clearance and optional minimum charted depth.
Unavailable chart/depth data remains explicitly unverified. Depth is at chart
datum; zero disables depth checks. Traffic rules, restricted-area conditions,
bridge clearance and tide height are outside this check.

Findings appear by leg in the report and as temporary chart markers. Select a
finding or click its marker to inspect it. Recheck uses the current route and
charts; route/chart changes invalidate an existing report. Cancel leaves a
partial report, and Close removes markers. Checking is optional: creating and
editing native routes and opening Weather Route Analysis retain their workflow.

Android carries forward the published 1.24 touch interface and comfort-search
controls. Tools > Route management > Check this route supplies a touch route
picker, stacked fields, scrolling details and persistent Close/Android Back.
The checker shares the desktop backend and optional-service detection. Stock
OpenCPN Android lacks that chart-safety service and reports checking unavailable;
ordinary weather routing and GSHHG land checks remain available. This release
does not add the separate S-57/S-63 provider work planned for 1.26.

Physical Android tests passed route-picker cancellation, read-only native-route
preservation, explicit unavailable-host reporting, Recheck, Close/Back and
portrait/landscape resizing. The report refits after delayed host canvas
changes, independently of check progress. Positive chart findings on Android
require a host implementing the optional chart-safety service and remain
unverified on the stock tablet host.

Local desktop/Android builds passed, along with 402 native tests and five
publication-contract tests. All 19 real-chart integration cases passed. Eight
routing comparisons against 1.24 retained identical route results, ETA, comfort
metrics and failure behavior. Standard and OpenGL report/marker interactions
were checked on private displays. Six timing examples preserved their routes;
a 47 NM o-charts example with 2 m depth and 0.4 NM clearance took about 4 seconds
initially and 3 seconds on recheck. Timing depends on route, charts and cache.

Upgrading from 1.24 preserves customized boats and polars without replacement
prompts. Alpha publication uses eleven API 1.21 package/XML pairs after the
22-target platform/API 1.21/1.22 matrix succeeds. Packages include root
metadata.xml for manual import. Native Linux, Windows and Mac jobs run the
regression suites; Android and Flatpak jobs compile and verify archives. The
historical Mac job produces native ARM64. Publication remains confined to Alpha.
