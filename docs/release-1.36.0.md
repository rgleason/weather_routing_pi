# WeatherRouting / xWeatherRouting 1.36.0 — development

This first increment improves the information available after Compute. It is
development work, not a published catalogue release.

Select a route and open **View > Details and report**, or use the same action
from the route's context menu. The report shows retained setup checks, errors,
warnings, corrective steps and technical details. **Copy diagnostic report**
copies the displayed information locally; nothing is uploaded. Review the
included positions, filenames and technical details before sharing it.

## Implemented

- A shared diagnostic record feeds route status, the selected-route report and
  additive structured diagnostics in headless JSON. Errors raised before a
  worker starts are retained as well.
- A disabled GRIB source is distinguished from an unavailable climatology
  provider. The user is directed to Basic > Data Source, even when wind arrows
  remain visible on the chart.
- Wind bounds from the departure frame are checked independently of permitted
  climatology fallback. Known endpoint coverage gaps block a route without a
  usable permitted fallback; otherwise they warn. A permitted fallback is not
  presented as proof that it was used.
- Single-engine shoreline endpoint findings identify coordinates and the
  selected GSHHG model. They remain available alongside independent departure
  wind-grid problems. Auto/All retains per-engine findings and effective
  shoreline descriptions without changing the selected resolutions.
- All existing native outcome statuses, preflight actions and warning codes
  have user-facing descriptions. Required depth is identified as depth.
  Search exhaustion is distinguished from proving that a passage is impossible.
- Complete routes show a warning count. Actual delivered-route intervals identify
  climatology wind, climatology/alternative current, assumed zero current,
  unknown/estimated wave heights and stationary waits. A deliberately disabled
  current model is distinct from missing current data.
- Unknown waves do not mean calm seas. The report states that a configured
  wave-height limit was unchecked in missing sections; it does not make that
  claim when no wave-height limit was configured.
- Existing polar-estimate and endpoint-buffer warnings remain available.
  Polar parser warnings survive successful boat loading; generated-data save
  failures become warnings. Last-valid weather reuse is explicitly identified
  as unavailable in the current native routing mode.
- Auto/All preserves all attempted engine outcomes even when one succeeds.
  A candidate reporting Complete without passing validation is recorded as a
  validation failure. Retained alternatives carry the operation's attempt history.
- Weather service no-reply, missing-frame and incomplete-wind-vector messages
  are retained separately from missing spatial coverage. Repeated provider
  events are grouped and counted.
- Cancellation is described separately from an internal failure. Arrival
  planning retains candidate failure details and distinguishes invalid settings
  and an exhausted evaluation allowance from an ordinary failed schedule.
- The adapter no longer overwrites the shared description on Android or appends
  speculative detour-angle advice to unrelated failures. Unexpected worker
  exceptions retain a diagnostic rather than escaping the worker.
- Diagnostic state resets with a new calculation. Route-specific source-use
  warnings are regenerated when choosing another retained candidate.
- Desktop configuration has one bottom-left OK button shared by Basic and
  Advanced. Android retains its existing touch-oriented Done header.

## Coverage limits and next increments

The current weather-service interface provides a departure timeline frame, not
the full dataset's temporal extent or a stable file/model/run identity. The
report explicitly says when the full forecast range is unknown. It continues
checking weather during search and independent validation; it does not promise
that endpoint bounds certify the entire passage.

Further work should extend the provider contract with per-parameter time/area
coverage, precise missing-sample reasons and stable dataset identities. Other
follow-ups are chart links and setting shortcuts, a dedicated interactive
pre-compute checklist, fuller host/build provenance, session persistence of
diagnostic snapshots, and detailed batch/multi-leg summaries.

Saved route configuration formats and routing safety policies are retained.
Reports are available in the current session; diagnostic history is not yet
persisted in configuration XML. No fallback, shoreline resolution, land/depth
requirement or search limit is relaxed automatically.

## Validation

The native plugin is built with stock API 1.21 headers and wxWidgets 3.2.
Regression cases cover source selection, Pierre's forecast bounds, UTC and
dateline handling, actual versus permitted fallback, disabled versus missing
currents, wave limits, retained engine history, import warnings, and JSON
round-tripping. The full local CTest suite is run for the final implementation.

Windows/macOS/Android platform builds and installed-host qualification remain
necessary before publication. Source changes to the Android report and clipboard
path are not, by themselves, device qualification.

The settings-footer follow-up also builds the Linux API 1.21 and 1.22 variants
and the Android ARM64 variant; Android native libraries pass the 16 KB alignment
check. Local installed-build checks verify OK closes settings from both desktop
tabs and Android Done works in portrait and landscape on the attached SM-X210.
The saved Android route configuration remains unchanged. These settings checks
do not qualify every routing/reporting path or replace the full release matrix.
