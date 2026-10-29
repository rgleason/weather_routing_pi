# xWeatherRouting 1.35.0

One source release now builds against the wxWidgets 3.2 toolkit used by
existing OpenCPN installations and wxWidgets 3.3.4 used by the new Windows
x64 development host. Routing engines, saved boats and route settings retain
their existing formats and behaviour, including the 1.34.1 gust-data fix.

## Windows packages

Choose the package matching **OpenCPN's wxWidgets runtime**, as well as its
architecture:

| File suffix | Matching Windows x64 host |
| --- | --- |
| `-wx329.tar.gz` | OpenCPN 5.14.2 x64 testing host with wxWidgets 3.2.9; compatible later 3.2 runtimes require matching compiler/build options |
| `-wx334.tar.gz` | OpenCPN 5.15.0 development host with wxWidgets 3.3.4, leading to the planned 5.16 release |

These are separate DLL builds of the same plugin version. A DLL built for
wxWidgets 3.2 must not be loaded into a wxWidgets 3.3 host. The plugin uses
the host's wxWidgets libraries; packages do not include a second wx runtime.
The Windows x86, macOS, Linux, Flatpak and Android targets retain their
existing platform-specific dependencies.

Both Windows x64 hosts currently advertise the catalogue label `msvc-64`.
Until OpenCPN/catalogue identifiers distinguish their wx ABIs, the ordinary
catalogue matrix retains the wxWidgets 3.2 package. The explicitly labelled
wxWidgets 3.3.4 package is a separate download for manual import, staged under
`windows-x64-wx334/` rather than mixed into the ordinary catalogue entries.
No new, unsupported catalogue target is invented.

## Compatibility changes

- List traversal supports both wxWidgets 3.2's linked lists and 3.3's
  default STL-backed containers, including existing-route extraction and
  chart checks.
- Dynamic chart-check messages use `wxGetTranslation`; the `_()` macro in
  newer wxWidgets accepts literal message identifiers only.
- JSON error logging uses explicit UTF-8 conversion and formatting instead
  of relying on implicit operators removed in newer wxWidgets.
- MSVC discovery preserves the selected SDK library directory and checks
  the exact wxWidgets version requested by the pinned build.
- Boat crossover completion explicitly joins its worker without nested GUI
  event dispatch, giving both wxWidgets versions the same wait behaviour.
- The Windows monotonic-clock helper includes its standard time and error
  declarations directly; wxWidgets 3.3.4 no longer supplies `timespec`
  indirectly through its array headers.
- String collections use wxWidgets' string-array interface consistently,
  avoiding MSVC's conflicting exported `std::vector<wxString>` instantiations
  and SDK-specific STL helpers. The Windows DLL supplies the API waypoint
  list helpers required by wxWidgets 3.3's inline STL-backed lists.
- Climatology compatibility includes the main-catalogue 1.6 family and
  Climatology 2.0.0 with the `ocpn-climatology-2026.2` dataset. Both retain
  the same wind/current, wind-atlas and cyclone callback interfaces and
  units. The additional typed query API in 2.0 is optional for this consumer.
- Every Climatology advertisement is checked, including provider unload
  and replacement. Unsupported or malformed advertisements clear all three
  callbacks so a previously accepted provider cannot leave stale pointers.
  The existing 0.10 through 1.6 compatibility range is retained.

## Qualification

CI builds both Windows x64 ABIs against API 1.21 and 1.22 headers. Each build
pins its genuine upstream OpenCPN installer and official wxWidgets archives
by SHA256, derives its OpenCPN import library from that host, checks plugin
imports against the host and its wxWidgets exports, then loads the packaged
plugin in an isolated host profile and calculates a real-GRIB route. The
existing route visibility, clearing and recomputation lifecycle checks run
for both hosts. The complete platform matrix remains part of release
qualification.

wxWidgets 3.3 development releases do not guarantee binary compatibility
between patch releases. The new target is specifically 3.3.4; it does not
claim compatibility with every earlier 3.3 release or a future 3.4 runtime.
