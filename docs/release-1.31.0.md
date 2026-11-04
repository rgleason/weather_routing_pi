# xWeatherRouting 1.31.0

Routes being calculated can again show their advancing search fronts and a
provisional explored path on the chart. This works with Quick, Standard,
Alternative and Professional, including their use inside Auto/All. Standard
and Alternative use sampled frontier points rather than invented contour
connections. The existing eye and display-thickness settings control visibility.
Android keeps its existing Plan / Routes / Results / Tools workflow.

The chart labels the provisional path **Search preview (not validated)**. Preview
geometry never becomes a computed result, export, ETA, route comparison or
acceptance decision. Final route validation is unchanged. Hidden calculations
do not generate previews; cancellation, restarting, phase changes and completed
results discard stale preview data.

Workers publish immutable snapshots through a latest-value mailbox. Geometry
is built only when requested, no more than twice per second per solver phase.
Chart repaint requests are combined across workers and limited to one per
second; user visibility actions and final results retain their immediate
refresh. Lines are drawn in batches rather than one graphics call per segment.
Each frame carries at most 256 front points and 256 path points; the UI retains
at most 16 frames. Preview generation makes no extra weather or land queries.
Professional graph/reverse recovery shows the actual prefix being explored;
this does not imply a validated connection to the destination.

For diagnostic comparisons, `WR_LIVE_ROUTE_PREVIEW=0` disables the observer.
This is separate from the existing `WR_ROUTING_PREVIEW` search-quality flag.

Validation results are recorded with the local 1.31 qualification evidence.
The repeatable, opt-in native timing test is
`LiveRoutingPreview.DISABLED_OceanTimingComparison`; enable it explicitly with
GoogleTest's `--gtest_also_run_disabled_tests`. Ordinary tests cover demand,
cadence, geometry bounds, observer failure isolation, and identical selected
legs and search/query counts with previews enabled or disabled for all four
engines.
