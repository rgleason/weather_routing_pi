# xWeatherRouting 1.33.0

Atlas adds a sixth independent routing strategy. It combines continuous heading/time connection solves, adaptive spatial decisions, multiple arrival histories and coarse time-dependent weather/current guidance. Sailing, waiting and motoring use the shared vessel/environment providers and independent chronological and safety validator. Atlas does not invoke the other engines.

The Advanced selector and Auto/All membership rows use this order:
**Quick → Quick2 → Atlas → Standard → Alternative → Professional.**
Atlas is initially unchecked in Auto and All. Existing engine IDs, saved membership bits, numerical settings and default engine choices are preserved. Auto retains the existing Quick2 provisional-result policy; All compares validated arrivals from every selected engine.

Completed-route and departure-optimisation results name the winning engine, including Atlas. The selector calls it **Atlas**, without a parenthetical label. The release retains the later 1.32 Professional recovery/arrival-horizon fixes, partial-result inspection and Android touch workflow and Qt compatibility fixes.

The [mathematical formulation](../research/sixth/mathematics.md), [literature review](../research/sixth/literature.md), [research measurements](../research/sixth/results.md) and [standalone API/build guide](atlas-engine.md) document the method and its limits. The broad research comparison used frozen baseline `1fa62c7`, rather than the later Professional policy carried into this release. Atlas is a bounded approximation: completeness and global optimality are not established.

Local qualification includes 544 enabled desktop tests, ten Android CPU cases (eight validated routes and two expected rejections), native real-GRIB Atlas/Auto/All replays, and tablet GUI/layout checks. A further 58 targeted regressions and four native replays check the winner-name correction, including Atlas winning All. Platform CI provides the release-package qualification and Atlas physical regression cases.

Each platform tarball contains a root-level `metadata.xml` matching its separate XML file, for manual import using OpenCPN's built-in Plugin Manager. Choose the package matching the host operating system, architecture and distribution version. Minimum plugin API remains 1.21. macOS is ARM64; Windows x86 and x64 packages are separate. Android arm64 ELF layout supports 16 KiB pages. Ordinary GSHHG routing works on stock OpenCPN; optional chart-backed safety requires a compatible host.

Bundled-data upgrade policy retains existing boats, polars and personal route settings. No default-engine change is made in this release.
