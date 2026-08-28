# Chart-safety cache

Weather Routing owns the authoritative cache of chart-classified safety
tiles. OpenCPN supplies exact chart classifications through an optional,
dynamically detected host capability; the plugin owns the RAM policy, durable
format, invalidation and recovery.

## Host compatibility

- On an enhanced OpenCPN host, Weather Routing registers its cache callbacks
  and uses the highest-detail applicable chart selected by the host safety
  service.
- The enhanced host accepts native S57/CM93 charts and vector charts supplied
  by a chart plugin. This includes o-charts when their licensed plugin is
  installed, enabled and the chart is in the active OpenCPN chart group. The
  host uses the normal plugin object-query API, so decryption and licence
  enforcement remain inside the chart plugin and its helper process.
- Applicable official/native and plugin vector charts rank ahead of CM93;
  within that provider tier the smallest scale denominator wins, followed by
  the newest chart edition and file timestamp. The selected chart path, scale
  and source are returned in safety diagnostics.
- On stock OpenCPN, the same plugin binary loads without unresolved enhanced
  API symbols and retains standard GSHHS land checking.
- On a stock host, saved enhanced-safety preferences are inactive and both
  enhanced controls are disabled in the UI. This prevents a profile last used
  with an enhanced host from making the stock plugin unusable; ordinary
  `Detect Land` routing continues with GSHHS.
- When an enhanced host is present and an authoritative enhanced-safety
  request cannot be completed, that request fails closed. It never silently
  substitutes GSHHS for an active enhanced-safety check.
- On a new enhanced-host installation, chart safety and enforcement default
  on. Existing explicit user choices remain unchanged.

## RAM and disk policy

The hot cache is an LRU with a user-selectable budget from 256 to 8192 MiB.
`Auto` uses one thirty-second of physical memory, rounded to 256 MiB and
bounded to 256–2048 MiB. Increasing this budget reduces repeated SSD reads
without changing any safety result.

Persistence defaults on. The disk cache is append-only during routing:
changed tiles are written in batches, not by rewriting the complete cache.
Records are checksummed and a partial final record is discarded after an
unclean shutdown. Compaction is deferred until shutdown and only considered
after the file exceeds 1 GiB.

The cache identity includes the host chart-set identity. Any chart database,
chart file metadata, chart group or safety-grid format change invalidates
stored tiles rather than risking a stale safety answer. Cached payloads retain
the exact hazard flags, depth availability and minimum depths produced by the
host.

Tiles derived from licensed plugin-vector charts are the exception: they are
kept only in the bounded RAM cache for the current OpenCPN session and are not
written to either the plugin or legacy host disk cache. This keeps the safety
integration within the chart provider's normal runtime access contract.

Land areas and permanently dry objects reject a segment. Drying and awash
objects are classified from `WATLEV`; dredged areas (`DRGARE`) are treated as
depth areas rather than drying areas. `DEPARE`/`DRGARE` use `DRVAL1`, while
isolated `WRECKS`, `UWTROC` and `OBSTRN` dangers use `VALSOU`. A required-depth
check fails closed when an isolated danger has no usable depth.

## Long-passage prewarm

Prewarm geometry is a performance hint only. It never bounds the weather
solver, certifies unexamined water, or changes the fail-closed authoritative
segment checks made as a route expands.

For an ocean passage of at least 600 NM, fallback prewarm uses five narrow
route-shaped corridors: the great-circle centreline plus symmetric inner and
outer bowed alternatives. The outer cross-track displacement is 15% of
passage length, bounded to 90–900 NM, and the inner alternatives are halfway
between it and the centreline. Each line has only a 6–12 NM raster margin.
Consequently a passage thousands of miles long can prefetch plausible
synoptic-scale diversions without filling a huge rectangular raster or
turning the corridor into a route-quality assumption. Scout-derived prewarm
is preferred when the preliminary isochrones expose a more relevant search
shape; any other solver excursion is populated safely on demand.

The Chart Awareness settings page shows capability state, effective RAM,
cache counts and I/O totals, and provides an explicit clear action. Headless
test runs log the same counters as `WR_PLUGIN_CHART_CACHE`.

The same RAM budget is also available in the Weather Routing configuration
dialog's **Advanced** tab, beside the departure-time worker control. Changes
take effect immediately and are saved for subsequent OpenCPN launches. The RAM
budget affects only the plugin's hot tile cache; certified tile persistence on
SSD remains enabled independently.
