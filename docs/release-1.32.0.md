# xWeatherRouting 1.32.0

Quick2 adds an independent CPU frontier search alongside Quick, Standard,
Alternative and Professional. It shares the existing vessel physics and safety
providers and independently replays each delivered route. It needs neither CUDA
nor a GPU. No OpenCPN core changes are required.

Basic settings offer Auto and All (slow), with the existing option to search
fastest/comfort alternatives. Individual engine selection and separate Auto/All
membership are on Advanced. Their common order is Quick, Quick2, Standard,
Alternative, Professional. Auto enables all except Alternative by default; All
enables all five. Unchecked engines are skipped. Empty selections produce a
configuration error instead of silently enabling an engine. Existing individual
engine selections and numerical preferences remain usable.

Auto stops its engine fallback sequence when Quick returns an accepted route.
If Quick fails and Quick2 succeeds, it stores Quick2's candidate and still runs
selected Standard independently, with its full normal allowance and fresh
providers/request. Quick2 neither seeds nor prunes Standard and spends none of
its allowance. When Standard succeeds, Auto keeps the earlier validated arrival.
If it fails, further enabled engines may try; the Quick2 candidate remains
available. A complete-operation cancellation retains the existing cancellation
semantics. All attempts every selected engine and compares validated arrivals.
There is no distance threshold or parallel-engine scheduling in this release.

The Fastest / Comfort slider, comparison table, missing-weather treatment and
existing severity model are preserved. Auto can retain both Quick2 and Standard
for comparison and optional exploration. Quick2's directed comfort search uses
the shared scorer, rather than the experiment's illustrative scoring policy.
The extra-search allowance remains separate from ordinary engine allowances
and shared across retained candidates. Its numerical controls are on Advanced.

Quick2's approximate bounded search can miss a route or return a detour; this is
why its success cannot skip Standard in Auto. It receives the original chart
provider during propagation, chronological replay and final plotting checks.
Coarse geographic search data never overrides authoritative chart/depth checks.
Tests establish behaviour under the supplied charts/settings, not navigational
safety of future forecasts or routes.

The 1.32 feature upgrade retains the existing bundled boats, polars and examples
and does not offer to overwrite personal routing data merely for a version bump.
See the local qualification report for exact binaries, scenarios and results.
