# Automatic adaptive settings — policy 1

Introduced in WeatherRouting 1.36.3 and xWeatherRouting 2.2.4. This switch is
independent of the engine selector: **Auto** chooses engines; **Automatic
adaptive settings** chooses suitable numerical inputs for supported engines.

New passages default to automatic settings, unless the user has explicitly
switched them off. That preference survives restart. Existing saved/imported
routes without the new XML attribute stay manual. Generated passage legs inherit
the selected base configuration; each saved leg retains its own choice.

Manual values remain in the saved route. Calculations use local copies, and
switching automatic settings off restores the same values, including precision
not displayed by the widgets. Opening another route does not change the
remembered new-passage preference. Only managed controls are greyed out. Weather
sources, boat/polars, wind/wave limits, sailing angles, maximum diversion/search
angles, land/depth margins, exclusions, motoring and explicit memory ceilings
remain under user control.

## How the policy decides

A bounded advisory preflight uses the boat's performance, a shoreline detour
sketch, estimated passage time, and inexpensive resident weather samples. It
samples current **vectors**, including speed, direction changes and spatial
variation. Diagnostics express current measurements in m/s. Sampling has a
small work/time allowance and never downloads a tidal overlay, requests an
additional host weather frame or proves that unsampled water has weak currents.
The authoritative weather, land and final-route validation providers remain
unchanged.

* **Quick:** chooses an initial nominal time/heading lattice. Offshore passages
  can start with three- or six-hour steps and wider heading separation. Measured
  rapid currents favour hourly or finer inputs. Unresolved coastal geometry
  keeps the saved inputs first. The engine still shortens steps near endpoints.
  A specific repeated chronological-repair failure can trigger one smaller-step
  retry within the same aggregate work allowance.
* **Quick2:** estimates frontier work using passage duration/VMG and the local
  step map. A separately compiled adaptive variant caps steps to an hour in
  buffered regions with sampled current variability, shear or geometry concerns.
  Lookup is constant time and adds no per-motion weather query. The original
  engine remains available in manual mode.
* **Standard:** starts with its existing native numerical policy. In All mode,
  a finite set of useful hourly/heading alternatives can be compared in suitable
  current regimes. Its endpoint/coastal adaptation remains active.
* **Atlas, Alternative and Professional:** retain their existing engine policies.
  This release does not retune their solvers or the Professional incumbent.
  Legacy/cumulative-Climatology routing also keeps its existing calculation path.

**Auto stops at the first accepted, validated route**, including Quick2. It does
not run the controller's optional quality comparisons after success. **All**
compares every selected engine and retains the earliest accepted arrival among
completed candidates, including admitted numerical alternatives. This is a
bounded comparison, not proof of global optimality. The existing separately
selected comfort-alternatives option remains independent of this switch.

## Resources and recovery

Work counts and actual memory exhaustion are separate conditions. The policy
can increase Quick2's proposal allowance, up to a bounded multiple, when its
work estimate/route complexity warrants it and measured memory headroom allows
it. Coastal/detour evidence remains useful when a complete sketch is unavailable.
It does not raise Quick2's retained-lineage ceiling or user memory settings.

For Standard, a separately compiled recovery variant can add recovery work
(600,000 to 900,000 states) while retaining the original first-phase boundary
at 400,000 states. Increasing the first phase too was counterproductive in
qualification. Arrival planning retains its existing per-departure slices and
cross-departure ledger; the controller cannot borrow later slices.

Shared GRIB and chart caches retain their existing admission and runtime memory
guards. Concurrent-route admission additionally estimates the working set of
the engines actually selected. Optional comparisons stop under falling memory
headroom and retain the accepted incumbent. These estimates are conservative
admission aids, not calibrated bounds on whole-application RSS. Cancellation
and authoritative final acceptance remain effective on every attempt.

## Evidence and limits

The portable controller was checked against 501 cases, including 142 real-GRIB
cases and 16 negative controls. Quick's 501 first-success and 501 comparison runs
produced 426 validated routes in each mode; first-success never continued after
acceptance. Quick2 retained all 461 paired validated tracks from its qualified
prototype. Standard matched the paired completed prototype tracks; one
screening-deadline cancellation completed with an identical track in a longer
paired rerun. Screening timeouts are not evidence that a route is infeasible,
and parallel wall times are not performance benchmarks.

The first port missed Quick2's unresolved-coast allowance fallback. The missing
fallback was restored and all 501 Quick2 cases were rerun; the original failed
trial is retained in the qualification records. Native solver sources under
`vendor/` remain unchanged. The adaptive variants are separate sources under
`src/engine/adaptive/`.

Application qualification covers manual bypass, checkbox precision and remembered
choice after restart, Auto stopping, six-engine All comparison, a real TPXO
current case, bounded arrival planning and an A–B–C–A passage with a manual middle
leg. Policy version and selected effective nominal settings appear in route
diagnostics; per-attempt settings, work and measured preflight values are logged.
General saved-route settings in a run record are not every engine's effective
inputs.

In version 2, advisory samples can read already decoded forecast grids directly.
The classic branch only peeks resident host frames and prepared Climatology.
An explicitly permitted, prepared Climatology fallback can supply advisory
estimates where forecast data are absent or not resident. A busy callback stays
unknown: preflight never waits for the callback lock. This does not mark the
forecast missing/ready or populate the authoritative provider cache. Neither
branch triggers extra host-frame requests to improve the advisory sketch. The policy cannot guarantee that every manual success will
be found, that a first accepted route is fastest, or that every difficult route
will fit the retained-history and work guards.
