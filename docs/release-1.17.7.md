# Weather Routing 1.17.7

Adds the optional Quick engine alongside Main. Choose the engine in Basic and
configure it in Advanced. Existing 1.17.6 routes and saved defaults stay on Main
with their tuning preserved; each engine remembers its own settings.

Advanced uses two columns: Engine settings, Constraints, Cyclone avoidance,
Motoring and Courses on the left; Options and Polar Efficiency on the right.
Options starts at the top, and cyclone avoidance has its own compact group.
All earlier Basic and Advanced controls remain available in their relevant
engine or compatibility mode; this rearrangement does not reset saved values.
Departure-time route workers and Optimize Tacking remain editable for Main and
Quick. The shared scheduler uses the worker setting, subject to global and
chart/boundary limits; the shared polar evaluator uses Optimize Tacking.

Quick reduces retained search state and calculation work using a separate C++
bounded beam search. It shares Main's physical propagation and independent final
validation. Its pruning can miss a feasible or faster route. Its configurable
search-memory budget is not a cap on total OpenCPN memory.

Presets apply only through Reset engine to preset and an Apply/Cancel preview.
The initial Balanced preset resets search tuning only. Memory budgets, boat,
weather and safety preferences remain intact. Manual tuning is shown as Custom.

Results identify the engine used; headless output also records search settings
and preset revisions. An unsupported saved engine is reported explicitly.

See [engine settings and limits](quick-routing.md) for details. Release validation
is recorded separately in the local 1.17.7 implementation report. Native Windows
32-bit and 64-bit full-plugin qualification is required before claiming support
or a fix for the reported Windows memory problem.

Advanced now offers shoreline resolutions 0–4 (Crude, Low, Intermediate, High,
Full), with all five GSHHG 2.3.7 datasets bundled for offline use. Main preserves
an existing route's setting, or inherits the previous global Full/High preference
when migrating old XML. Fresh Main defaults to Full. Quick starts at Crude and
remembers its own manually selected resolution. Engine switches and search preset
resets preserve both shoreline choices.

On a compatible enhanced OpenCPN core, enabled and enforced chart safety changes
this control to Scout shoreline resolution. Its separately saved choice starts
at Crude and remains editable for finer coastal scouting. Chart geometry and
depth checks remain authoritative. Disabling chart enforcement restores the Main
or Quick shoreline choice. Stock OpenCPN keeps all five resolutions available;
loading charts alone does not provide the enhanced core API.

Lower shoreline resolutions omit smaller coastal features and can permit routes
through land visible in a more detailed dataset. Search speedups depend on the
route; shoreline resolution is not a guarantee of clearance. Calculation logs and
headless results record the effective resolution and dataset identity.

View / Shoreline data manages the bundled datasets and per-dataset tile-cache
limit. Its default selection is used for importing older routes; it does not
change existing route selections. Dataset repair is available offline.

Main can use a small separate arrival allowance when its forward search reaches
the normal work limit close to the destination. This can finish a coastal
approach without exhausting recovery stages and restarting at a higher effort.
The allowance is 10% of the original forward budget, added to the total work
ceiling; the original fallback allocations and retained-state limits are
preserved. Already generated arrival candidates are also checked at a budget
boundary. Normal motion checks and independent final validation still apply.

Short shoreline checks wholly inside one spatial bin now avoid repeated work
while retaining the same intersection and land-containment tests. The older
reader's missed crossings are not restored for speed: matching 1.17.5 settings
and resolution does not promise identical geometry under the corrected reader.
