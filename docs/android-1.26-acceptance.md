# Android 1.26 acceptance — 6 October 2026

The shared 1.26 routing, bounded coastal access and chart-grid traversal changes
are incorporated into the existing Android plugin. The Android Plan, Routes,
Results and Tools controls remain unchanged from the tested 1.25 source.
The Android linker and packaging checks also retain the 16 KiB page-layout fix.

## Build and device

- Tested source: `e4c69fc4f35445e172f359a5286bb9e0be259efc`, based on shared
  routing revision `cd3db9e811f6c5852e711fecf052f9397f91a983`.
- API 1.21, arm64, NDK 26.1.10909125, stock Android OpenCPN link library.
- Samsung SM-X210, Android 15, OpenCPN development app 5.14.1.
- The device uses 4 KiB kernel pages. ELF LOAD, RELRO and offset checks verify
  16 KiB layout; execution on a 16 KiB kernel is not claimed.
- Import archive SHA-256:
  `ce6d6c8d6ce1831e5da9e0171eb89d8bdd0a5bec3c12ded6b579df5cb5b38d4f`.
- Packaged and loaded library SHA-256:
  `b81c3514a76cbfff66b659cf71216cc6f56a61f9dfb315ff9e0a8b4c3e8bd1b9`.

The development app was upgraded through Plugin Manager's normal import flow.
It was reimported successfully after routing and dialog tests, then cold-started
with the user's restored profile. No application APK was replaced.

## Reproducible routing comparison

Both versions ran on the same device and host with the same Nicholson 35 boat,
full GSHHG shoreline, 0.1 NM land margin and deterministic uniform-wind GRIB.
This is a synthetic regression fixture, not an operational forecast or a
reproduction of Quinton's Pacific passage. Endpoints are 53.4 N, 5.5 W to
53.4 N, 5.3 W; the fixture has 24 hours of 6 m/s eastward wind, no currents
and no wave constraint.

| Android mode | Selected engine | Passage | Same route fingerprint as 1.25 |
| --- | --- | ---: | --- |
| Quick | Quick | 1:47:42 | Yes |
| Standard | Standard | 1:37:34 | Yes |
| Professional | Professional | 1:39:33 | Yes |
| Auto | Quick | 1:47:42 | Yes |
| All | Standard | 1:37:34 | Yes |

All five results pass final validation. All also retains the Alternative
candidate (1:40:33); Alternative is accessed through All, rather than a separate
mode. The impossible-wind control fails in both versions. A departure on actual
land is rejected by the Android startup check in both versions.

## Mobile workflow and safety checks

- Departure optimisation produces five hourly candidates. Three within forecast
  coverage complete; the two before coverage fail explicitly. The candidate
  picker displays all five and does not give failed candidates a successful ETA.
- Candidate comparison, selecting Alternative through All, showing the selected
  route on the chart, and navigating Plan/Routes/Results/Tools work. Explicit
  selection updates the selected result as intended.
- Portrait/landscape changes retain a usable comparison dialog. Editing land
  clearance with the Android keyboard persists the value through a full restart.
- Comfort exploration retains validated normal candidates. A timed soft-stop
  during active exploration records `stopped=1` and retains the validated
  Standard result. Stop All cancels a longer coastal search without delivering
  an unvalidated route; Routing Status can be hidden and reopened.
- At a 1 NM land margin, Quick refuses the Conwy Bay coastal departure inside
  the buffer. Professional completes the approximately 6.8 NM northwest escape
  with bounded local access: 1:22:27, four legs, final validation passed. Actual
  land checks remain active. This is a focused GSHHG coastal control; a separate
  longer Holyhead–Conwy synthetic run was cancelled and is not claimed to pass.
- This Android host has no chart-safety service. Chart-awareness controls remain
  disabled, ordinary routing works, and a 3 m minimum-depth request fails with
  an explicit unsupported-service explanation. Real chart-depth enforcement on
  Android is not qualified by this test; it requires a host exposing that service.
- The post-test crash buffer contains no crash entries. The normal reimport and
  final cold launch succeed. Bundled shoreline verification and all six
  publication-contract tests pass.

## Preservation and release scope

The original six route configurations and preferences were restored. All 749
original routing files were verified unchanged; the navigation database was
not replaced. xGRIB and Celestial Navigation libraries remain unchanged. The
qualified 1.26 plugin remains installed in the development app.

Private device backups, logs and screenshots are retained locally under
`artifacts/xweather-1.26-android-20261006` and are not release payloads. The local
comparison and preservation reports record hashes and results. The platform CI
matrix, canonical package checks and publication hold still apply before alpha
publication. These tests do not qualify a new S-57/S-63 provider or real
chart-depth operation on this stock Android host.

## Actual CI package follow-up

The API 1.21 arm64 package from source
`b5e40bc1e8489e57eda53785eea9fe3f09c9e6f6` was imported through Plugin Manager
and cold-started on the same tablet. Its library SHA-256 is
`bf8615b43a80cd393827ef71ca3ede965bbe275d6ffd64d209471252bc717bcb`.
All five modes again reproduce the baseline passage times and fingerprints.
The 1 NM coastal control reproduces the validated 1:22:27 Professional result
and Quick's explicit buffer refusal. A 3 m depth request is explicitly rejected
on the unsupported host. The process maps confirm the imported library is
loaded, and the post-import/test crash buffer is empty.

Preferences, six routes and all 749 original routing files were restored and
verified again, preserving the other plugins and leaving the CI 1.26 library
installed. Task staging files and the superseded local import archive were
removed. Subsequent source changes repair only the macOS CI dependency
download and update qualification documentation; final publication still
requires the fresh platform matrix and archive review.
