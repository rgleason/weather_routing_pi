# WeatherRouting 1.18.4 integration candidate

This branch starts at Rick's validated `fixed-master` candidate (commit
`3b62cc4`), based on his 1.17.12 `master`. It applies the application source,
tests, UI, routing documentation and three-engine behaviour from Paul's
`release/xweather-1.18.4` (commit `28ba541`). The resulting plugin retains
Rick's native `weather_routing_pi` / WeatherRouting identity, configuration
path, repository metadata and catalogue package name.

The branch keeps the platform build and CI setup validated on `fixed-master`.
CircleCI builds Linux, Flatpak, macOS universal and Windows x86 targets with
the native identity. The separate Win32 check also runs. Android is excluded
from this integration target set. CI does not publish packages or use deployment
credentials; release and catalogue decisions remain separate.

Existing Main and Quick configurations map to Professional and Standard,
respectively. New installations default to the new Quick engine. See
[`quick-routing.md`](quick-routing.md) for settings and upgrade behaviour.

Local validation: native shared library build and 314 CTest cases passed on
Linux. Hosted platform results are attached to the branch commit in GitHub.
