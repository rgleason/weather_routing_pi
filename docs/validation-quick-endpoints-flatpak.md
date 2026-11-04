# Bounded Quick endpoints and Flatpak preliminary validation

This development change is based on 1.29.2. It enables the existing
`allowCoastalEndpointLeeway` option for the GUI Quick engine, including its
Auto and All attempts. Professional interim bridges and engine search budgets
remain unchanged. The shared policy still caps access at
`min(2 NM, max(0.5 NM, 2 * land margin))` around each original endpoint.
Outside those fixed areas, full clearance applies even within a long leg.
Independent replay and the final plotted-route checks remain required.

The Flatpak compatibility target is now `flatpak-<architecture>`, without
the extra wx version. Flatpak data installs under `share/plugins`, matching
the stock host's imported `data/plugins` search path. Ubuntu 22.04 builds
detect wx 3.2 from the actual build environment and use
`ubuntu-wx32-x86_64`. Other current platform labels match the host rules.
CI checks generated and embedded metadata for all platform jobs, and Flatpak
jobs now import the package and run all three visible engines in the stock
host with a disposable application home and display.

Local checks on 12 October 2026:

- All 482 native CTest cases pass, including the existing long coastal
  egress, forged long chord, actual hazard, depth and endpoint-radius tests.
- All 14 packaging/publication tests pass. The Ubuntu 22.04 Docker configure
  produces `ubuntu-wx32-x86_64`; Flatpak 25.08 produces `flatpak-x86_64`.
- An audit of 44 previous build metadata files (both repositories and API
  header variants) finds only the Flatpak and Ubuntu 22.04 label mismatches.
  This is a label audit, not a new runtime test of all 44 builds.
- Unmodified stock Flatpak OpenCPN 5.14.0-1+91f3b67 rejects the old package,
  imports the corrected package, finds its installed polar, and completes
  Quick, Standard and Professional offshore routes using the synthetic
  forecast fixture. The compact shoreline archive also passes its audit.
- With the actual forecast, Nicholson 35, 0.4 NM margin and 3 m depth checks,
  Quick completes Holyhead to nearby clear water (53.40 N, 4.61 W):
  3.770 NM in 36 minutes 50 seconds; search time 108 ms. The formerly
  successful offshore Quick control has the same distance and arrival time.
- The full Holyhead–Conwy departure at 13 October 12:00 UTC still exceeds
  Quick's candidate-validation allowance. Auto completes through Standard
  with the same passage time as the baseline, but its additional Quick
  attempt takes about 11 seconds. A 1 NM margin also does not make this
  departure solve in Quick. Bounded endpoint admission does not guarantee
  that Quick can solve every coastal passage.

Development binaries are installed on the laptop and attached Android tablet
for user GUI testing. They retain the displayed version 1.29.2. The Android
binary passes the 16 KB ELF alignment gate. This preliminary development stage was superseded by the qualified 1.29.3
release described in `validation-quick-nominal-1.29.3.md`.

Private run reports and installation backups are retained outside the source
tree in `output/xweather-endpoints-flatpak-20261012`. The exact historical
50 NM Anglesey route has not been reproduced in this pass; the existing
synthetic coast-following regressions exercise the earlier loophole.
