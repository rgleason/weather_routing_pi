# xWeatherRouting for Android: product and implementation design

Design baseline: desktop xWeatherRouting `28ba541` (1.18.4), OpenCPN 5.14
development app on Samsung SM-X210 (Android 15, 1200 × 1920 pixels, 240 dpi),
and the installed xGRIB plugin. This document precedes Android implementation.

## Observed baseline

The attached tablet is accessible through ADB: application launch, taps,
screenshots, logcat, plugin import files and `run-as` access to its development
app work. The installed Weather Routing 1.17.12 opens as a near-full-screen
window with a File/Position/Routing/View/Help button row, two dense tables,
truncated column headings and a crowded output row. Its old tablet-specific
changes make dialogs reachable but preserve the desktop interaction model.
The existing binary, route XML and OpenCPN preferences were copied to
`/tmp/xweather-android-backup-20260925/` before replacement.

## Navigation and screen model

The plugin opens a single, resizable in-content workspace over the OpenCPN
chart. A top bar holds **Weather Routing**, current forecast coverage/status,
**Chart**, and **Close**. A visible four-item navigation strip switches between
**Plan**, **Routes**, **Results** and **Tools**. No operation depends on a menu
bar, right click, hover, keyboard shortcut or long press. Android Back closes
the current sheet, then returns to the previous page, then closes the workspace.

In portrait, pages fill the available content area and scroll vertically. In
landscape, Plan can place the route summary beside its editor and Routes can
place a selected-route detail beside the route list. The same controls remain
available when the Android keyboard appears. A primary action stays visible
at the bottom of the active page. Touch targets are at least 48 dp, text and
spacing follow the host font scale, and destructive actions request a clear
confirmation. Controls use explicit Save/Cancel semantics on wxQt because
Android's modal return codes have proved unreliable in xGRIB.

### Plan

A short route builder asks for **From**, **To**, **When**, **Boat**, **Weather**,
**Engine** and **Safety** in that order. Each row opens a focused, scrollable
sheet; its current choice and any validation issue remain visible in the row.
From/To choose a saved position, an OpenCPN waypoint, the boat position (start
only), or **Pick on chart**. Chart picking temporarily hides the workspace,
shows a clear crosshair instruction, and returns to the same editor after a
tap or cancellation. Start and destination may also be edited as named
coordinates. When supports departure now, specified UTC/local time, planned
arrival, and departure optimisation with range/step and safety margin.

Boat chooses an existing polar/boat file or opens the boat library/editor.
Weather shows xGRIB's selected forecast and time coverage, permits the
existing climatology modes, and offers **Open xGRIB** when data are missing.
Currents and swell-dependent limits get field-coverage checks before compute.
Engine offers Quick, Standard and Professional with the existing stable IDs,
separate settings and a short capability description; unsupported combinations
are explained at selection time. Safety presents land detection, shoreline
detail and margin, boundaries, and the enhanced chart/depth controls only when
the host actually supplies that service. Advanced settings are grouped in
collapsible **Sailing**, **Weather**, **Search**, **Hazards** and **Resources**
sections, with the original setting values and persistence retained.

The persistent action is **Add route** or **Save route**. A separate **Compute**
action becomes available when the route is valid. Unsaved edits are preserved
while moving to another page and confirmed on exit.

### Routes

Routes are readable cards containing endpoints, departure/arrival mode,
engine, forecast source, status, ETA and distance. One tap selects a card and
shows its available actions. A status strip shows queued/running/complete/
failed counts and progress. Primary actions are **Compute selected**,
**Compute all** and **Stop**. A per-card action sheet contains Edit, Duplicate,
Go to chart, Reset, Delete, routing status, multi-leg sequence, multi-leg
departure optimisation, group settings, and result/output actions. Long lists
support filter and sort; multi-select has an explicit Select button and a
visible selection count. Batch creation is a guided four-page flow for start
times, route pairs, boats and wind strengths, with a preview before generation.
Existing XML open/save/save-as stays under Tools > Route sets.

### Results

The selected completed route has a summary first: start, arrival, duration,
distance, engine, forecast coverage, land/chart safety status and any warnings.
Visible segments switch between **Overview**, **Chart**, **Table**, **Weather**,
**Statistics**, **Report** and **Plot**. Chart returns to OpenCPN with the
selected route highlighted; a compact route inspector can reopen from its
toolbar button. Table and report are scrollable and retain their existing
values, with dense table columns presented as per-leg details on portrait.
Plot and crossover/polar views use touch-sized selectors rather than tiny
notebook tabs. Cursor and route-position inspectors remain accessible here.
Output actions are **Save as OpenCPN route**, **Save as track**, **Simplify**,
**Export GPX**, and combined multi-leg output when applicable. Completion and
failure both show the reason and the next useful action; a failed/partial
route is never offered as a validated result.

### Tools

**Positions** manages named coordinates, OpenCPN waypoints, boat-position
updates and deletion. **Boats & polars** covers boat files, polar plotting,
grid/dimensions/measurements/generation and crossover data. **Route sets**
opens/saves XML. **Display** includes route colours/thickness, isochrones,
alternates, wind/current/comfort overlays and table columns. **Resources**
contains shoreline download/status, cache/thread settings and host chart
safety capability. **Help** provides information, manual, about and a short
first-run guide. No desktop View or Help menu is required.

## Function preservation contract

| Existing function family | Android destination |
| --- | --- |
| New/edit/delete positions, update boat position | Tools > Positions and Plan endpoint picker |
| New/edit/delete/reset/go-to routing, filter, compute/stop | Routes cards and actions |
| Departure optimisation, arrival planning, batch, multi-leg | Plan > When and Routes guided actions |
| Quick/Standard/Professional and all route constraints | Plan editor and grouped Advanced settings |
| GRIB, climatology, currents and forecast time | Plan > Weather, linked to xGRIB |
| Boat/polar editor, plots, crossover, generation | Tools > Boats & polars |
| Statistics, report, plot, route table, cursor/route position | Results segments |
| Tracks, routes, GPX, simplification, combined output | Results > Output |
| Configuration XML open/save/save-as | Tools > Route sets |
| Display settings, shoreline, chart awareness, help/about | Tools |

The command inventory in `WeatherRoutingUI.h` and the full set of current
configuration fields are the functional checklist. No item may disappear
because its desktop menu or context menu is removed. The routing engine,
serialisation, chart overlays and xGRIB plugin-message interface remain
shared with desktop unless a measured Android constraint requires a guarded
platform implementation.

## Architecture and delivery

Keep the current 1.18.4 model and routing engine. Add an Android-only
presentation layer, selected by `__OCPN__ANDROID__`, with one controller
command per user action and shared validation/persistence. Decouple the
essential route commands from wx menu/list events so both desktop and Android
call the same logic. Keep route calculation off the UI thread and expose
cooperative cancellation and stage/progress feedback. Cap concurrent work and
cache admission using actual device memory; a resource rejection must be
reported before a job starts. Do not rely on executing helper binaries from
Android writable storage.

Build an arm64 OpenCPN 5.14/API 1.21 package with an Android import archive
and metadata, plus a separate CircleCI Android job in the same validation
workflow as Linux, Windows, macOS and Flatpak. Packaging must include the
toolbar icons and GSHHG shoreline manifest/data and must validate architecture,
required runtime symbols, metadata and archive contents. Android installation
must preserve current route XML; tests use a copy and restore saved user data.

## Device acceptance gates

1. Package imports through OpenCPN and survives cold restart with xGRIB active.
2. Portrait, landscape, keyboard, Android Back, font scale and small-screen
   layouts have reachable actions and no clipped controls.
3. New route from chart/waypoints/saved positions; edit, duplicate, save,
   reopen, delete and recovery after restart work.
4. Each engine computes a known GRIB-backed route; absent wind/current/wave
   coverage, land endpoints and unavailable enhanced chart safety explain
   themselves before or during computation.
5. Departure optimisation, planned arrival, batch and multi-leg routes work;
   stop and retry leave coherent states.
6. Every Results segment and every output form opens, scrolls, saves and
   reopens; exported GPX is checked independently.
7. Boat/polar editing and every Tools page operate with touch and Android
   storage selection.
8. Route table, overlay, selection and chart-pick return path work in OpenCPN.
9. Background/foreground, orientation change and low-memory cancellation
   preserve data and do not leave a stuck progress indicator.
10. Desktop regression suite and Android build/package checks pass; a clean
    arm64 package is tested on the attached tablet after each major UI slice.

The development APK supports realistic testing here. A production APK and a
second form factor remain separate release gates because their plugin manager,
file chooser and available memory can differ.
