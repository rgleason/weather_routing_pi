# xWeatherRouting 1.19.1 alpha

This release adds a native Windows x64 target to the existing Android and
desktop CircleCI matrix. It retains minimum plugin API 1.21 and validates
each platform with the stock 1.21 and 1.22 headers separately. API 1.22
compatibility artifacts are evidence, not additional catalogue entries.
Android and Windows x64 remain alpha testing targets.

## Windows x64

The target is the upstream `msvc-64;10;x86_64` host identity. This differs
from our earlier `msvc-wx32-x64` Preview identity. The matching host is Dave
Register's testing installer built from the OpenCPN 5.14.2 release revision
`bd6986a08`, not the older chart-aware Preview SDK. Host and wxWidgets 3.2.9
archive hashes are pinned in `ci/windows64-sdk.json`.

Run `ci\circleci-build-windows64.bat` in a disposable Windows checkout with
MSVC 2022 Community, Git, CMake, Python 3.11 or newer, 7-Zip and Chocolatey.
The script extracts the host without installing it, builds AMD64 import
libraries from its actual OpenCPN and zlib exports, builds the source-based
utility libraries, runs regression tests, packages the plugin, embeds root
`metadata.xml`, and verifies the plugin's host imports and AMD64 architecture.
The genuine host then runs a short routing scenario in an isolated portable
profile. Windows x86 has a matching test using the official 5.14.2 installer.

The official Win32 installer bundles MSVC runtime 14.12.25810. New MSVC
headers' constexpr mutex initialization can crash with this old runtime even
when tests pass against the executor's newer runtime. MSVC builds therefore
use Microsoft's `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` compatibility option;
the genuine-host route test checks the installed host's runtime as supplied.
See https://github.com/microsoft/STL/releases/tag/vs-2022-17.10.

Do not reuse the x86 `opencpn.lib` or `zlib1.lib` for an x64 build. A direct
CMake x64 build fails unless its matching import-library paths are supplied.

## API compatibility

Set `WEATHER_ROUTING_API_HEADER_VERSION` to `1.21` (default) or `1.22`, as a
CMake cache option or environment variable. Both builds use the existing
`opencpn_plugin_121` base and report API 1.21. Compilation checks the selected
header revision and minimum host requirement. No new mandatory API 1.22
methods are called. The official 1.22 message/event API does not replace our
optional chart/depth-safety host extensions.

The dependency revision is pinned to upstream opencpn-libs
`b4e4aa267de99f4310beea18e2fa58e26a5bbcb8`, which deploys API 1.22. The
polar extrapolation fix `f0a2546` is already in the release ancestry.

## Publication

The workflow requires all 22 builds to pass before the existing manual alpha
approval hold. Only the eleven API 1.21 package/XML pairs are persisted for
deployment. The publication checker rejects wrong architectures, identities,
API requirements, incomplete matrices and missing embedded metadata. Packages
go to `pob220/xweather-routing-alpha-oss`; no beta or master catalogue update
is part of this release.
