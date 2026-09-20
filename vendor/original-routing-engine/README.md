# Hardened Original contour engine

Local standalone implementation retaining the catalogue v1.15.45.7 contour
intersection, skip-list and merging algorithm (upstream commit
`ed07e567c22e5c65d1711f7a4ded654101912043`). This is a port of that algorithm,
not the Main or Quick solver behind a new name. The pinned catalogue worktree
remains the unchanged benchmark control.

## Build and use

```sh
cmake -S plugins/weather_routing_pi/vendor/original-routing-engine \
      -B /tmp/original-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/original-build -j
ctest --test-dir /tmp/original-build --output-on-failure
```

Link `hardened_original_engine`, include `original_routing/Engine.h`, and call
`original_routing::Engine{}.route(request, environment, options)`. The request,
provider and result types are shared with the adjacent native engine library.
No display, wxWidgets, OpenCPN callbacks, global provider binding or thread-local
provider binding is required. Wind vectors must already be water-relative, as
required by the common motion/validation contract. Providers are supplied by
the caller; this library does not load GRIBs, download data or manipulate the UI.

## Acceptance and work limits

* Enclosing the destination is never arrival. Each candidate is repaired into
  explicitly timed, fixed-heading sailed legs ending at the actual destination.
  Heading and duration are solved together using signed errors. Necessary
  tacks/gybes are two real, individually checked legs with configured penalties.
* Search retains cheap Euler contour expansion. Only candidate paths pay for
  midpoint integration (at most five-minute slices) and independent replay by
  the same `RouteValidator` used by Main/Quick. `Complete` always requires that
  validator to pass. It does not constitute a proof of continuous safety or
  globally optimal ETA; environmental sampling and shoreline fidelity still
  bound what any such validation can establish.
* Requested missing current/wave data follow explicit common policies. Strict
  policies reject missing data; permitted assumptions/fallbacks are reported
  in warnings and source usage. Non-finite samples are not usable weather.
  Set `missingWaves=DisallowWhenConstrained` for fail-closed wave limits: the
  common request type itself defaults to `AllowWithWarning`.
* Cancellation, generated/retained states, geometry work, weather queries,
  candidate replay count, maximum route duration and wall time are bounded.
  Geometry is owned by a request-local arena, including partial merges during
  exception/cancellation unwinding. External provider calls must themselves be
  bounded; synchronous code cannot interrupt an arbitrarily blocking provider.
* An engine instance is stateless and reentrant. Different requests have no
  shared mutable search state. A provider shared between requests must support
  concurrent reads, or its adapter must serialize them. The supplied GSHHG
  provider has a mutex-protected, bounded tile cache.

## Shoreline contract

`GshhgProvider(path)` reads the chosen OpenCPN GSHHG polygon file directly;
full resolution is supported without a fallback to crude resolution. Its base
crossing reader derives from the robust plugin reader at
`d71a9f8d18ceeb0a0c45dcdd533e6052e3775424`, including signed intersections and
grid-line/corner handling. All segment lengths are checked.

Zero margin uses a single indexed crossing test. Nonzero margin searches the
entire buffered segment for coastline edges and land, including small islands
entirely between the old offset lines. Projection scales deliberately
underestimate distance, producing a conservative (slightly wider) stand-off,
particularly at high latitude. Margins from 0 to 100 NM are supported.

The supplied provider enforces the margin on the whole segment, including
departure and destination. It does **not** silently waive a harbour margin.
Consequently endpoints inside the stand-off can make a route infeasible.
The check covers both the integrated sailing path and its exported chord.
GSHHG is a shoreline dataset, not a bathymetric chart; this provider supplies
no depth information. A requested depth limit needs a depth-capable provider.

## Scope and integration

Supports deterministic fastest routing, wind/current/wave providers and
authorized fallbacks, sail/motor performance profiles, transition penalties,
hard environmental limits, land/boundary checks and fuel/motor limits. The
contour search retains only its outward frontier; it is not a multi-objective
fuel optimiser. Other objective kinds are explicitly rejected.

Main-specific graph/reverse/frontier recovery, ensembles, stationary waiting,
host data preparation are not provided. Optional bounded diagnostic isochrone and
route-to-cursor snapshots are enabled with `Options::captureVisualization`.
Those shared request options do not turn Original into Main. In this port
`maximumSearchAngleDegrees` retains Original's meaning (turn relative to the
parent heading), rather than Main's destination-bearing cone. Set 180 for
unrestricted, directly comparable contour trials. Host integration must map
settings deliberately and supply correct provider snapshots.

The plugin offers this component as **Quick** in 1.17.4. The former Quick is
**Standard**, and Main is **Professional**. See `docs/quick-routing.md` for the
adapter's settings, safety and resource policies. The standalone benchmarking
lab retains the unchanged catalogue control and the hardened comparison results. Catalogue copyright/license notices are
retained; this component is GPL-3.0-or-later.
