# Weather Routing 1.17.8

Weather Routing 1.17.8 adds a configurable GRIB timeline cache for routes using
large or high-resolution forecasts. Main and Quick remember independent limits
under their Advanced **Resources** settings. Existing configurations keep the
historical defaults: 512 MiB for Main on 64-bit systems (192 MiB on 32-bit) and
64 MiB for Quick. Engine preset resets preserve both values.

The cache retains immutable GRIB frames only when the route requests them. One
aggregate cache is shared by every route in a computation batch, including all
departure-time candidates and multileg continuations, so the selected limit is
not multiplied by the number of completed or concurrent routes. Completed
routes retain their compact route results. Timeline frames are released when
the final candidate or continuation finishes, or when calculation is cancelled.

Explicitly enlarged caches use a physical-memory admission rule. A cache of
`C` GiB is enabled only when at least `2 + 3C` GiB is available before routing,
which leaves `2 + 2C` GiB outside the full cache. For example, a 2 GiB request
requires at least 8 GiB available before calculation and retains a 6 GiB
reserve after the cache fills. The cache checks available physical RAM while it
grows and reduces retention if that reserve disappears. Swap is not counted.
When a large request cannot be admitted, routing continues with the engine's
standard cache rather than failing. Native 32-bit builds cap the setting at
192 MiB.

This changes Weather Routing only. xGRIB and the bundled GRIB plugin continue to
provide timeline frames through the existing plugin-message API. The source
GRIB remains owned by its provider; Weather Routing releases its copied frames
after the calculation batch. Cache diagnostics record requested and effective
limits, unique timeline keys, hits, evictions, reloads, peak retained bytes and
released bytes.

Headless scenarios can set the selected engine's limit with
`route.gribTimelineCacheMiB`, allowing the same large-GRIB case to be compared
under different cache budgets.
