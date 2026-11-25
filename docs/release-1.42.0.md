# 1.42.0

Weather and land-query providers keep lightweight private configuration templates. Boat polar tables and the search heading list are removed from those templates once, avoiding repeated deep copies on individual land checks and weather-cache misses. The performance provider retains the complete boat data, and each query retains its independent mutable settings and diagnostics. Existing routing engines and safety predicates are unchanged.

With Detect Land enabled, endpoint preflight now runs for individual methods, Auto and All before route search or chart-scout preparation. It uses enforced chart data when chart-aware routing is active, otherwise the passage's selected GSHHG dataset. Errors identify the start or destination, its name, coordinates and data source. Missing data is reported separately from land. Existing minimum-depth endpoint checks remain in force.

The endpoint is checked at zero margin before checking its configured shore-margin buffer. A water endpoint inside that buffer produces a Continue/Cancel warning, preserving existing coastal entry/exit leeway and final route validation. Duplicate warnings within one Compute action are suppressed. Noninteractive runs retain the warning and existing coastal policy; the headless margin-decision override can exercise cancellation.

This is a plugin change and requires no new OpenCPN API. Ordinary routing and GSHHG endpoint checks work on stock OpenCPN. Chart-backed checks use the existing optional enhanced-host service. The optimization adds no workers or cache capacity. Configuration-copy microbenchmarks are not measurements of whole-passage speedup.

Feature-only upgrades from the preceding supported releases retain existing boats, polars and route examples without a bundled-data overwrite prompt.

Help now covers endpoint errors and Continue/Cancel warnings, the remembered calculation-helper and parallel-method options, and chart starting-layer/cache status. The Information page, manual and land-check tooltips were refreshed. Search-angle descriptions distinguish Quick's previous-leg reference from the destination-bearing reference used by the other applicable forward searches.
