# WeatherRouting 1.29.1 integration

This branch integrates the published xWeatherRouting 1.29.1 implementation
into WeatherRouting, starting from Rick's master at
`a3cf453108fc866ae86aeed8ff32e410379841d7`. Master currently contains the 1.23
routing baseline, so the integration includes the intervening changes.

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

All 1,064 files under `src`, `include`, `vendor`, `test`, `data` and `po` match
the published source commit `8a3ef56c635972c52cffc5f65c84e3737561a663`.
Its platform qualification passed 22 CircleCI jobs and 7,692 test executions,
including native Windows 32-bit/64-bit host checks for the xWeatherRouting
packages. Android's local ARM64 build was also tested on the Samsung tablet.
These results qualify the shared source. The native WeatherRouting integration
was separately built with `WEATHER_ROUTING_XWEATHER_IDENTITY=OFF` against the
vendored stock API 1.21 headers: all 480 tests passed. All 10 CI helper tests,
build-script syntax checks and local CircleCI YAML checks also passed.

Published source and qualification evidence:
https://github.com/pob220/xweather_routing_pi/releases/tag/v1.29.1-alpha.1
