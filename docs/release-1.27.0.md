# xWeatherRouting 1.27.0 — optional geometry-based chart awareness

Version 1.27 retains the corrected 1.26 routing engines, bounded coastal access,
land-margin validation and desktop/mobile controls. The performance and
isolated-danger changes are supplied by the companion OpenCPN core; the plugin
continues to load and route on stock OpenCPN.

## Stock OpenCPN remains supported

The plugin advertises the unchanged API 1.21 and supports a clean standalone
build against its vendored stock headers. Enhanced chart-safety functions are
resolved dynamically and are never required imports.

On a stock host, ordinary weather routing and plugin-managed GSHHG shoreline
checks remain available. Chart-awareness and chart-depth controls are disabled,
and the chart-based route checker reports that the host service is unavailable.
Saved chart-awareness preferences do not force chart queries on a stock host.
Ordinary routes with no minimum-depth requirement use GSHHG. A saved route that
explicitly requires minimum charted depth is refused with an explanation until
that requirement is removed or a capable host is used; it is never silently
downgraded to shoreline-only safety. These guards are shared by desktop and
Android; the existing mobile workspace is retained.

No user-data migration or reset is required. Existing boats, polars, routing
settings and optional chart-safety choices retain the 1.26 data layout. The
feature-only upgrade recognises 1.24–1.26's unchanged bundled data and advances
the version marker without offering to overwrite boats, polars or route examples.

## Companion-core changes

The qualified prototype is core commit
`9425ab3d25f009b2fed32209f8e66edb824d0d34`. Its whole-tile shortcut is enabled by
`OCPN_CHART_SAFETY_GEOMETRY_PROOF=1` in the local working launcher.

- Copy native safety geometry into immutable query data, with local sounding
  indexes, rather than repeatedly consulting display eligibility.
- Reject distant line/area objects using authoritative bounds before geometry
  extraction. Points and unresolved bounds still receive detailed checks.
- Reuse proven whole-area coverage for individual cells. A uniform whole-tile
  depth proof additionally needs complete highest-detail CM93 chart coverage.
- Explicitly consider `UWTROC`, `WRECKS` and `OBSTRN` as point, line and area
  objects. Any such danger prevents the whole-tile shortcut. Missing danger
  depth remains unknown and cannot borrow surrounding deep-water evidence.
- Retain small shallows, reefs, drying areas, coverage holes and local sounding
  minima. Invalid geometry cannot certify water as clear. Cache identities
  invalidate earlier derived data.

This is the first CM93 geometry-proof stage, with shared native S-57 query
improvements. It is not a completed multilevel hierarchy or a new S-63 provider
integration. Real licensed S-63 cells remain a separate qualification task.

## Existing prototype qualification

Before the version bump, the unchanged corrected 1.26 plugin and companion core
passed 151 deterministic core tests, 16 native fixtures and 10 real-chart checks.
The geometry tests include 70,000 independent comparisons and ASan/UBSan checks.
Shortcut on/off cache records matched across 7,198,042 common cells.

One fixed Hawaiian workload took 130.123 seconds on the published preview,
104.512 seconds with strengthened detailed queries, 91.338 seconds with the
whole-tile shortcut, and 22.980 seconds with its persistent cache reused. The
corrected runs had identical aggregate arrival and distance. Safety corrections
changed the published baseline route, so these are observed same-input timings,
not identical-path timing or a general performance guarantee.

The 1.27 native build passes all 420 plugin tests, loads and completes a full
GSHHG route on stock OpenCPN 5.14, and completes the enhanced-host Hawaiian
route with the same aggregate arrival and distance as the qualified prototype.
Customised boats and polars survive the upgrade. Physical Android checks are
recorded in [android-1.27-acceptance.md](android-1.27-acceptance.md); the existing
mobile controls and stock-host capability guards are retained. The full
platform/API matrix and review of its exact packages still gate publication.
