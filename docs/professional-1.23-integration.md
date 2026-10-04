# Professional integration in xWeatherRouting 1.23

The existing `main` engine ID and Professional selector now use the upgraded
Professional engine. It first tests a memory-admitted Quick candidate, then
uses the broader Professional search and recovery methods to seek a better
validated route. A Quick near miss can supply bounded interim bridges. The
prior standalone Professional implementation is not a selector option or an
Auto/All stage; its recovery solver remains an internal component of the
upgraded engine.

Auto and All retain their existing sequence and reach the upgraded engine
through `main`. Saved experimental `professional2` IDs load as `main` and are
written back as `main`. The integrated Quick controls are shown in the
Professional settings panel. A coastal endpoint buffer exception produces a
warning in the completed route status.

The previous 1.23 source is retained on branch
`archive/xweather-1.23-professional-old` at `9590a1d`. The prior working
binary prefix is also kept when the desktop launcher is switched to the new
installation.

Verification on 2 October 2026:

- Release build with standalone API and xWeatherRouting identity.
- All 362 CTest cases passed.
- Isolated OpenCPN profile, Holyhead to Mouth of Lough Foyle with UKMO GRIB,
  currents, CM93 chart and 5 m depth enforcement: completed and passed final
  validation in 103,997 seconds of passage time. The integrated Quick could
  not satisfy the selected environment, so broader Professional search
  supplied the validated route.

The earlier Professional2 comparisons and Lizard recovery evidence are in
`docs/professional2-comparison-2026-10-02.md` on branch
`experimental/professional2`.
