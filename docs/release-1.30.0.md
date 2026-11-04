# WeatherRouting 1.30.0

Quick uses the route's configured nominal timestep, including when Professional runs Quick internally to find a contour candidate. The internal search no longer replaces that value with a three-hour offshore setting. Existing reductions near departure and destination remain active; Standard's timestep policy is unchanged.

Professional can reconnect a chronologically replayed prefix to a nearby later contour vertex when changing wind or tides prevent reconstruction of the next proposed vertex. Recovery is bounded to four rejoins per candidate, at most three proposed vertices ahead and within 30 NM. It cannot skip a proposed stationary wait. Every delivered leg still passes independent chronological validation and the host's final plotted-route safety checks.

This release includes the unpublished 1.29.3 changes: fixed endpoint areas which cannot follow the boat along the coast, corrected Flatpak ABI labels and data installation, Ubuntu 22.04 wx 3.2 identification, and package ABI validation throughout CI. Minimum host API remains 1.21, with CI also checking API 1.22 headers. Existing user boats, polars and route settings are preserved during the feature-only upgrade.

Local qualification passed 484 automated routing/contract tests, all 17 historical Professional cases, and all 13 departures in the updated Holyhead–Conwy forecast. The current batch exercised seven Quick and six Professional completions. All 30 delivered routes passed final chart safety checks. The historical Conwy reconstruction regression is covered by a successful replay. Desktop and Android installations of the completed source were verified with personal navigation and routing data preserved.

See [the 1.30.0 qualification report](validation-quick-professional-1.30.0.md) for the tested cases and limits. Platform packages are published only after the final build matrices, package metadata checks and host import tests pass.
