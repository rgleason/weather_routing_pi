# Meridian: Donegal coastal guidance correction, 23 November 2026

Meridian in production 1.44.0 and development 2.11.0 now guides medium-length coastal passages towards their actual destination. Its display name is **Meridian** throughout the UI, progress, help and package descriptions. The established six search engines and saved selections are unchanged.

## Confirmed cause

The saved Donegal Bay–Mouth of Lough Foyle passage is 70.759 NM directly, with land between the endpoints. The previous advisory guide used an artificial target 60 NM along that bearing. That point is in water and outside the 0.4 NM margin, but the bounded geographic search could not connect to it. An empty guide then ranked sailing proposals mainly by direct distance, favouring repeated approaches towards the blocking coast.

For passages up to 160 NM, the guide now uses the actual destination. The Full GSHHG probe changes from zero guide vertices to 21, describing a 119.272 NM detour around Donegal. Longer hierarchical guidance is retained. The guide only orders work: every sailing leg still needs wind/current integration, safety checks and independent chronological validation.

## Matched real-weather replay

Both branches use the saved 23 November 2026 12:12:38 UTC departure, Nicholson polar, 95% wind scale, 300-second tack/gybe penalties, 600-second sail-change penalty, currents enabled and 0.4 NM land margin. Production and development use external xGRIB 0.4.6 with the same real GRIB. Native chart controls also use the working OpenCPN host, licensed vector-chart provider, copied chart catalogue, 3 m minimum depth and accepted global layer (63,103 proven + 2,433 detailed regions). Each replay has a private profile.

| Control | Before | Corrected | Calculation phase before → after | Corrected final search |
|---|---|---|---:|---:|
| Production, Full GSHHG | Allowance exhausted; 58.225 NM short | Validated arrival; 119.101 NM sailed | 55.454 → 4.058 s | 1.242 s |
| Development, Full GSHHG | Allowance exhausted; 58.225 NM short | Validated arrival; 119.101 NM sailed | 31.872 → 3.638 s | 1.276 s |
| Production, native chart-aware | Allowance exhausted; 61.162 NM short | Validated arrival; 119.365 NM sailed | 25.501 → 15.516 s | 3.256 s |

Development's native chart-aware control also completed: 13.437 s calculation phase, 3.252 s final search. Both branches have identical waypoint times and positions agree within 1.4e-9 degrees. GSHHG arrival is 24 November 11:02:57 UTC; chart-aware arrival is 11:12:11 UTC. All four corrected Donegal tracks have 87 segments, independently checked against Full GSHHG: zero land crossings and zero 0.4 NM margin violations.

The calculation phase includes preflight, preparation, scouting, provider waits and final validation, excluding application startup. The final-search column excludes the preliminary chart scout. Timing is one observation per configuration, not an average. Because the baseline failed, these are successful-recovery measurements, not a claimed before/after solve-time ratio. Auto and All retain their other selected method attempts.

Currents remain enabled. The results explicitly report climatology-current and zero-current fallback in parts before/around forecast current coverage, plus estimated wave heights. The correction does not manufacture forecast coverage.

## Validation and scope

- Full enabled native suites: 737 production and 827 development tests passed; one timing benchmark is intentionally disabled in each suite.
- CI/tooling: 89 production and 73 development tests passed.
- All 13 Meridian tests pass in each branch, including a new medium-distance obstacle test whose artificial 60 NM target is inland. It requires a safe geographic detour and independently validated exact arrival.
- All 196 qualified calculation inputs match the development freeze contract; the permitted Meridian inputs are requalified. Existing solver source trees have no changes in this patch.
- Saved Lizard–Provincetown real-GRIB control, production: complete, 24.217 s calculation phase. The complete timed track exactly matches the earlier Meridian v9 control in this branch. This is a regression control, not a new objective-table speed comparison.
- Saved Lizard–Provincetown real-GRIB control, development: complete, 24.276 s calculation phase. The complete timed track exactly matches the earlier Meridian v9 control in this branch. This is a regression control, not a new objective-table speed comparison.

## Relationship to LuckGrib

LuckGrib documents retaining short time steps when land intersections are frequent and increasing them in open water ([adaptive time steps](https://routing.luckgrib.com/manual/settings/time-step.html)). It also documents searching all directions, including initially away from the destination ([search width](https://routing.luckgrib.com/intro/isochrones/search.html)). These pages do not establish the exact one-sided collision-steering rule described in the discussion.

The useful principle is to let obstacles influence search ordering while preserving alternative ways around them. Meridian already has a full-circle heading fan, clearance-dependent coastal steps and advisory geographic detours. This fix restores the missing geographic detour for the observed case. An additional one-sided rejection heuristic is not introduced without evidence of a remaining benefit; the real-weather replay now completes with roughly 9,400–10,000 generated proposals instead of exhausting two million. Time-dependent wind/current failures must not become permanent land exclusions.
