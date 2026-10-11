# xWeatherRouting 1.28.1 — bounded polar wind policies

A single boat polar beginning at 6 knots previously prevented routing through
lighter forecast winds. Quinton's Hanalei Bay Offshore to Kaneohe Bay Offshore
route exposed this: the default demo boat failed outbound and completed in
reverse as it encountered different winds at different times.

The September impossible-speed correction remains. Routing now applies
separate low- and high-wind policies through the same lookup used for search,
sail selection and chronological route validation. Automatic tapers the
lowest-wind tables below the entire boat's supplied range, reaching zero at
zero wind. A single polar holds its final column at the current wind angle
above its range. Multiple sail tables retain strict upper ranges, and gaps
between their supplied ranges are not automatically filled.

Boat → Polars offers Automatic, Reject and Taper to zero for low wind, and
Automatic, Reject and Hold final speeds for high wind. Choices persist as
LowWindPolicy and HighWindPolicy attributes in Boat.xml. Existing files retain
automatic defaults. Unknown policy values are rejected. Use explicit Reject
for a sail's operating limit; select Hold explicitly for a general boat table
within a multi-polar boat.

Blank cells are missing measurements, interpolated only when supported by
neighbouring data. Unresolved blanks remain unavailable. Numeric zeros,
including 0 and 0.0, always specify zero direct sailing speed. This fixes the
legacy reader's inconsistent treatment of the first literal 0 as missing data.
Supplied zeros, decreasing speeds and normal in-range interpolation remain.
Optimised tacking may provide progress by sailing another angle. Sailing STW
cannot exceed the selected polar's highest recorded speed after efficiency
adjustments. Motor speed and current-driven SOG are separate. The actual wind
continues to govern weather and apparent-wind limits. Completed routes report
estimated polar performance when independent chronological replay uses a
tapered or held lookup. No wave-surfing bonus or guessed high-wind decay is
introduced.

Quick now retains a bounded stationary wait when no configured heading has
usable vessel performance. It can wait through zero wind and resume sailing
when the wind returns, both at departure and after making progress. Waits use
the shared safety checks and independent chronological replay; disabled waiting,
the maximum wait allowance and the route duration limit remain enforced.

Failed routes now say that a weather-source summary requires a completed
route, rather than reporting weather unavailable merely because routing failed.
The unchanged bundled boat/polar data upgrade from 1.24–1.27 does not prompt
to overwrite user data. Version 1.28's route display, clearing and Android
workspace behaviour remain included. Minimum host API remains 1.21.

Fractional wind angles now use the correct interval after a polar row,
including fractional rows and rows with explicit zeros. Previously the
whole-degree lookup could extrapolate from the preceding interval and produce
a negative speed beside a valid zero.

Local qualification passed all 449 tests and ten publication contract tests.
Seeded offshore endpoint pairs, both directions, two bundled polars and low,
in-range and high winds are exercised across all three engines. Nine checks
cover calm-wind waiting and its limits. Numeric zeros and blanks survive a
save/reload, and generated polars refresh their maximum-speed limit.

Quinton's exact endpoints, 9 October 2026 22:09 UTC departure and supplied
ECMWF GRIB complete both ways with the unchanged demo default polar in Auto
on desktop and a Samsung SM-X210 running Android 15 and stock OpenCPN 5.14.1.
The outbound result reports GRIB weather and estimated polar performance.
Professional also completes the outbound passage. Quick's bounded search
still exhausts its candidate-validation allowance on that passage; Auto
continues successfully with Standard. This is not a guarantee that every
individual engine can solve every feasible route within its search allowance.

The Android wind controls fit in portrait and landscape and save independent
Reject, Taper and Hold settings in the boat file. The arm64 package passes
16 KiB ELF layout checks; this tablet runs a 4 KiB kernel. Final CircleCI
platform qualification and publication evidence accompany the alpha release.
