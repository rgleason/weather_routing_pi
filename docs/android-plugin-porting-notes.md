# Notes for future OpenCPN plugin ports to Android

These observations come from the Samsung SM-X210 Android 15 tablet running
the OpenCPN 5.14 development app, the earlier xGRIB port, and this
xWeatherRouting port. Recheck each point on the intended release APK.

## Device and testing access

- ADB access to the USB-attached tablet works when invoked outside the
  workspace command sandbox. `adb devices -l` identifies it as `SM_X210`.
- The development app is `org.opencpn.opencpn.dev`; the Play Store app is
  `org.opencpn.opencpn`. Both were initially installed with the same launcher
  name and icon; the stock 5.10.2 app was removed at the user's request. Always
  specify the package and test the actual home-screen shortcut. The development
  app has `run-as` access to its private plugin
  library directory, `manPlug/`.
- Android 15 denies ordinary `adb pull` for app-specific external storage.
  `adb exec-out run-as org.opencpn.opencpn.dev cat <path>` can back up a file,
  and `adb exec-out run-as ... tar -cf - -C <files-dir> <subdir>` can back up
  plugin assets and user data. Check byte counts and hashes before installing.
- `adb shell wm size` reports the natural display size, not necessarily the
  current rotated canvas geometry. Use `GetCanvasByIndex(0)->GetClientSize()`
  and respond to rotation while the workspace is open.

## Host and user interface

- OpenCPN's Android host uses Qt 5 with wxQt. Floating frame menu bars and
  captions may be unavailable; build navigation and Close/Save/Cancel controls
  inside plugin content. Do not require right click, hover or a desktop menu.
- The bundled wxQt predates `wxWindow::FromDIP`; use a small platform
  compatibility function for code shared with newer desktop wxWidgets.
  Confirm actual touch target size on the device with screenshots.
- The Android software keyboard and orientation changes can cover modeless
  dialog actions. Put actions above scrollable content or in a persistent
  footer, and test both orientations while typing.
- wxQt modal return codes have previously been unreliable in xGRIB. Use
  explicit acceptance state for decisions that must distinguish Save from
  Cancel, and verify by reopening the view.
- Visual checks on a running device matter. A clean compile and static UI
  contract test do not detect truncated list columns, invisible dialog
  captions or covered buttons.

## Build and package

- Match the installed APK to the OpenCPN Android core library, API and Qt/wx
  support archive. The development app here is OpenCPN 5.14.0 arm64 and uses
  plugin API 1.21. Record the installed plugin version rather than assuming the
  partially ported 1.17.12 baseline is still active.
- The local Android NDK is 26.1.10909125. Set `NDK_HOME` explicitly; the
  inherited Android toolchain otherwise searches `/opt/android/ndk`.
- Build the OpenCPN `lunasvg` target before `gorp` in a fresh core build.
  NDK 26 has `llvm-ar`; old scripts using `aarch64-linux-android-ar` fail.
- The Qt 5 headers need `_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS` when the
  plugin is compiled as C++20. Older wxQt `wxString::FromUTF8` overloads need
  a `const char*`, such as `string.c_str()`.
- Android cannot execute a helper installed in writable plugin storage;
  in-process native code or a host service is required. xGRIB's Android
  generator follows that rule.
- A CPack tarball alone is not a manual-import package. Put `metadata.xml`
  at archive root, verify the target/API/name/version, library identity and
  required data, and then test import through OpenCPN's plugin manager.
- Build the Android identity from the same source commit and CircleCI
  workflow as the other platforms. For xWeatherRouting, set
  `WEATHER_ROUTING_XWEATHER_IDENTITY=ON` and verify that the archive contains
  `libxweather_routing_pi.so` and matching plugin data/metadata.

## Integration and runtime

- xGRIB is installed and loaded on this device. The bundled GRIB plugin is
  present but skipped because it is disabled. Check the OpenCPN log for this
  state rather than assuming installation implies activation.
- Preserve the plugin message protocol for weather timelines; a route
  calculation must be tested against a real xGRIB-opened file, including
  wind, optional waves and optional currents at the route time and area.
- The stock OpenCPN 5.14 Android development app lacks the optional enhanced
  chart-safety host service. Keep GSHHG land checks available and explain why
  chart/depth enforcement controls cannot be used. Never imply GSHHG supplies
  charted depth.
- Package Crude/Low/Intermediate GSHHG and its manifest; High/Full remain
  explicit optional downloads. Test an endpoint on land as well as a safe
  route to prove the checks actually run.
- Large routing searches share RAM with OpenCPN charts, xGRIB and caches.
  Tablet resource admission, cancellation and progress are user-facing
  features, not merely engine implementation details.

## Tests still needed for this port

Record the exact package hash and app version for each tablet install.
Exercise every action in the Android UI, portrait/landscape, keyboard,
Android Back, foreground/background, xGRIB forecast handoff, saved data,
computation and output. Recheck on a production APK and another screen size
before claiming broad Android support.

## wxQt details found during the routing redesign

- A control can display a typed value while its wx event or cached value is
  unchanged. Qt spin boxes needed explicit `valueChanged` bindings and
  `interpretText()` before dismissing a sheet. Reopen the dialog and inspect
  the saved configuration; a screenshot alone does not prove persistence.
- Block updates while filling forms programmatically. Qt emits changes from
  some setters. Bulk editing must retain unedited endpoint types, times and
  engine-specific settings independently for every selected route.
- Generic date pickers lost month/day information with locale formatting, and
  wx time conversion applied daylight saving twice. Use explicit calendar and
  time selectors and the host's Qt time-zone database for Android conversions.
  Serialize unambiguous ISO/epoch values internally; requiring users to type an
  exact ISO timestamp caused avoidable generation failures.
  Test UTC, local time, DST ambiguity and a nonexistent local time separately.
- Font changes do not reliably recalculate existing QLabel heights. Polish
  the native widget and measure wrapped text using its actual QFontMetrics.
  A larger font on a fixed desktop-height label can silently clip whole lines.
- Style the popup item delegate as well as the collapsed combo. Large closed
  controls can still open a list of tiny rows. On this 240 dpi tablet 48 dp is
  72 physical pixels; the compatibility helper alone did not scale by density.
- Native checklist indicator CSS enlarged the box without moving the text,
  causing overlap. Ordinary touch-sized checkboxes backed by the same model
  avoid that problem. A generic wx list is not necessarily a Qt item view.
- On this host, reading hidden wxListCtrl columns with GetItemText returned
  the first-column value in the position view. Read the domain model for
  Android cards and retain measurement values independently of table text.
- Update completed results in the domain model even when a filter hides their
  rows. A completion notification confined to visible rows left distance and
  weather unavailable after the filter was cleared, despite a valid route.
- Detaching a desktop sizer does not hide widgets left outside the new layout.
  Reparent controls into their new panels and hide obsolete static lines,
  boxes, footer buttons and intermediate notebook containers explicitly.
- Qt modal sheets did not consistently deliver the wx show event used for
  automatic fitting. Give newly created modal sheets their initial geometry
  before ShowModal, then continue to handle rotation with weak references.
- Android Back needs a Qt event filter, including the matching key release.
  Dismiss an owned popup before cancelling its sheet; ignore other windows.
  Apply/Save and Back/Cancel must have
  deliberate semantics; do not accidentally save a palette selection on Back.
- Defer rebuilding cards until a clicked native button's callback returns.
  Check model ownership before dereferencing row pointers during refresh.
  Timers can run inside native modal loops and during deletion; detach an
  object from its owning collection before destroying it.
- Audit Android preprocessor guards for disabled business logic. The earlier
  boat editor compiled out polar reorder and removal. A visible enabled button
  is not evidence that the operation is implemented.
- Keep desktop placeholders out of the Android flow. The desktop polar
  editor's boat-characteristics generation subpage and NMEA import button
  have no handlers in this baseline. They are separate missing desktop
  features, not working functions to claim as tested in a port.
- Remove inherited dialog-wide pan gestures before adding touch scrolling.
  On this host a swipe on a child page dragged the entire routing workspace.
  wxQt scroll helpers need explicit QScrollPrepareEvent geometry and QScrollEvent
  positions when using QScroller; use the wx scroll-unit API to move content.
  Verify a content swipe on the device, including drags beginning over controls.
- Preserve model pointers and selection across native column replacement.
  Walking the complete model list to repopulate filtered rows can attach the
  wrong route to a row. Android sorting can reorder cards while retaining the
  controller's row indexes for selection and commands.
- wxStaticBoxSizer owns its static box. Replacing a sizer can destroy that box
  and the model controls still parented to it. Detach and reparent **hidden**
  controller controls too. Hide obsolete boxes before deleting the sizer; do
  not retain raw child pointers and access them after replacement. A device
  crash in EditPolarDialog's constructor exposed this ownership rule.

- This wxQt build does not reliably return a button caption from GetLabel().
  Store action roles and palette RGB identities separately. Comparing captions
  disabled routing status during computation and generated empty swatch styles.
- When converting two-column forms to stacked fields, expand every sizer item.
  Wrapped labels with a zero minimum width and no wxEXPAND became invisible,
  while their vertical space remained. Inspect both labels and updated values.
- xGRIB uses the host Android file chooser through a JNI worker adapter.
  A blocking GUI-thread JNI call can deadlock with the Android keyboard.
  Weather Routing also exposed a separate problem: a native chooser can own
  input while hidden behind a wxQt plugin sheet. Its touch browser therefore
  stays inside Qt, uses app-accessible folders and supports explicit selection
  of several polars. Test visibility, keyboard Back, new files/folders, multiple
  selection and overwrite refusal. Do not equate a chooser return with a
  successful file operation; inspect the file and the reopened model.
- Wait for the plugin's autosave timer before checking persistence. This port
  saves route XML five seconds after a change; an immediate file read can show
  stale data even when the operation has succeeded.

### Read-only refresh must not invoke command warnings

Use the controller's silent selection accessor when refreshing availability.
`CurrentRouteMaps(true)` opens a modal warning when selection is empty; calling
it from a periodically refreshed Android workspace can create nested modal
loops. The Android result-enablement refresh uses `CurrentRouteMaps(false)`.

## Hidden forecast provider lifetime

Closing a weather panel must preserve the forecast supplied to other plugins.
The old Android xGRIB Close action destroyed its controller. Weather Routing’s
next timeline request recreated it using the newest-file preference, silently
switching from the explicitly opened Irish forecast to an Atlantic forecast.
The Android fix retains the hidden controller. Test this exact sequence with
two forecasts of different geographic coverage, and distinguish panel hiding,
plugin unload, explicit forecast selection and startup loading policy.

## Verify scheduled instants in exported data

An optimisation screen can display three successful calculations while all
three use the same instant. A current-time template passed its flag into each
fixed-departure clone; route startup overwrote the offsets with Now. Snapshot
Now once when creating a batch and clear automatic current-time updates on
its candidates. Compare actual departure timestamps and route fingerprints
in the computation log as well as displayed offsets. The same principle
applies to arrival searches, multi-leg continuation and generated batches.


## GLES rendering and crash evidence

On Adreno, the bundled piDC GLES2 four-point DrawPolygon path enabled a
vertex attribute using a colour **uniform** location, then provided a short
stack colour array. Stability cells crashed in the driver. Keep uniforms and
attributes distinct; use owned vertex buffers, and restore program, uniforms,
buffer bindings, attribute pointers/enabled states and blending after drawing.
Test overlays with chart pan/zoom/rotation and neighbouring plugins active.

The development app can restart automatically after a native crash. A nonempty
pidof result alone does not prove continuity. Capture PID before and after,
check the foreground package, and read the full logcat crash buffer. Match
addr2line to the exact installed binary; retain the binary before rebuilding.

wxQt SetItemState(-1, ...) did not clear previous comparison-row selection.
Update each row explicitly, then verify the selected chart route as well as
the visible comparison fields. Scrolling live fields is more reliable when
a wxScrolledWindow moves one content panel instead of individually updated
labels and inputs; verify that content moves as well as the scrollbar thumb.

## Native file-picker thread handoff

The installed host's `QtActivity.FileChooserDialog` uses a blocking Java
`CountDownLatch` for app-owned files. After editing a Qt text/spin field,
Android's main thread can call `finishComposingText` while the chooser gains
focus; Qt implements that call with a synchronous invocation on its GUI thread.
Calling the blocking picker on the Qt GUI thread therefore deadlocks. The ANR
trace, rather than a slow USB command, identifies this circular wait.

xWeatherRouting calls only the Java picker/polling methods on a JNI-attached
worker and runs a nested Qt event loop on the GUI thread. No wx/Qt widgets are
used on the worker. This supports both the legacy blocking result and the
asynchronous scoped-storage result. Test Open, Add, Save As and Cancel after
editing text, with the keyboard showing and hidden. Native Java dialogs left
behind by `hide()` in the host are also visible in `dumpsys activity lastanr`;
they are not sufficient evidence of a new plugin window.

## Android modeless workspace surface

A wxFrame carrying desktop Tool/float-on-parent flags could report `IsShown()`
and Qt `isVisible()` yet not appear after an Activity cold restart on this
host. The xGRIB wxDialog rendered normally during the same run. Setting the
Android routing frame to `Qt::Dialog | Qt::FramelessWindowHint` restored its
composited surface after restart while retaining in-content navigation.
Inspect a screenshot and test tapping controls; logging a visible flag is
insufficient to verify window visibility. Modal wxQt sheets can also miss wx
show and parent-size notifications during rotation. The shared Android sheet
helper now listens to native Show, canvas Resize and QScreen geometry changes,
then fits after the host layout settles. Scrollable notebook pages need a zero
minimum viewport height and refreshed virtual extent; otherwise their desktop
best size can put an action below a landscape screen with no usable scroll.
Verify both rotation directions, keyboard visibility and header actions. This flag adjustment is Android-only.

Reports also need a native scrolling document surface. The wxHtmlWindow paint
surface left overlapping text after touch scrolling on this wxQt host. Qt
QTextBrowser, embedded in full-height report pages, preserves the shared report
generation and renders cleanly in both orientations. Check a long report that
actually requires scrolling as well as a short report that fits the viewport.

## Preserve instants across XML and host route APIs

Store UTC epoch seconds alongside legacy local date/time fields. Android wxQt
ISO parsing applied the DST offset incorrectly when reopening a saved route
set. Qt local ISO parsing fixes legacy files; explicit epoch fields preserve
new files across device timezone changes. Test saved departure and planned
arrival against independent epoch conversion and a cold restart.

OpenCPN's legacy AddPlugInRouteEx takes the final waypoint creation time as
planned departure. API 1.21 HostApi121::Route carries planned departure and
per-waypoint leg speed/ETD explicitly. Verify the host database and route
manager after output, as a successful insertion message does not prove times
are correct. Preserve the host API's UTC representation conventions.

## Swipes beginning on buttons

Qt can interpret the release after a drag over a button as a click, even when
the containing page supports kinetic scrolling. Track touch and mouse movement
against the drag threshold, forward the gesture to the nearest scrolling
parent, and suppress button activation after a drag. Keep stationary taps
working. Verify selection survives swipes beginning on Edit and action buttons.

## Android message boxes and GPX serialization

On this host, Java message-box Back could hide the alert while leaving the
native waiter/input window active. QMessageBox also chose the Android native
platform dialog, so switching that class alone did not solve it. A Qt QDialog
sheet with explicit actions and Back handling keeps the entire modal lifecycle
in Qt. Verify Back followed by an actual workspace tap, including confirmations
where Back must mean No/Cancel.

Format GPX departure, ETD and point timestamps with an explicit UTC timezone.
Appending Z to FormatISODate/Time labels local time as UTC. Include point time
elements and compare against route epochs under a non-UTC test environment.
OpenCPN's route calculation stores the incoming leg's speed on its destination
waypoint; the API header description can be misleading. Inspect the host route
calculation and verify both ETD and speed association.

## RAM and resource budgets across platforms

Use available physical headroom rather than total advertised RAM or swap/ZRAM.
This tablet exposes approximately 3.3 GiB physical RAM, but measured headroom
was about 500 MiB while OpenCPN and other apps were running. Linux/Android
`MemAvailable` estimates headroom without swapping; Android can kill processes
under pressure before a native allocation throws. See the
[Linux proc documentation](https://www.kernel.org/doc/html/v6.15/filesystems/proc.html)
and [Android memory overview](https://developer.android.com/topic/performance/memory-overview).

The retention and runtime worker policies are shared by all builds. Windows
uses `GlobalMemoryStatusEx` physical headroom; Linux/Android use `MemAvailable`
and accessible cgroup v1/v2 limits, including parent groups. A known exhausted
limit must remain distinguishable from an unknown measurement. macOS uses
Mach free plus inactive physical pages as a conservative estimate, excluding
compressed/swap storage and avoiding double counting speculative/purgeable
pages; see [Apple VM statistics](https://developer.apple.com/documentation/kernel/vm_statistics64_data_t).
The Windows and macOS readers require native build/device checks; Linux and Android
builds do not exercise that conditional branch. Only first-use defaults and
unknown-memory fallbacks differ on Android; saved preferences are not rewritten.

- Android first-use timeline-cache preferences are 128 MiB. Existing requested
  limits remain persisted, while effective allowances can be smaller.
- Below 2 GiB available, timeline retention is limited to one eighth of current
  headroom, with a 16 MiB minimum. Chart tile retention uses one sixteenth.
  The shoreline tile cache also shrinks retention to one sixteenth of
  physical headroom. Its original per-tile allocation bound remains; a valid
  larger tile can serve a query without being retained. Geometry and chosen
  resolution remain intact. Runtime checks occur at most once per second and
  shrink retention rather than
  repeatedly growing and evicting it. Completed batches release timeline data.
- Automatic Android concurrency starts conservatively. Runtime scheduling
  reserves 256 MiB headroom and estimates at least 256 MiB per additional route,
  respecting larger engine allowances. A sampled allowance must not be reused
  to admit extra workers repeatedly before RAM is measured again. Existing
  routes continue; the queue waits for its next worker allowance.
- These are retention/scheduling budgets, not a hard limit for the complete
  process. Forecast-provider originals, graphics, charts, search frontiers and
  live frame references also use RAM. A single oversized timeline reply can be
  retained long enough to serve a request. Do not claim this prevents every LMK.
- Preserve forecast resolution and routing effort by default. Reducing cache
  retention and concurrency may cost time but does not discard coastal detail.
  Forecast coarsening needs a separate explicit choice, coverage and missing-
  value checks, and route comparisons; it is not a silent emergency shortcut.

## Additional touch and data checks

A wxQt choice can visually show a first item while its controller has no
selection. Synchronize the visible picker, controller and model explicitly,
including after opening a boat and switching position/waypoint endpoint types.
Use the visible Android polar selection for Remove, rather than hidden desktop
rows which may retain several selections.

A manually forwarded touch click can be followed by a synthesized mouse click.
Suppress that second sequence and distinguish a drag from a tap. Verify one
mutation per physical tap, especially Duplicate, Remove and batch generation.

Use touch progress with a visible Cancel button and Android Back. Keep original
batch templates after cancellation and document partial generated routes.
Fractional days/hours must use seconds consistently in preview and generation.

The stock host track API serializes local calendar fields followed by Z. This
port compensates at the Android API boundary and checks the resulting instant.
A UTC calendar falling inside the local spring DST gap cannot be represented
through that legacy API; refuse the affected track rather than saving wrong
times. GPX writes canonical UTC directly. API 1.21 route departure/ETD and speed
association require independent saved-object checks: segment speed belongs to
the destination point's incoming leg.

Native browser launch needs Qt QDesktopServices on this host. Render bundled
HTML inside a touch Qt view and verify the actual manual page, not only browser
launch. Modal sheets may need a queued raise after a wxQt chart refresh.

## Back dispatch, waypoint identity and inspectors

The Android Java host checks its registered top-level wx window count during
`onKeyUp`. Closing a sheet in a Qt Back key-press filter reduces that count too
early and leaks the host's exit toast. Latch the press, perform the dismissal
on release, and consume both events. Test Back from chart picking as well as
ordinary sheets.

Waypoint names are not unique. Store each GUID as combo item data and keep it
through section/time-zone refreshes. Coordinate labels help users distinguish
duplicate names. Do not resolve selection using an index into a freshly queried
host GUID list: skipped invalid entries can change the correspondence.

An inherited inspector declared a label that its constructor never created.
Its invalid-selection path dereferenced that pointer and crashed. Exercise
empty and multiple selection, then clear every dependent field so old weather
values cannot remain visible.

Modern cursor traces are inspection geometry, not validated routes. Preserve
the individual predecessor times; adaptive steps cannot be reconstructed by
dividing total duration evenly. Query available weather at those instants,
show missing values as unavailable, and label the preview honestly. Plot touch
readouts should sample the route at the selected x/time position, rather than
report the arbitrary y position of the finger.

The host Plugin Manager still uses its blocking native Java chooser. After
editing Qt text it can reproduce the host IME deadlock; a cold restart allowed
both import archives to install successfully here. Plugin-specific picker
fixes do not patch this separate host action. Record this host limitation when
shipping manual-import instructions.

## Empty models and file preservation

Test deleting the final entry, not just a middle one. The polar editor exposed
unsigned underflow in angle lookup and VMG access when no angle/wind entries
remained. The shared model now tolerates these editing states and reports
unavailable values. Validate before opening a file in write mode: rejecting
an empty polar after fopen("w") would already have destroyed existing data.
Use disposable copies and verify the original file bytes after refused Save
and Cancel.

On wxQt, changing a radio label before reparenting did not survive creation of
the new native control. Set its intended text at construction and check the
actual reopened screen. Cursor traces carry their own bounded predecessor
timestamps; use those for weather queries and the plot endpoint rather than
uniformly spreading layer time over sampled geometry. Label unvalidated search
previews explicitly.

## Modal rotation and scrolling content

This wxQt host can miss wx modal Show notifications. Listen to native Show,
canvas Resize and QScreen geometry changes, then fit after the host layout
settles. Weak wx references and QObject-owned timers prevent callbacks to
destroyed sheets. Check native visibility rather than only wx IsShown.

A wxScrolledWindow containing a single content panel scrolled reliably;
reparenting many controls directly into it left the landscape polar actions
unreachable even with a visible scrollbar. Give the viewport and notebook
pages zero minimum size, retain the content minimum, and update FitInside
after content/viewport changes. Test a swipe starting on a button too.
Recalculate wrapped QLabel minimum height after rotation; wordWrap alone
can render more lines into the old landscape height and clip them.

Audit menu reachability against the desktop command inventory. Shoreline
management compiled for Android but was missing from the replacement Tools
actions. Replace stock OK identifiers when an explicit Apply caption is needed:
this wxQt implementation substituted OK despite the supplied label.

## Repeatable workflow for the next plugin

1. Record the latest desktop commit and inventory every command, setting,
   calculation, output, chart interaction and persistent file. Mark placeholders
   separately from working functions. Map each working command to a visible
   Android action before implementing the new presentation.
2. Confirm ADB, screenshots, app logs, app-accessible storage and the installed
   APK/API/ABI. Back up binaries, settings and user data. Use disposable copies
   for destructive tests; prove ownership before deleting anything.
3. Design the Android navigation first. Group frequent tasks into a few pages,
   use focused scrolling sheets, explicit Apply/Save/Cancel and visible Back,
   and replace dense grids with editable cards or selectors. Keep calculations
   and file formats shared unless an observed platform constraint requires a
   guarded adapter.
4. Build and install a small vertical slice early: cold launch, first toolbar
   open, one edit, one calculation, one real output. This finds ABI, linkage,
   event-loop, storage and provider-lifetime problems before polishing every
   page. Test Plugin Manager imports, not just direct library replacement.
5. Exercise both orientations, keyboard open/closed, Android Back, app
   background/foreground, empty/final-entry states, invalid inputs, cancellation
   and repeated operations. Swipe over labels, buttons and blank space; an
   enabled control or visible scrollbar does not prove that an action works.
6. Verify results independently. Read saved files back; compare coordinates,
   units, UTC instants, identifiers and counts with the model. A screenshot of
   a success message is insufficient. Confirm original user files still match
   their backups after editing, failed Save and Cancel.
7. Watch physical headroom, cache retention, worker counts, responsiveness,
   crashes and ANRs under the actual host plus other enabled plugins. Reduce
   optional retained data and concurrency first. Preserve calculation accuracy
   and full safety geometry. Record effective limits separately from saved
   user preferences.
8. Keep platform adapters conditional and run the shared desktop regression
   suite after shared-model changes. Include Android in the same build/artifact
   matrix; verify library/data names, metadata, bundled assets and import archive
   layout. Validate packaging with the final binary after the last UI fix.
9. Finish with a cold start, enabled-plugin check, useful sample workflow,
   ownership-based cleanup, coverage table, exact package hashes, known host
   limitations and local commits. Distinguish tablet acceptance from untested
   APKs, screen sizes and remote CI.

## Celestial Navigation handoff (deferred)

The Celestial Navigation port has **not** been started. Its first session
should read this guide, establish its current desktop baseline and inspect its
own source, build instructions and command inventory. Do not assume that the
Weather Routing page structure fits a different workflow.

Before designing its UI, identify the order in which a navigator enters data,
checks units/time/reference frames, performs each calculation, reviews results,
and uses any chart or export actions. Preserve precision and validate the
shared calculations against known reference cases. Audit date/time input,
number entry, tables, plots, file/database dependencies and offline operation
on the target APK. Build one real calculation and saved/reopened result on the
tablet early, then design the remaining Android flows around those findings.

Reusable patterns from this port are in `AndroidDialogHeader.h`,
`WeatherRoutingMessageDialog.h`, `WeatherRoutingFileDialog.h`,
`WeatherRoutingBrowser.h` and `DeviceMemoryPolicy.h`; xGRIB's
`OpenCPNAndroidFileSelector.h` is the JNI chooser adapter. Review and extract
only applicable patterns rather than copying routing-specific controllers.
The source files are examples for a future shared helper library, not a new
host ABI or a promise that another plugin can include them unchanged.

## Background downloads and hidden input owners

OpenCPN 5.14's Android background download API also opens a Java busy
ProgressDialog. A Qt progress sheet can cover it visually while the Java dialog
still owns touch input: Back worked, but a visible Cancel button received no
taps. Dismiss the host busy circle through its existing activity method before
showing the plugin progress sheet. Keep the public download API's callback
cleanup, cancel semantics and checksum/format verification. Account for
DownloadManager removing a completed target during callback cleanup. Test both
real completion and active cancellation, and read files back independently.

This build targets 5.14/API1.21; the presence of an older production APK does
not establish ABI compatibility. Keep host version/API explicit in packages
and acceptance records. Identical launcher labels can send users to another
package with a separate plugin installation and navigation database.

Touch dropdowns need the same drag/tap distinction as buttons. A swipe over a
wxQt plot selector changed its selected quantity instead of scrolling. Forward
drags to the nearest touch viewport; open the dropdown only on a stationary
tap. Test actual selection, dismissal and a swipe over the control after adding
this adapter. Preserve editable combo text behavior separately.

### Match the CI packaging environment

Run the actual CI shell script locally with its pinned cached dependencies.
`OCPN_TARGET=android-arm64` changes the inherited CPack filename; a manual
configure without that environment produced a different archive name. Old
archives in a reused build directory can look current. Select only the archive
reported by the current CPack run, validate its embedded metadata and assets,
and compare the packaged library hash with the tablet-tested library. Here the
full local CI script passed and produced identical library bytes.

## Chart results and forecast coverage

- Keep chart-overlay visibility independent of whether a plugin sheet is open.
  On Android, Close and Back are normal ways to return to the chart. A visible
  completed route should remain drawn; transient selection/isochrone previews
  can have a separate lifetime. Exercise Close, Back and the visibility toggle.
- A comparison sheet's selection may belong to a separate hidden wxListCtrl.
  Provide a direct Show on chart action, map its selected result to an owned
  route, select that route in the main workspace, and frame it on the chart.
  Handle filtered candidates and stale pointers. Avoid drawing every generated
  departure candidate automatically.
- Show Departure or Arrival deadline explicitly on route cards and summaries.
  A stored departure time alone is misleading for an arrival-planning route.
  Short dynamic headings also avoid wxQt labels retaining an old narrow width.
- Check wind fields' actual geographic and time coverage, not just a merged
  GRIB's overall duration. A file can include 96-hour wave/current data while
  wind ends at 54 hours, or a bounding box can end just short of the destination.
  Inspect each field's GRIB metadata and allow space around the search area.
- Retain user settings and navigation data before changing installations.
  `pm uninstall -k` removes an explicitly unwanted host app while preserving
  its data for recovery. Back up the APK and readable navigation files as well;
  a production app usually has no `run-as` access.
- Staging public Downloads with `cp` under `run-as` can fail because the source
  is inaccessible. Streaming the local package through `adb exec-in run-as ...
  sh -c 'cat > <app-owned-path>'` worked; verify the installed library against
  the stripped library inside the package, not an unstripped build output.
- Do not type into the tablet while the user is editing it. A screenshot can
  become stale between a read and the next input, and tab changes can redirect
  a coordinate edit into an unrelated field. Resume input after the user finishes.

## Date/time, credentials and modal Back (xGRIB 0.3.2 follow-up)

- Replace exact-format timestamp typing with a calendar and touch hour/minute
  controls. Keep UTC explicit, serialize the request internally, and test both
  acceptance and cancellation under a summer timezone. A Current UTC action
  should set a useful forecast hour, not accidentally use local time.
- A dropdown's stylesheet or model size hint may be ignored by the pinned Qt
  style. An item delegate with an explicit minimum row height worked. Exercise
  tap selection, swipe scrolling, Back dismissal and both rotations on-device.
- Remember passwords only as an explicit option. xGRIB uses an installation's
  nonexportable Android Keystore AES key, AES-GCM with a fresh IV, and the
  username as authenticated additional data. Only ciphertext/IV and the account
  identifier are written to preferences. Forget deletes both the key and record;
  changing accounts clears an automatically restored password. Do not log Java
  exceptions or plaintext credentials. Run Keystore IPC on a worker and test
  cold restart, opt-out, account changes and significant password whitespace.
  A copied configuration cannot transfer its key to a new installation.
- Reference Android's [Keystore guidance](https://developer.android.com/privacy-and-security/keystore)
  and [KeyGenParameterSpec API](https://developer.android.com/reference/android/security/keystore/KeyGenParameterSpec).
  Fail closed when secure storage is unavailable; do not introduce a plaintext fallback.
- OpenCPN's Android activity checks its wx top-level window list before it
  forwards Back key-up to Qt. xGRIB's private static wx library has a separate
  list because `--exclude-libs,ALL` is needed to isolate TLS. Resolve the already
  loaded `libgorp.so` explicitly (its local linker scope is not necessarily in
  `RTLD_DEFAULT`) and register only owned sheets with the host's matching wx ABI.
  Unregister on destruction; preserve the TLS symbol-isolation gate.
- This pinned wxQt ShowModal path displays QDialog without updating wx's shown
  bit. Synchronize owned Qt Show/Hide events with wxWindowBase visibility so
  the host counts the open sheet. Consume both halves of Back, then dismiss
  the keyboard, owned popup or current sheet in that order. A nested popup
  must not close the generator or start the host's double-Back exit sequence.
  Test host PID continuity and actual window counts, not just screenshots.
- Treat these bridge helpers as pinned-host adaptations. Review the next
  plugin's linkage and host ABI before copying them, especially for future
  Android versions using predictive Back dispatch instead of legacy key events.
- Scope a Back filter before its wxDialog is destroyed. Parenting it only to
  QDialog is too late: wx destruction can invalidate its vtable while Qt still
  delivers events. Stack filters around ShowModal and an Impl-owned filter for
  a persistent dialog passed nested Back, explicit Close and re-opening.
- This Qt Android combo popup receives synthesized mouse events. QScroller's
  TouchGesture let a swipe select the released row; LeftMouseButtonGesture with
  pixel scrolling kept the popup open and scrolled correctly. Verify the input
  stream on the actual control rather than assuming all Qt widgets use Touch.
- Recheck repository HEAD before the final build and commit. A user can commit
  a shared-engine fix while tablet UI work is underway. The final 1.18.5 build
  was rebuilt on f0a2546, its five new polar-range tests passed, and the coastal
  routes were rerun. Keep earlier fingerprints as historical evidence when an
  intentional engine policy change legitimately changes the new results.
