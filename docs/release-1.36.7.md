# Historical cyclone assessment and responsiveness

Weather Routing 1.36.7 and xWeatherRouting 2.3.4 make historical cyclone
assessment an explicit report action. Opening or refreshing route details no
longer invokes a cyclone provider or scans every completed route.

The report defaults to selected completed routes; all completed routes can be
chosen explicitly. Its seasonal window is 1–365 days (default 30). Analysis
uses immutable copies of positions/timestamps on a serialized worker. Progress
updates between segments, and Cancel/closing the report stops further queries
without holding route or UI locks. An outstanding legacy callback must finish
before provider unload or application shutdown; no thread is forcibly killed.

Completed results are reused for identical geometry, timestamps, seasonal
window and provider revision. Cache memory and entry count are bounded.
Provider/filter changes invalidate displayed and cached results. Providers
must advertise safe background cyclone queries; older providers show a clear
update-required message for this optional report. Calculations with cyclone
avoidance selected also require corrected queries (Climatology 2.0.3 or the
incorporated v2 provider); older providers fail explicitly without clearing
the avoidance setting. If data disappear during a calculation, unchecked
segments are rejected rather than silently passing cyclone avoidance. Routing
avoidance remains controlled by its existing setting and is unaffected by
cancelling a report.
The incorporated v2 provider supports background assessment directly.
GRIB-only routes requesting cyclone avoidance also prime the historical track
data before starting workers; selecting forecast wind does not skip this setup.

Reports distinguish not assessed, running, completed, cancelled and unavailable.
Negative provider replies do not count as crossings. Results describe detected
historical track crossings rather than asserting that a passage is safe from
cyclones. A missing switch break in the previous zero-crossing report has been
removed with the old synchronous aggregation path.

The companion external Climatology patch corrects separated parallel/point
intersections, dateline and negative-coordinate binning. It publishes a
value-owned immutable filtered index atomically, preserving the existing
wind/pressure/date/ENSO/storm-class filters. Rendering no longer inserts empty
cache entries. The pointer ABI is unchanged; optional capability fields and a
filter-revision notification are additive. The Climatology patch is a separate
plugin change and must be packaged with its own release before distributing it.

Qualification includes provider failure/cancellation/cache tests, geometry and
concurrent index-publication tests, private-profile GUI checks and real GRIB
route comparisons. Results and platform limitations are recorded under
`output/cyclone-fix-20261117` in the local workspace. No publication is performed
by this change.
