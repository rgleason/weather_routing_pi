# `fixed-master` validation candidate

This branch starts from Rick's `master` at `dae3823`. It keeps the 1.17.12
WeatherRouting routing, UI, data and plugin identity. It does not import the
1.18.4 engines or settings changes from xWeatherRouting.

The branch replaces the experimental build and packaging setup with the
separately validated platform build path. CircleCI always builds the native
`weather_routing_pi` identity and runs the Linux, Flatpak, macOS arm64 and
Windows x86 validation jobs. Android jobs are not scheduled.

CircleCI does not publish packages or receive Cloudsmith deployment credentials
on this branch. A later deployment workflow needs a separate review before this
branch is used for publication.

Local checks before publication of this candidate: native shared library build,
299 CTest cases, clean package build, embedded metadata, stock-host ABI and
compact shoreline archive checks. Hosted CI results are recorded on the branch
commit in GitHub.
