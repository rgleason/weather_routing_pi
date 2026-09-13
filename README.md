# Weather Routing integration branch

The optional hardened-OpenCPN planning-provider boundary is documented in
[docs/external_control_provider_preview_b.md](docs/external_control_provider_preview_b.md).
Stock OpenCPN remains supported through the unchanged plug-in API 1.21.

This branch integrates the fully working xWeatherRouting developments back
into the current standard OpenCPN Weather Routing plugin. It retains the
standard plugin identity, settings, polar and GRIB integrations while adding
the modern deterministic routing engine developed and qualified in the
xWeatherRouting branch.

The plugin currently provides:

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

Fresh installations leave both optional chart/depth controls unchecked.
Users of an enhanced OpenCPN host can opt in; explicit choices made by
existing users are preserved. The established `/PlugIns/WeatherRouting`
settings and user-data layout remain unchanged.

## Building

For a clean standalone build against the vendored stock OpenCPN 1.21 API:

```sh
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEATHER_ROUTING_STANDALONE_API=ON \
  -DOCPN_BUILD_TEST=OFF
cmake --build build-release --parallel
cmake --build build-release --target package
```

Keep release packaging in a build directory where tests are disabled. This
prevents test-only GoogleTest libraries from being included by older
packaging infrastructure.

## Testing

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

## Status

The package, library, catalogue and UI use the standard identity:
`weather_routing_pi` / WeatherRouting. Cross-platform artifacts may be built
for validation, but publishing or opening an upstream pull request is a
separate release decision.

## Licence and acknowledgement

The plugin is GPL v3 or later. The original Weather Routing plugin was written
by Sean D'Epagnier and has benefited from many OpenCPN contributors,
translators and testers. This integration preserves that lineage and licence.
