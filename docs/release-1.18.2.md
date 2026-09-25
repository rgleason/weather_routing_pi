# Weather Routing / xWeatherRouting 1.18.2

This update makes the active land-checking mode visible beside **Detect Land**.
The label says **GSHHG shoreline only** when loaded-chart checks are not
enforced and **loaded charts enforced** when they are. GSHHG is shoreline data;
it does not verify Palmerston's reefs, shoals or charted depths. The Basic page
now says so explicitly. The search algorithms and their shoreline data have
not changed.

Quick retains at most 128 isochrone layers for display, as before. On a long
voyage it now thins earlier display layers and samples later layers across the
passage instead of keeping only the first 128. This changes the chart overlay,
not the accepted route or its validation.

When an endpoint fails the configured minimum-depth check, the message now
identifies the selected chart source, chart file and sampled position. It also
reports the triggering depth when the exact raw chart cell supplies one.
No depth classification rule has changed.

An unresolved `PENDING_DATA` chart response now rejects an edge until the
chart-safety request is serviced, including during final validation. It can no
longer be interpreted as clear water.

The user-facing version is 1.18.2; OpenCPN package metadata uses `1.18.2.0`.
Debian 12 and 13 preview tarballs include root-level `metadata.xml` for direct
import into OpenCPN.
