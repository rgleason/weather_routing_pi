# xWeatherRouting / WeatherRouting 1.35.1

The Windows x64 catalogue package now uses wxWidgets 3.3.4, matching the
OpenCPN 5.15 x64 development host. Version 1.35.0 placed its wxWidgets 3.2.9
build in the catalogue and required manual import of the wx334 package.
The 1.35.1 Windows x64 package ends in `-MSVC-x64-wx334.tar.gz`.

The primary SDK is pinned to the official OpenCPN
`5.15.0-0+85.e41f0de` x64 installer, which includes the upstream
WinUsbWatchDaemon shutdown fix. The wxWidgets 3.2.9 build is retained for
the older 5.14.2 x64 host under `compatibility/windows-x64-wx329/`, outside
the ordinary catalogue publication. Both hosts advertise `msvc-64`, so
their incompatible DLLs cannot safely be offered as interchangeable entries
for that same target. Users of the older host must keep using its matching
wx329 package. Windows x86 and the other platform dependencies are unchanged.

Build verification and publication staging inspect the actual AMD64 DLL
import table. Renaming a wx329 archive or correcting only its XML cannot
make it qualify as the wx334 catalogue package. Embedded and external
metadata must still agree.

Both API 1.21 and 1.22 header matrices remain required: 24 platform builds,
including the two legacy Windows x64 builds. Windows qualification checks
the packaged DLL against the pinned host and runtime exports, installs it
into an isolated normal OpenCPN profile, calculates real-GRIB routes, and
checks route visibility, clearing, recomputation, disable/re-enable and
shutdown. Publication contract tests run separately.

The standalone repository defaults to xWeatherRouting and the
`pob220/xweather-routing-alpha-oss` channel. The mergeable branch defaults
to WeatherRouting and `opencpn/weather-routing-prod`. Validation does not
upload packages. Production publication after merge still requires a
complete successful qualification from Rick's master and approval of the
prepared artifacts before upload. Routing behaviour and saved settings
retain the 1.35.0 formats.
