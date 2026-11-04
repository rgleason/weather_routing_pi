# WeatherRouting 1.29.2 integration

This branch integrates the tested xWeatherRouting 1.29.2 implementation
into WeatherRouting, starting from Rick's master at
`a3cf453108fc866ae86aeed8ff32e410379841d7`. Master currently contains the 1.23
routing baseline, so the integration includes the intervening changes.

- Professional retains the independently validated coastal departure prefix
  when selecting a contour incumbent. The delivered-route and plotting gates
  apply that same bounded allowance. Actual land, drying areas and minimum
  depth remain checked, and full stand-off resumes outside the local prefix.
  This fixes the Holyhead–Conwy regression exposed when the 1.29.1 wave
  correction made a shorter contour candidate eligible. It adds no provider
  queries and preserves search, motoring and polar policies.
- All engines and final validation share wave-height checks. Limited local
  estimates use the maximum height of nearby connected-water cells within
  15 NM, without time extrapolation. Unknown coverage and estimates are
  reported; **Require wave coverage** rejects unknown heights when enabled.
  The selected maximum wave height remains enforced on known heights.
- Auto failures report that no validated route was found with the selected
  settings, followed by the attempted engines' results.
- Modern engines honour **Anchoring**, retaining stationary waiting when
  enabled, with a six-hour total bound. Route reports include waiting
  locations, UTC times and duration; passage time includes the waits.
- Motor-only routing can use headings outside sailing-angle limits when
  the best usable sailing STW is below the configured motoring threshold.
  Other safety checks still apply. Bounded low/high polar wind policies
  distinguish valid numeric zeros from missing measurements.
- Hiding a route is instant and reversible, including computations which
  finish while hidden. **Clear computed results** releases result data while
  preserving the configuration. Departure optimisation supports selected,
  all and manually chosen departure displays.
- Comfort alternatives, optional existing-route chart checks, chart request
  budgets and bounded coastal endpoint handling from 1.24–1.27 are included.
  Optional chart checks require a compatible enhanced host; stock OpenCPN
  retains ordinary routing and GSHHG checks through API 1.21.
- Android retains the existing tablet workspace and touch controls, with
  the shared policies and 16 KB page-aligned ARM64 build support.

The default identity remains `weather_routing_pi` / WeatherRouting. Native
Windows host acceptance supports that identity and adds visibility/clearing
lifecycle checks. Rick's Windows GitHub workflows, import libraries, helper
scripts and prebuilt-gettext macOS fallback are preserved. CircleCI defaults
remain validation-only, using the native identity.
The native macOS CI script also uses the standalone branch's tested gettext
source recovery when a Homebrew `msgfmt` bottle crashes. Reinstalling the
same bottle failed before plugin compilation in the first PR build; healthy
tools still use the normal path.

All 1,064 tracked files under `src`, `include`, `vendor`,
`test`, `data` and `po` match the tested standalone source commit
`d67060ca9183bb204cfba828e1141db71b824391`.
The native WeatherRouting integration was rebuilt with
`WEATHER_ROUTING_XWEATHER_IDENTITY=OFF` against the vendored stock API 1.21
headers: all **482/482 tests passed**. All **10/10 CI helper tests passed**.

Standalone 1.29.2 qualification includes 482 automated checks and isolated
OpenCPN replays of all 13 Holyhead–Conwy departure times with the reported
Nicholson 35, current forecast, motor settings, 0.4 NM margin, 3 m depth
and comfort alternatives. Every delivered and plotted route passes final
chart-safety validation. The final Android ARM64 package passes the 16 KB
page-alignment gate. Quick, Standard and Professional complete the tablet's
light-wind motoring test; all three reject a required-wave case without wave
heights. Results touch controls and chart display were checked. The tested
1.29.2 builds are installed on the working laptop and Samsung SM-X210 tablet;
saved routes, polars and navigation data were checked during installation.

The earlier shared 1.29.1 source passed 22 CircleCI jobs and 7,692 test
executions, including Windows 32-bit/64-bit host checks for the xWeatherRouting
packages. Those are prior-version platform results, not a claim that the
1.29.2 platform matrix has completed.

Release notes: [xWeatherRouting 1.29.2](release-1.29.2.md).
Earlier platform qualification:
https://github.com/pob220/xweather_routing_pi/releases/tag/v1.29.1-alpha.1
