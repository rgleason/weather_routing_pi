# xWeatherRouting 1.26.0 alpha — land clearance and chart performance

This release reduces redundant work in chart-aware passage planning. The plugin
services the shared host request queue once per computation timer update, rather
than once for each waiting route. Its existing 16-request / 50 ms service budget
is shared by concurrent routes; a single synchronous host chart operation can
still exceed that time budget.

The companion OpenCPN patch prepares eligible CM93 area rules once for each
safety tile, decodes their unchanged land/drying/depth attributes once, and
retains exact polygon selection at every existing grid point. Missing CM93 cells
no longer rebuild geometry or depth-contour tables when nothing was loaded.
Semantic extraction suppresses repeated global busy-cursor changes inside a
nested thread-local scope; normal rendering keeps its busy-cursor behaviour.
Borrowed rule references are discarded before any fallback query that can change
the prepared chart working set.

## Compatibility and safety

The plugin still uses API 1.21 and discovers chart-safety services dynamically.
On stock OpenCPN, chart-awareness controls remain disabled; ordinary weather
routing and GSHHG land avoidance continue to work. A stock OpenCPN weather route
was completed during qualification. Desktop and Android retain their existing
interfaces. Upgrading from 1.24 or 1.25 preserves customised boats and polars
without an import/replacement prompt.

The measured chart-preparation gains require the companion OpenCPN changes.
Installing only the plugin on an older enhanced host provides the shared queue
budget, but does not replace that host's chart-classification implementation.
Stock OpenCPN does not acquire chart-awareness merely by installing this plugin.

Grid resolution, chart priority, minimum charted depth and missing-depth
handling are retained. Land-clearance enforcement is corrected: a binary
inside/clear adapter result could keep departure access active while following
the coast, and final checks trusted the resulting leg flag. Search, independent
chronological replay and delivered plotting chords now enforce the same local
endpoint bound: twice the clearance, with a 0.5 NM minimum and 2 NM maximum.
Outside that local access area the full configured clearance is required. An
endpoint flag cannot waive an arbitrarily long chord; actual land, exclusions
and depth remain checked inside the local access area. An offshore departure
is checked normally rather than forcibly marked as inside the buffer.

CM93 preparation is optimised, with existing o-charts behaviour retained. It does
not introduce the separately planned S-57/S-63 providers or change xGRIB's
handling of the date line.

## Qualification

The earlier performance qualification, before the clearance correction, uses
fixed NOAA GFS data for the Hawaiian test area, a
Lagoon 560 cruising-catamaran polar, Quinton's saved engine parameters, full
GSHHG, 1.5 m minimum depth and 0.1 NM clearance. Chart awareness remains enforced.
The benchmark is a substitute workload, not a reproduction of Quinton's missing
original forecast and chart configuration, and its endpoints are test inputs.

Sequential unprofiled timings on the local laptop were:

| Calculation | 1.25 / existing core | 1.26 / companion core |
| --- | ---: | ---: |
| Cold passage | 356.692 s | 130.825 s |
| Warm passage | 21.326 s | 21.353 s |
| Cold preparation | 59.447 s | 22.422 s |

The cold passage improved 2.73×; the warm passage showed no measured improvement.
The four runs retain the same arrival, passage duration and routed distance,
and pass final plotted-route safety validation. A comparison of 7,083,734 cells
across 4,214 common tiles found identical class, hazard flags, depth and depth
completeness. Additional tiles reflect changed prefetch timing.

The local report records the individual runs and cell comparison.
It also records 402 plugin unit tests, 116 companion-core tests, six publication
contract tests, 19 real-chart checks, eight unchanged routing-result comparisons,
stock-host routing, concurrent chart-aware departures, and the unchanged native
route workflow when the checker is skipped. Android arm64 is compiled against
the stock OpenCPN core library; packages include manual-import metadata and
verified bundled shoreline data. Physical Android execution was qualified in
the subsequent pass below. Physical Windows execution is not newly qualified;
the platform CI remains required before publication.

The earlier performance binaries and import packages were prepared locally.
Those packages require rebuilding after the clearance correction before alpha
publication. The corrected native build is qualified separately below.
Provider expansion remains separate.

## Initial clearance regression qualification

The clearance fix includes a reproduction of a synthetic 12 NM route which
remains entirely inside a 1 NM buffer and previously passed independent replay.
Both that route and a forged long departure chord must now fail, while local
coastal departure followed by an offshore passage remains valid. Land and depth
checks remain active during local access. The previous benchmark timings above
are not new measurements of the clearance-corrected build.

The complete standalone suite passes all 405 tests, with the five subsequently
added solver/arrival regressions also passing (410 distinct tests). The solver checks
exercise Quick, Standard, Alternative and Professional against a binary
inside/clear provider. The standalone pre-fix reproduction now fails after
6 zero-margin checks and its first full-margin check; previously it accepted
all 12 NM using 48 zero-margin checks and no full-margin checks. A 2 m sample
is rejected with the 3 m constraint, while a 3 m sample passes.

An isolated test using the working OpenCPN host, installed o-charts provider,
Full GSHHG and the current combined GRIB completes the offshore north-Anglesey
control at 1 NM clearance and 3 m minimum depth (28.147 NM, 4 h 39 min 51 s).
Its chronological and final plotted-route validation pass; the private test
leaves the working profile and plugin unchanged. This is a control passage,
not the original coastal route.

The Holyhead local-departure control also completes at 1 NM / 3 m (4.953 NM,
52 min 20 s), retaining ten-minute initial legs and returning to full clearance
offshore. The original Holyhead–Conwy 19 October 12:00 UTC test does not find a
valid route with the selected search settings. A separate 180-degree course
angle test exhausts its graph state allowance. Neither failure is evidence
that no safe passage exists; these runs must not be reported as successful
coastal passage qualification. Their failed results are retained with the
control results and regression logs.

The separate offshore-to-Conwy arrival control also exhausts the graph state
allowance. Independent engine coastal-arrival regressions pass, but a successful
real-chart arrival into this Conwy waypoint is not claimed by this pass.

## Holyhead–Conwy correction and qualification

The initial 1.5-times endpoint bound above was too short for this Conwy
waypoint: an exhaustive chart-grid survey found no fully clear cell within
approximately 1.51 NM. Repeating the survey with the previous OpenCPN core
returned identical section results. This does not implicate the core
performance changes as the cause of that obstruction.

The revised local bound is twice the margin, retaining the hard 2 NM ceiling.
Actual land, minimum depth and exclusions remain checked inside that area;
full clearance applies outside it. Continuous chart-grid traversal also replaces
rounded-endpoint Bresenham traversal in the plugin, so a plotted chord cannot
skip an intersected cell merely because its endpoints round to other cells.
Corner and boundary contacts are included conservatively. New regression tests
for missed land/depth cells fail against the previous traversal implementation
and pass against the correction.

With the confirmed combined GRIB, Nicholson 35 boat, full GSHHG and installed
o-charts, the isolated 19 October 12:00 UTC Holyhead–Conwy test now completes
at 1 NM clearance and 3 m minimum depth when Max Diverted Course is increased
from 120 to 180 degrees. Max Search Angle remains 117 degrees. This setting
allows the route to turn away from the destination to clear the coast; the
plugin does not increase it automatically. Reducing clearance to 0.4 NM alone
did not resolve the original failure.

The corrected single-departure test takes 39.139 seconds wall time and returns
a 7 h 42 min 10 s passage. Independent checks using the unchanged installed
checker find all 145 sections outside the 2 NM endpoint areas clear at
1 NM / 3 m, and all 162 sections of the full passage clear of land and below-
minimum-depth hazards with the margin disabled. Neither audit has unverified
sections. This is bounded endpoint access, not a claim of full 1 NM clearance
right up to either waypoint. It is also not a reproduction of the full GUI
departure-optimisation batch.

The complete corrected standalone suite passes 416 tests from 59 suites.
The corrected library is installed in the working wrapper profile and its
loaded file identity has been verified. A complete GUI batch of 25 hourly
departures returns 20 validated routes and five failed searches: two exhausted
search stages and three resource limits. These failures do not establish that
no safe passage exists. All departures retain the 1 NM clearance, 3 m minimum
depth and bounded endpoint policy. Full-batch independent chart audits have
not been performed; the separate single-route audits above remain the
independent evidence.

A fresh comparison of the corrected native build against the saved 1.25
ordinary-routing fixtures reproduces the seven successful engine, comfort and
departure results. The eighth, impossible-wind case still fails without a
route; its failure text adds the intended Max Diverted Course advice.
Fresh platform bundles and their qualification are still required before
publication. Earlier performance timings are not new measurements of this
clearance-corrected candidate. Auto continues to use Quick, Standard and
Professional; no Alternative fallback has been added.

## Final follow-up evidence

Alternative was tested separately against all five failed GUI departure times,
with cold and warmed chart caches. It recovered none within the bounded test
budgets. One cold run subsequently completed through Professional frontier
recovery; that is not an Alternative recovery. Warmed Alternative searches were
cancelled after approximately 522–535 seconds of engine time. These concurrent
outcome checks are not sequential performance benchmarks. The additional cost
and absence of recovered routes do not justify adding Alternative to Auto.

The final clearance-corrected Hawaiian passage differs from the earlier timing
benchmark: 220.951 NM and 275,819 seconds of passage time, compared with
205.791 NM and 237,296 seconds for 1.25. Both select Standard. Independent
checks using the unchanged checker find all 918 corrected-route sections and
all 835 baseline-route sections clear, with no unknown sections. The cause of
the route-choice difference is not established by that audit. This is a material
route-quality difference, so the earlier 2.73-times cold speedup must not be
presented as a measurement of the final corrected release, or as a guarantee
of unchanged route quality. The first corrected cold run overlapped other
tests and is excluded from the controlled comparison below.

A subsequent sequential, unprofiled comparison with fresh private caches
completed in 346.527 seconds for 1.25 / previous core and 129.414 seconds for
corrected 1.26 / companion core: approximately 2.68 times faster. Forecast,
polar, CM93, full GSHHG, 1.5 m minimum depth and 0.1 NM clearance are fixed.
Both runs reproduce the exact route points independently audited above.
This is one cold run per version, not a statistical benchmark or a general
performance guarantee; the route-quality difference remains as disclosed.

Physical Android acceptance is recorded in
[android-1.26-acceptance.md](android-1.26-acceptance.md). The existing mobile
controls are retained, all five ordinary routing modes reproduce the 1.25
route fingerprints and passage times, and coastal access, departure
optimisation, comfort exploration, cancellation, rotation, setting persistence
and normal plugin reimport were exercised. The tested stock Android host
correctly rejects chart-depth requests; real chart-depth operation there still
requires an enhanced host. The 16 KiB ELF layout is verified statically on a
device with a 4 KiB kernel. Fresh platform CI and publication checks remain
required before releasing the canonical alpha packages.

The macOS CI gettext smoke check remains enforced. If the runner's existing
gettext bottle crashes, its source-rebuild repair now downloads through bounded
GNU mirror fallbacks and checks the archive against Homebrew's recorded
SHA-256 before populating the source cache. Four integrity tests cover valid
caches, corrupt mirror responses, fallback and rejection of unexpected sources.
This addresses dependency-server timeouts encountered during qualification.
