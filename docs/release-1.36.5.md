# Weather Routing 1.36.5

Each saved weather-routing configuration now has one **Passage shoreline**
resolution, used by every routing method, including Auto and All. Choosing Full
in Auto can no longer leave Quick or Standard using an older, coarser choice.
Numerical settings, search allowances, adaptive policies and routing algorithms
are unchanged.

Existing configurations with different method resolutions migrate to the finest
explicitly saved choice. Once a user chooses another level, that choice is
remembered for the passage and last-used defaults. Saved XML also mirrors the
choice into the older method keys for reopening in earlier versions. Missing or
invalid optional data never silently downgrade the selected resolution.

Chart-enforced routing retains its separate **Scout shoreline resolution**.
That controls preliminary GSHHG work; authoritative chart/depth checks remain
separate from shoreline-only routing.

* All five dataset levels remain visible, labelled available or not installed.
  Installing High/Full makes the data available; select it in passage setup to
  use it. Installation does not rewrite saved passages.
* **View → Show selected passage shoreline** draws the selected dataset's
  actual GSHHG outlines in purple. The labelled snapshot remains visible when
  the routing window is hidden. Toggle off and on to inspect another setup.
  Android exposes the action under **Tools → Display and safety**.
* The outline viewer excludes polygon tile-clipping edges and bounds the view
  to 16 tiles / 30,000 segments. Broad or dense views ask for a closer zoom
  rather than silently substituting simpler geometry. Rendering is cached per
  viewport; dataset verification occurs on selection, not during repaint.
* The run record retains the exact prepared dataset's resolution, version,
  SHA256 and file identity. The installation help explains that OpenCPN 5.10+
  displays a separate OSM background map, updated through Chart Downloader's
  OSM basemap catalogue.

Qualification includes the complete Linux test suite, migration/rollback and
shoreline-view checks, real Full-GSHHG crossings at Alcatraz, Palmerston and near
Lalona, GUI method switching/edit/save controls, and normal shutdown.
The real Irish Sea GRIB control retains its exact track and ETA
(`857383926234c8c9`, 28 September 2026 01:30:53 UTC).

The geometry probes do not reproduce Quinton's reported Tonga route; that still
requires his delivered GPX, configuration and diagnostics.

Software chart rendering no longer initializes OpenGL in the drawing helper.
This prevents a macOS startup crash when no GL context exists and also covers
the visible shoreline overlay. A real wxDC drawing probe rejects any GL query
and verifies that software lines/text are painted. Inactive shoreline viewing
does not construct a drawing helper.
