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
  host prefers the optional bounded semantic-grid batch API and retains the
  existing object-query path as a compatibility fallback. Decryption and
  licence enforcement remain inside the chart plugin and its helper process.
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

OpenCPN's startup identity is treated as provisional until all chart-provider
plugins have loaded. Provisional identity notifications may populate the hot
RAM cache, but they cannot open, reset or write the persistent store. The
first main-thread chart prewarm confirms the post-load identity; only then may
the matching disk store answer requests. This prevents plugin load order from
destroying a valid warm o-chart cache.

Where the chart-provider contract explicitly permits derived semantic safety
data to be retained, plugin-vector tiles use the same identity-scoped disk
cache as native vector and CM93 tiles. The cache contains only the classified
hazard/depth raster returned through the safety API, not a decrypted chart or
chart object stream. A provider/chart identity change invalidates it before it
can answer another route.

The semantic tile store is xWeatherRouting's expanding long-term safety mask.
It is the cache which prevents repeated chart-object extraction when later
routes revisit the same area, and a route outside its current extent extends
it safely on demand.

The enhanced host can additionally keep sparse certified-safe proofs. Each
entry represents a 0.2-degree coarse cell for one exact combination of chart
identity, active chart group, safety margin, depth-check state and minimum
depth. A cell is stored only after every required fine cell and margin halo is
authoritatively clear. This auxiliary proof store is opportunistic: it can
remain absent when no complete coarse cell is eligible, without reducing
semantic-tile persistence or causing o-chart objects to be extracted again.

Disk growth is bounded independently: the semantic tile store retains at most
65,536 live fine tiles and the host retains at most 32,768 certified coarse
cells. These are deliberately conservative implementation caps rather than
route-search boundaries. The RAM budget remains user-configurable in Advanced
settings; changing a disk cap should wait for benchmark evidence that the
current bounds are unsuitable.

Land areas and permanently dry objects reject a segment. Drying and awash
objects are classified from `WATLEV`; dredged areas (`DRGARE`) are treated as
depth areas rather than drying areas. `DEPARE`/`DRGARE` use `DRVAL1`, while
isolated `WRECKS`, `UWTROC` and `OBSTRN` dangers use `VALSOU`. A required-depth
check fails closed when an isolated danger has no usable depth.

## Reachability and long-passage prewarm

Prewarm geometry is a performance hint only. It never bounds the weather
solver, certifies unexamined water, or changes the fail-closed authoritative
segment checks made as a route expands.

For a coastal or medium passage shorter than 600 NM, prewarm first requests a
filled geodesic reachability envelope. If `S` and `G` are the endpoints and
`L` is the logged maximum path length, every point `P` on a route whose total
sailed length is no more than `L` satisfies
`d(S,P) + d(P,G) <= L`. Tiles intersecting that ellipse, plus the configured
safety margin, are requested in provider-sized batches. The envelope uses a
cross-track semi-axis of 45% of direct distance, bounded to 12–75 NM, and is
unioned with all retained scout/frontier geometry. Its path-length value is an
explicit cache-coverage guarantee, not a routing horizon: longer or wider
routes remain eligible and extend the mask on demand.

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
