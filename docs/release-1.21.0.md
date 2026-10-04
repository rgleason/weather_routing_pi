# xWeatherRouting 1.21.0 alpha

The Routing Engine list is now **Auto, Quick, Standard, Professional, All (slow)**.
Fresh installations default to Auto. Existing saved selections and per-engine
settings retain their values; older configurations without an engine field
retain Professional for compatibility.

## Auto

Auto tries Quick, then Standard, then Professional, stopping at the first
validated complete route. Search failure, missing required data, or failed
validation permits the next engine to run. Professional is allowed to escalate
up to 400% effort and returns when its existing search policy succeeds; 400%
is a ceiling, not an instruction to spend the full allowance. This temporary
ceiling does not change the user's saved Professional effort.

## All (slow)

All runs Quick, Standard, Alternative and Professional serially with the same
departure and voyage inputs. Each uses its own search and input policies.
Alternative uses Professional's saved environment and safety settings with
its independent sector search. Professional uses the saved effort ceiling.

Only complete routes that pass chronological validation and the applicable
final host land checks compete. The earliest arrival wins; equal arrivals
retain the earlier engine in the order above. A failed engine does not discard
a valid result from another engine. Cancelling stops the whole comparison and
discards any earlier winner. The completion status and headless JSON identify
both the requested mode and the engine that supplied the selected route.

Alternative is internal to All; it is not a separately selectable or saved
plugin mode. Professional2 is not included. All may take substantially longer
because it runs all four searches rather than stopping at the first success.

## Settings and limitations

Independent Quick, Standard and Professional search, shoreline and GRIB-cache
settings are reused. Required immutable shoreline datasets are prepared on the
main thread before workers start. Failure to load an optional dataset rejects
that engine while allowing other prepared engines to run. Auto displays its
fixed 400% Professional ceiling; switching modes preserves the saved effort.

Existing-route analysis, cumulative climatology and forced legacy routing
still require the individually selected Professional engine. Auto and All
support departure optimisation and arrival planning through the existing
scheduler; each arrival-planning probe applies the selected sequence.

Comfort optimisation, candidate comparison tables and the Fastest–Comfort
slider are deferred to a separate development pass. The existing engine
`comfortWeight` field remains inactive. For this release, best means earliest
validated arrival.

## Local validation

The Linux x86_64 plugin and test executable build successfully. All 341 CTest
cases pass, including settings migration, five-mode persistence, sequence
fallback, validation rejection, cancellation, stable ties, effort preservation,
and a real four-solver integration test. A further 22 standalone Alternative
and engine-contract tests pass.

Isolated OpenCPN 5.15 host runs against the packaged binary confirm:

- Auto returns immediately after a successful Quick route.
- With a wave ceiling and wind-only GRIB, Auto falls back from Quick's missing-wave rejection to Standard.
- All runs all four engines with Intermediate shoreline checks and chooses the fastest validated arrival (Professional: 4,291 seconds; Quick: 4,366; Standard: 4,367; Alternative: 5,280).

These are short deterministic Irish Sea fixtures, not long-voyage performance
benchmarks. Test results and package provenance are saved beside the local
build artifact.

The working Linux OpenCPN GUI was also exercised on Holyhead–Mouth of Lough
Foyle with multiple departures, using both chart-aware safety and GSHHG-only
checks. Auto fell back to Standard when Quick rejected unavailable current or
wave samples, the start safety constraint, or its resource allowance.

“Max Swell” currently limits significant height of combined wind waves and
swell. Quick requires usable height samples wherever it applies a nonzero
limit; Standard retains its existing policy permitting missing samples.
A combined GRIB containing waves can still have coastal coverage gaps. Set
Max Swell to zero to explicitly disable that limit. This release does not
change either engine's missing-data policy.

## Alpha publication

The existing CircleCI alpha workflow builds the eleven platform targets with
API 1.21 and API 1.22 headers. All 22 builds must pass before the approval hold
and alpha deployment. The eleven API 1.21 package/XML pairs are published to
`pob220/xweather-routing-alpha-oss`; API 1.22 builds remain compatibility
evidence. Archives contain root `metadata.xml` for manual OpenCPN import.

Debian 12 and Debian 13 x86_64 and Android arm64 import archives are retained
for external testing alongside the other supported targets. Deployment does
not update the beta or master catalogue.
