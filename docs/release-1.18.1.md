# Weather Routing / xWeatherRouting 1.18.1

Quick now tries a few additional valid tack and gybe angles when repairing an
isochrone candidate into a sailed route. This resolves a reproduced failure on
the Santa Cruz to Kaneohe route with the bundled default polar and changing
Pacific GRIB wind: Quick previously exhausted its candidate-validation
allowance less than one nautical mile from the destination. The final route
still has to reach the destination and pass chronological validation. See the
[reproduction and test report](quick-candidate-repair-20260924.md).

The user-facing version is 1.18.1; OpenCPN package metadata uses `1.18.1.0`.
Debian 12 and 13 preview tarballs include root-level `metadata.xml` for direct
import into OpenCPN. Broader platform builds and Alpha publication are reserved
for the later release pass.
