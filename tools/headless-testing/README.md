# xWeatherRouting headless smoke testing

This kit wraps the headless scenario runner already present in xWeatherRouting.
It does not change the plugin or its routing engines. It was prepared against
xWeatherRouting 1.30.1 source (db39eba42ce4285eabf0ed69be8e29a126859287).
The local integration test uses 1.30.0 binaries, which have the same routing
algorithms. The scripts use Python's standard library only (Python 3.8+).
The application launch path has been exercised on Linux; this is not a claim
of Windows or macOS qualification for the wrapper.

## Files

- [run_headless.py](run_headless.py): run one scenario through selected engines, sequentially.
- [extract_timings.py](extract_timings.py): extract timing records from ordinary or headless logs.
- [example-kraal.json](example-kraal.json): example using the saved Cape route constraints.
- [Comparison tables](quinton-comparison-tables.md): reported outcomes and measured local timings.
- [Download the complete kit](https://github.com/pob220/xweather_routing_pi/raw/refs/heads/xweather-routing-alpha/tools/headless-testing/headless-testing-kit.zip): scripts, this guide, example and tables.

Download and extract the ZIP, then run the commands below from the extracted
folder. Alternatively, use these files from a checkout of this repository.

## Prerequisites and one-time profile setup

1. Use a current xWeatherRouting installation with the JSON headless runner.
   The scripts do not build or install OpenCPN/plugins.
2. Prepare a SEPARATE OpenCPN test profile. Start your chosen OpenCPN binary
   or launcher with --configdir=/absolute/path/to/test-profile. Create the
   directory first. In this profile, enable xWeatherRouting and a compatible
   GRIB plugin (xGRIB works), add your charts, install the selected GSHHG
   shoreline data, dismiss first-start dialogs and then close OpenCPN.
   Confirm the profile has valid tide/harmonic data paths too: a missing-data
   startup dialog will block unattended runs, even for a route without currents.
   If using climatology fallback, enable the climatology plugin and its data.
   The supplied --profile is a working test profile: OpenCPN may update it.
   The script refuses the usual ~/.opencpn directory. Other working-profile
   locations cannot be identified automatically, so choose the path carefully.
3. Use a Boat XML file whose polar paths are valid on your machine. The script
   resolves route.boatFile relative to the scenario or accepts --boat. It does
   not rewrite paths inside the boat XML and does not supply a polar.
4. Supply an existing GRIB covering the intended area/time. Edit the example's
   startTime when using a different forecast. It must be explicit UTC (Z).
   Climatology fallback is an explicit scenario choice, not automatic.
5. Chart mode requires an OpenCPN build exposing the compatible chart-safety
   services. For Quinton's chart-aware preview bundle, keep its supplied runtime
   launcher/environment. Ordinary OpenCPN can run GSHHG tests, but may not
   provide the chart-safety services required for an enforced chart test.
6. On Linux, --xvfb uses a private virtual display. Install Xvfb if needed
   (Debian: sudo apt install xvfb). Omit --xvfb to use the current display.
   Close other OpenCPN instances when testing to avoid single-instance/IPC
   conflicts. A launcher must forward the --configdir and --no_opengl arguments
   and remain attached to the OpenCPN process rather than backgrounding it.

## Example command (replace every /path/... value)

```sh
python3 run_headless.py \
  --opencpn /path/to/opencpn-or-launcher \
  --profile /path/to/separate-test-profile \
  --scenario example-kraal.json \
  --boat /path/to/Boat.xml \
  --grib /path/to/cape-forecast.grb2 \
  --engines quick standard professional all \
  --timeout 900 \
  --xvfb \
  --output /path/to/new-results-directory
```

If required, add --plugin-dir /path/to/plugin-libraries (repeat the option for
additional directories). Otherwise OpenCPN uses the profile and the launcher’s
existing plugin search configuration. The executable may still depend on its
distribution's shared libraries/data; use its normal launcher where required.

Each selected engine gets a fresh OpenCPN PROCESS and its own scenario/result
files, but all use the supplied test profile. Consequently chart caches can
warm between runs. For performance studies, record cold/warm state, repeat the
same cases and control ordering; these scripts do not make cache states equal.
The timeout includes plugin preparation. An additional 60-second outer deadline
allows startup/shutdown; a process still running then is stopped and marked.
The wrapper sets TZ=UTC for consistent log timestamps where the platform
supports that environment setting. JSON scenario and result times use UTC.
The wrapper exits 0 only when all runs complete and exit normally. Expected
negative tests, time limits or missing results produce exit 1.

## Engine names: an important historical mapping

| Wrapper name | Scenario `routingEngine` value |
|---|---|
| `quick` | `original` |
| `quick2` | `quick2` |
| `alternative` | `alternative` |
| `standard` | `quick` |
| `professional` | `main` |
| `auto` | `auto` |
| `all` | `all` |

Alternative is exercised inside All. The released JSON schema has no separate
Alternative selector. Individual runs of Quick, Standard and Professional
avoid losing all their timings when Quick consumes an All run's time limit.
Alternative will still have no timing if All never reaches it.
The wrapper uses the modern xWeatherRouting engines, not catalogue Cowboy.
Catalogue Cowboy requires the separately pinned source comparison harness;
the laboratory run-case.py/original.cpp files are not portable plug-and-play
scripts for Quinton's installation.

## Inputs must be explicit

With WR_HEADLESS_SCENARIO, the plugin loads a scenario rather than importing
the saved GUI route XML. Put relevant settings in the JSON; do not assume it
will inherit the last GUI route. The kit overrides only routingEngine and an
optional --boat value. Other scenario settings remain as supplied.
The supported fields are documented in the public guide below. The JSON schema
does not express every historical XML setting. Check the emitted scenario,
WR_BUILD log, result searchSettings and shoreline identity before calling a
comparison an exact reproduction. The example intentionally retains clearance
constraints which may make it fail on the available chart data.
This wrapper handles single routes, not concurrent departure optimisation.

## Outputs to send with a bug report

```text
summary.json              Process wall time, exit/result status, build records,
                          GRIB identity and extracted per-engine timings.
01-quick/scenario.json    The exact scenario supplied to that process.
01-quick/result.json      Plugin outcome, voyage duration, geometry, safety and
                          available coverage notes; absent if runner did not start.
01-quick/opencpn.log      Log records from that run, copied from the test profile.
01-quick/timings.json     Structured engine/summary timing records.
01-quick/stdout.log       Startup/runtime console output.
```

Equivalent folders are created for subsequent engines. Retain the source GRIB,
boat/polar files, chart-source identity and installation versions as well.
No charts, forecasts, boat files or binaries are included in this kit.

Verification on 14 October 2026: the Linux wrapper ran All and Standard
sequentially against the Cape negative test, captured their failed outcomes,
and extracted the four main All attempts separately from its initial scout.
A short open-water Quick control completed with finalSafety=pass and exit 0.
These checks qualify the launcher/result collection, not every route or OS.

## Timings in NORMAL GUI runs

Yes: normal native-engine GUI runs write timing records to opencpn.log without
using the headless scripts or requiring a debug build.

```text
WR_ENGINE_ATTEMPT          One attempt inside Auto/All. elapsed_ms is computation
                          wall time; passage_s is simulated voyage duration.
WR_MODERN_NATIVE_SUMMARY   Overall native calculation, including inside-call
                          host-service waits; elapsed_ms is computation time.
                          scout=1 identifies a preparation scout.
WR_PROFESSIONAL_QUICK_BUDGET
                          Shows RAM admission for Professional's internal Quick;
                          this is a budget record, not a separate timing.
WR_RAW_TILE_PREWARM / WR_ROUTE_MASK...
                          Preparation work; it may happen outside the engine
                          timer. Some records describe nested/overlapping work.
```

For a native single-engine selection, use WR_MODERN_NATIVE_SUMMARY. Auto/All
also emit WR_ENGINE_ATTEMPT. A short initial Quick scout may appear separately;
do not count it as the full route attempt. Professional's elapsed time includes
its internal Quick stage and subsequent solver work.
The JSON candidate's elapsed field is VOYAGE time, not computation time.
retainedRoutes[].processingMilliseconds is another available timing field for
retained successful candidates; it is not present for every failed attempt.
Engine timing is not necessarily click-to-result time: earlier chart preparation,
queueing and subsequent UI/display work can sit outside it. Do not blindly sum
all nested timings. Serial headless tests also record process wall time and the
WR_HEADLESS_ROUTE_TEST route wall time, including preparation.

On Linux, inspect the chosen profile's log:
```sh
rg 'WR_BUILD|WR_ENGINE_ATTEMPT|WR_MODERN_NATIVE_SUMMARY|WR_PROFESSIONAL_QUICK_BUDGET' \
  /path/to/test-profile/opencpn.log
```

No ripgrep dependency is required by the Python extractor:
```sh
python3 extract_timings.py /path/to/opencpn.log --json timings.json
```

For a serial SINGLE-ROUTE log, add --single-route to associate attempts with
following summaries and mark scouts. Do not use that association flag for
concurrent GUI routes or departure optimisation: the attempt lines themselves
do not contain a route identifier, so attribution could otherwise be ambiguous.
The headless wrapper enables this flag only for its single-route tests.

## Troubleshooting

Missing result: inspect stdout.log/opencpn.log for plugin load failure, an
unacknowledged startup dialog, unsupported JSON field, unavailable GRIB plugin,
missing forecast, stale IPC state or a launcher overriding --configdir.
Check WR_BUILD to establish the loaded plugin version; a filename is not proof.
"running" or "timeout" is not completion. A route's finalSafety applies to
the SELECTED datasets/settings; GSHHG pass does not establish a separate CM93
pass. Likewise unknown waves/depths are not measurements of zero.

## Public source references (pinned to the reviewed source)

[Scenario schema, environment variables and examples](https://github.com/pob220/xweather_routing_pi/blob/db39eba42ce4285eabf0ed69be8e29a126859287/docs/headless_scenarios.md).

[Scenario examples](https://github.com/pob220/xweather_routing_pi/tree/db39eba42ce4285eabf0ed69be8e29a126859287/testdata/scenarios).

Synthetic unit-test benchmark script (requires a built test binary; it is NOT
the real-GRIB headless application runner):
[benchmark_modern_native_engine.sh](https://github.com/pob220/xweather_routing_pi/blob/db39eba42ce4285eabf0ed69be8e29a126859287/tools/benchmark_modern_native_engine.sh)

The [comparison tables](quinton-comparison-tables.md) accompanying this kit clearly identify missing
timings, time-limited tests and candidates rejected by a subsequent chart audit.
