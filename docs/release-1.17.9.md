# Weather Routing 1.17.9

Weather Routing 1.17.9 makes chart-aware route preparation visible,
responsive and actionable. The routing and chart-safety rules are unchanged:
the Main engine still performs authoritative chart-backed propagation and
final-route validation when those options are enabled.

The progress dialog now reports the exact number of 0.05-degree chart tiles in
each preparation pass. Wider reachability coverage, the scout corridor, its
search-margin mask and endpoint approaches have separate counters and a
determinate progress bar. Display updates are limited to four per second to
avoid measurable preparation overhead. The route table shows **Preparing chart
safety grid** while this work is in progress, and a hidden progress dialog can
still be reopened from **View > Routing progress**.

OpenCPN GUI events are serviced between completed tiles. This keeps the dialog
responsive and lets **Stop all computations** cancel preparation at the next
tile boundary. Individual chart tiles can vary greatly in cost, especially in
detailed CM93 coverage, so the dialog deliberately reports elapsed time and
completed tiles without an unreliable completion-time estimate.

When a positive minimum charted depth is configured, the two endpoint tiles
are now prepared and validated before the wider chart area. A route whose
start or destination lacks suitable depth coverage therefore fails early. The
full progress message identifies the endpoint name, coordinates, configured
minimum and whether the chart reports insufficient depth or cannot prove the
required depth, instead of relying on the truncated route-table State column.

This release includes the configurable shared GRIB timeline cache introduced
in 1.17.8, including independent Main and Quick limits, physical-memory
admission and one shared cache for departure-time optimisation batches.
