# Weather Routing / xWeatherRouting 1.18.3

This release uses a three-part version (`1.18.3`) in OpenCPN's plugin
metadata, archive names, About dialog and build log. It contains the same
routing code and checks as 1.18.2.1.

This update clarifies the data source beside **Detect Land**. Its stable label
is **GSHHG shoreline check**. The Basic page explains that GSHHG shoreline is
used by default. When both chart options are enabled on a compatible host,
loaded charts enforce route land and depth safety while GSHHG helps the initial
search. GSHHG alone does not verify Palmerston's reefs, shoals or charted
depths. The search algorithms and their shoreline data have not changed.

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

The user-facing and OpenCPN package version is `1.18.3`.
Debian 12 and 13 preview tarballs include root-level `metadata.xml` for direct
import into OpenCPN.
