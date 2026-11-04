# WeatherRouting 1.32.0

Quick2 adds an independent bounded CPU frontier search using the shared motion kernel and independent chronological validation. Auto and All have saved per-engine membership controls. Auto retains a successful Quick2 route while Standard searches independently with its normal allowance, and compares later successful candidates. All compares every selected engine. No GPU is required.

Professional preserves validated incumbents while comparing unrestricted and focused native recovery. Arrival-horizon checks and continuation-aware pruning cover tide, sailing profile and minimum motor-run state. Existing motion, land, depth and other configured safety checks remain mandatory. The detailed sequence and its resource limits are documented in [the Professional policy](../vendor/weather-routing-engine/PROFESSIONAL-POLICY.md); bounded search does not guarantee the globally optimal route or a solution for every feasible passage.

Failed All calculations can retain a bounded set of independently replayed partial routes. Best partial route shows each prefix's actual endpoint, endpoint time and remaining straight-line distance. A partial preview remains incomplete, follows the route visibility control and never supplies a destination ETA or enables completed-route exports. Clearing computed results removes the prefixes while preserving route settings. Unknown wave heights remain unknown.

The established Android Plan / Routes / Results / Tools workflow is retained. Its touch controls expose engine selection, Auto/All membership and Best partial route. Tablet testing found and corrected a Qt keyword collision which otherwise made the partial archive capacity zero. A dedicated regression reproduces that failure and verifies the correction.

This release includes 1.31's bounded live chart progress for visible computations. Search-front and provisional-path previews are labelled unvalidated and do not change final acceptance. Hidden computations stay hidden. Preview work is bounded and requested only for visible routes.

Release packages retain minimum plugin API 1.21 and are qualified with both API 1.21 and 1.22 headers. Each manual-import tarball contains one root metadata.xml matching the external XML; publication rewrites its tarball URL to the verified public download. The separately installable xWeatherRouting identity and mergeable native WeatherRouting identity are built independently.
