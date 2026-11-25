# Weather Routing production

This repository's `master` branch is the **production 1.x line**, currently
**1.44.3**. It retains the established Weather Routing interface and uses the
external GRIB or xGRIB plugin for forecast data, with the separate Climatology
plugin when climatology is requested.

| Line | Current version | Source |
| --- | --- | --- |
| Weather Routing production | **1.44.3** | [pob220/weather_routing_pi — master](https://github.com/pob220/weather_routing_pi/tree/master) |
| xWeatherRouting improved UX development | **2.11.3** | [pob220/xweather_routing_pi — develop](https://github.com/pob220/xweather_routing_pi/tree/develop) |

The 2.x line has the improved planning interface, incorporated GRIB loader and
generator, bundled Climatology, and separate preferences and user data. See its
[published 2.11.1 developer test packages](https://github.com/pob220/xweather_routing_pi/releases/tag/developer-test-2.11.1-20261124).

The production 1.44.2 change is present here; integration into Rick's `master`
is tracked in [PR #740](https://github.com/rgleason/weather_routing_pi/pull/740).
The version declared here does not mean the plugin catalogue has already updated.

1.44.3 fixes downloading the optional High and Full GSHHG datasets on a fresh profile.
The plugin includes Crude, Low and Intermediate for offline installation.

Both lines include Meridian as an optional routing method. Ordinary GSHHG
shoreline routing works on stock OpenCPN; chart-backed depth and hazard checks
require a compatible chart-safety host. See [Meridian](docs/MERIDIAN.md),
[routing settings](docs/quick-routing.md) and the retained
[integration architecture](docs/integration-1.29.2.md).

The optional hardened-OpenCPN planning-provider boundary is documented in
[docs/external_control_provider_preview_b.md](docs/external_control_provider_preview_b.md).
Stock OpenCPN remains supported through the unchanged plug-in API 1.21.

The production branch uses the standard plugin identity, settings, polar and
weather-provider integrations, with the modern routing methods developed and
qualified across the production and development lines.

The plugin currently provides:

- consistent wave-height checks and coverage warnings across routing engines,
  optional required wave coverage, and explicit anchoring waits with locations
  and durations in the route report;
- threshold-based motoring when sailing performance is insufficient, bounded
  polar wind policies, reversible route visibility, explicit result clearing
  and selected/all/manual departure display modes;
- a read-only **Check this route** action in the OpenCPN route context menu
  (v1.25). It reports land crossings, drying areas, charted hazards, land
  clearance, optional minimum charted depth and unavailable chart/depth data.
  Findings appear in a per-leg report and as temporary chart markers. Click
  a marker to select its report entry, or select an entry and choose
  **Show selected finding**. **Recheck** uses the latest route and chart state;
  closing the report removes its markers. Cancellation leaves an explicitly
  partial report. The checked route and its waypoints are never changed;
- deterministic adaptive forward isochrones, reverse recovery and
  time-dependent graph fallback;
- preservation of useful suboptimal lineages for difficult coastal routes;
- departure-time optimisation with independently isolated workers;
- planned-arrival routing, including determination of the required departure
  time;
- UTC routing internally with optional IANA local-time display in the UI;
- dense independent route validation and standard GSHHS land checks;
- optional enhanced chart-backed hazard checks when the OpenCPN host exposes
  the dynamically detected experimental service.

The same binary loads on an unmodified stock OpenCPN host. Stock OpenCPN does
not expose the optional chart-backed service, so Weather Routing disables
those two controls and continues to use the standard GSHHS checks.
There is no direct enhanced-core symbol dependency.

Route checking also requires the compatible chart-safety host. It checks the
rhumb-line legs between the route's existing waypoints using authoritative
chart queries, independently of the routing engines. Depth is at chart datum;
entering zero disables depth checks, and the report states this explicitly.
Android retains the established Plan / Routes / Results / Tools workspace.
Tools → Route management → Check this route opens the same read-only checker
through a touch route picker, with stacked settings, scrollable findings and
persistent Close/Back actions. Stock OpenCPN Android lacks the optional chart
safety service and reports that checking is unavailable; ordinary routing and
its GSHHG land checks remain available.
The checker does not evaluate traffic rules, restricted-area conditions,
bridge clearance or tide height. Reports become outdated when the route,
chart selection or chart identity changes. Version 1.27 pairs the existing
plugin with the optional geometry-based core improvements described in its
release notes. Real S-63 cells still require separate qualification; this
release does not claim a new S-63 provider integration.

Fresh installations leave both optional chart/depth controls unchecked.
Users of an enhanced OpenCPN host can opt in; explicit choices made by
existing users are preserved. The established `/PlugIns/WeatherRouting`
settings and user-data layout remain unchanged. Upgrading from v1.24 preserves
customized boat and polar files without prompting to replace them.

## Building

For a clean standalone build against the vendored stock OpenCPN 1.21 API:

```sh
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEATHER_ROUTING_STANDALONE_API=ON \
  -DWEATHER_ROUTING_XWEATHER_IDENTITY=OFF \
  -DOCPN_BUILD_TEST=OFF
cmake --build build-release --parallel
cmake --build build-release --target package
```

For the improved UX development frontend, use the 2.x source linked above.
Changing the plugin identity alone does not select that frontend.

Keep release packaging in a build directory where tests are disabled. This
prevents test-only GoogleTest libraries from being included by older
packaging infrastructure.

## Testing

For real-GRIB smoke tests in a separate OpenCPN profile, see the
[headless testing kit](tools/headless-testing/). It includes a Python runner,
timing extractor, example scenario and setup instructions.

```sh
cmake -S . -B build-test -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEATHER_ROUTING_STANDALONE_API=ON \
  -DOCPN_BUILD_TEST=ON
cmake --build build-test --parallel
ctest --test-dir build-test --output-on-failure --parallel
```

The native engine, deterministic resource policies, planned-arrival planner,
timezone lifecycle, route validation, chart-cache data structures and
supporting geometry are covered by the test suite. Stock-host compatibility
must also be checked by loading the clean package in an unmodified OpenCPN
5.14 or later installation.

Further architecture and validation details are in
[`docs/modern_native_engine.md`](docs/modern_native_engine.md) and
[`docs/chart_safety_cache.md`](docs/chart_safety_cache.md).

For the wxWidgets 3.2 and 3.3.4 build variants and matching Windows x64
packages, see [`docs/release-1.35.0.md`](docs/release-1.35.0.md).

## Status

The package, library, catalogue and UI use the standard identity:
`weather_routing_pi` / WeatherRouting. Cross-platform artifacts may be built
for validation, but publishing or opening an upstream pull request is a
separate release decision.

## Licence and acknowledgement

The plugin is GPL v3 or later. The original Weather Routing plugin was written
by Sean D'Epagnier and has benefited from many OpenCPN contributors,
translators and testers. This integration preserves that lineage and licence.
