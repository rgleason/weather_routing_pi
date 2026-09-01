# Weather Routing Engine

`weather-routing-engine` is a deterministic, headless C++20 library for
time-dependent sailing-route calculation.

The core contains no GUI, chart-database, GRIB-decoder, OpenCPN, Qt, wxWidgets,
or application-singleton dependency. Applications supply immutable weather,
current, climatology and chart-hazard providers through the public interfaces
in `include/supercpn/weather_routing`.

The solver combines:

- adaptive forward isochrones with bounded alternative-lineage retention;
- a bounded, fine-grained coastal departure phase which preserves safe egress
  lineages before returning to the normal offshore cadence;
- destination-side recovery with chronological forward replay;
- validated frontier recovery using a focused multi-label A*/Dijkstra search;
- progressively widened global graph fallback;
- independent chronological route validation;
- bounded waiting for temporary environmental gates;
- deterministic cumulative 100%, 150%, 200% and 400% effort tiers; and
- deterministic ensemble and concurrent-request behaviour;
- arrival-anchored reverse timing projection with adaptive bracketing; and
- mandatory chronological forward solving and validation for every proposed
  departure in an arrival plan.

## Embedding

The supported source-vendoring layout is:

```text
vendor/weather-routing-engine/
```

A parent project can either use:

```cmake
add_subdirectory(vendor/weather-routing-engine)
target_link_libraries(my_target PRIVATE
  weather_routing_engine::weather_routing_engine)
```

or install and consume the generated CMake package.

Application-specific adapters, serialization formats and UI code deliberately
remain outside this repository.

## Standalone build

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Versioning

The package follows semantic versioning. Consumers pin a Git commit and record
that commit in `vendor/weather-routing-engine.version`.

## Licence

Apache License 2.0. This is compatible with both the Apache-licensed SuperCPN
application and GPL applications which vendor the engine.
