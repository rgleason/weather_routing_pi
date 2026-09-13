# External-control planning provider (Preview B)

Preview B lets a hardened OpenCPN host discover xWeatherRouting as
`route-planning.chart-weather.v1`.  The adapter is deliberately optional:
it resolves the host registration functions at runtime.  Stock OpenCPN hosts
which do not export them log one informational message and retain every normal
xWeatherRouting GUI feature.

The native OpenCPN plug-in API remains version 1.21.  No virtual method,
plug-in class layout, or existing ABI contract is changed.

## Published Linux packages

The Preview B release contains two resource-complete Linux x86_64 packages:

- `xweather-routing-preview-b-hardened-arch-x86_64.tar.gz` is compiled against
  the Preview B core headers and registers the resident planning provider;
- `xweather-routing-preview-b-stock-arch-x86_64.tar.gz` is compiled solely
  against the vendored stock OpenCPN 1.21 API and is the backward-compatibility
  artifact for an unmodified OpenCPN host.

Both contain the plug-in library, data, locales, example boats and polars. The
raw shared-library assets are diagnostic artifacts and are not complete
plug-in installations. Package checksums are published with the release. The
complete isolated installation and acceptance procedure is in the OpenCPN
hardening branch's `docs/development/external-control-preview-b-testing.md`.

## Request boundary

The provider accepts coordinates, departure time/window, concurrency, routing
effort, minimum depth, land margin, an installed polar file name, and explicit
climatology fallback.  Preview B intentionally supports only the datasets
currently active in xGRIB/current providers.  It rejects paths as polar
identities and never imports returned geometry into OpenCPN automatically.
The host independently revalidates a completed route against authoritative
chart safety before exposing it as a draft.

Climatology fallback is fail-closed.  It is disabled when the field is absent
or false and maps to xWeatherRouting's existing most-likely mode only when
`allowClimatologyFallback` is explicitly true.  This uses the established
Climatology messaging contract and does not depend on a particular compatible
Climatology build.

## Lifecycle and compatibility fixes

The implementation fixes six defects found while qualifying the resident
provider path:

1. A completed resident calculation retained headless-run state, making every
   later request report busy.  Completion cleanup now clears that state on the
   OpenCPN thread.
2. A delayed OpenCPN-thread start could run after its temporary scenario files
   had been deleted.  A timed-out start is marked abandoned and the delayed
   callback cannot begin it.
3. External jobs and GUI calculations could start concurrently against shared
   route-engine state.  Admission now requires no resident, running, waiting,
   multi-leg, or departure-optimisation work.
4. The API's climatology-fallback choice was discarded by self-contained
   scenario loading.  The scenario contract now preserves an explicit value;
   omission remains deterministic and fail-closed.
5. A large initial chart prewarm held the OpenCPN thread long enough for the
   API cancellation request to time out. Externally initiated prewarm now runs
   in bounded batches and observes the host cancellation token between them;
   ordinary GUI routing retains its established path.
6. Cancellation during startup stopped route work but returned before clearing
   the resident headless-session marker. Terminal cancellation now includes
   owner-thread cleanup, so a subsequent request is admitted immediately.

The first three are confined to the new resident-provider path and do not
affect stock OpenCPN.  The fourth also corrects reusable headless scenarios.
No xGRIB or Climatology defect was demonstrated during this pass.

## Unload and cancellation

Only one resident job is accepted at a time.  Core cancellation is forwarded
to xWeatherRouting on the OpenCPN thread.  Provider removal first disappears
from discovery and cancels active work; plug-in deactivation is vetoed until
the pinned job has drained.  This prevents callbacks into an unloaded shared
library without changing the legacy plug-in ABI.

## Qualification expectations

The published Linux Preview B passed:

- 176/176 xWeatherRouting tests against its vendored stock 1.21 API;
- compilation against both stock and hardened OpenCPN hosts;
- hardened-host cold-start registration;
- a completed route with independent authoritative OpenCPN validation at
  5.0 m minimum depth;
- cancellation of an active seven-departure, 240-hour request; and
- immediate successful provider reuse after that cancellation.

Preview B is a developer preview.  Resource discovery, concurrent provider
jobs, broader dataset identities, and automatic route import are intentionally
deferred until this narrow lifecycle and safety boundary has field evidence.
