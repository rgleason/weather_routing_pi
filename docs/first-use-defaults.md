# First-use routing defaults (local development)

The Santa Cruz–Kāneʻohe test exposed repeated GRIB timeline reconstruction
with Quick's 64 MiB cache. At the diagnostic snapshot, Quick had made over
10,700 frame requests for only 54 distinct timestamps; roughly 22.5 minutes
of its first 24.25 minutes were spent servicing GRIB requests. Main had
completed the same endpoints at 3-hour/10-degree sampling in 7 minutes 27
seconds with a 2,061 MiB cache. These were different search settings, so this
is evidence of a cache bottleneck, not a controlled engine speed comparison.

## Candidate defaults

| Setting | Main | Quick |
| --- | --- | --- |
| Preferred GRIB timeline cache, 64-bit process | 2,048 MiB | 2,048 MiB |
| First-use true-wind-angle range | 40–160° | 40–160° |
| Heading separation | 10° | 10° (adaptive) |
| Shoreline detail | Intermediate (2) | Intermediate (2) |
| Time step (unchanged) | 1 hour | 3 hours offshore (adaptive) |
| Search memory budget (unchanged) | Existing resource policy | 256 MiB |

Cache sizes are ceilings, allocated on demand. A larger cache requires enough
available physical RAM to hold it and leave 2 GiB plus twice its size free.
For example, 2,048 MiB needs 8 GiB available; 1,024 MiB needs 5 GiB;
512 MiB needs 3.5 GiB. When the requested size cannot be admitted, the policy
steps down through smaller allowances. Runtime memory guarding also applies
to a reduced allowance above the historical floor.

The historical low-memory/unknown-memory floors remain 512 MiB for Main and
64 MiB for Quick. The 32-bit defaults remain 192 MiB for Main and 64 MiB for
Quick, with a 192 MiB maximum. This does not promise that a large global GRIB
will route efficiently on a low-memory device.

Saved settings take precedence, including explicit 64 MiB caches, Crude
shorelines and older sampling settings. An existing installation must change
these settings explicitly to trial the new defaults. Balanced search presets
are revision 2 and reset heading separation to 10°; reset preserves cache
budgets, wind-angle constraints and shoreline choices.

The 40–160° range constrains sailing headings and is not appropriate for every
boat. It remains editable. Intermediate shorelines omit small coastal features;
nearshore routes can still require High or Full detail.

Before release, compare the Pacific case with identical endpoints, weather,
polar and shoreline: first change only Quick's cache from 64 to 2,048 MiB,
then compare 20° and 10° headings. Record runtime, cache reloads, completion,
arrival time and memory. Policy and persistence tests cannot establish these
end-to-end performance results. Version bump and publication are deferred.

## Local validation

- Linux xWeatherRouting plugin and test executable build successfully.
- All 303 CTest cases pass, including cache admission at simulated 32-bit and
  64-bit process widths, low/unknown RAM, saved-settings preservation, shoreline
  checks, routing and arrival-planning regressions.
- The running OpenCPN installation has not been replaced. Pacific-route timing
  with the revised defaults and GUI verification remain release checks.
