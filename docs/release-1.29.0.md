# xWeatherRouting 1.29 — motoring when sailing performance is insufficient

Motor if boat speed is below now permits motor-only headings outside the
sailing-angle limits when the best usable sailing STW is below the threshold
configured for that route. This includes directly into the wind. The check
uses every usable boat polar and permitted sailing angle, with sailing
efficiency settings applied. It compares actual sailing STW, not projected
tacking progress or speed over ground. Motor speed remains independently
configured.

Sailing and motor-sailing retain their angle limits. When a permitted sailing
angle provides sufficient performance, motor-only legs also retain those
limits. Missing or rejected polar data does not count as zero performance;
valid numeric zero does. The condition is checked at integration samples and
again during independent chronological validation. Weather, apparent-wind,
wave, land, chart, depth, boundary and propulsion checks continue to apply.

There is no additional checkbox or fixed wind-speed cutoff. Existing route
settings and the desktop and Android workflows remain. The motoring tooltip
and bundled help explain the rule. Version 1.28's route visibility, clearing
and departure display controls, and 1.28.1's polar wind-range policies and
fractional-angle correction, are included. Minimum host API remains 1.21.

An isolated Holyhead to Conwy replay using the Nicholson 35 polar, a 4-knot
motoring threshold, 5.5-knot motor speed and the original 40–160 degree sailing
angles leaves immediately instead of waiting at departure. Auto completes at
16:11 BST rather than 19:25 BST, and Professional also completes. Both pass
final chart and depth validation. Disabling motoring reproduces the previous
version's result exactly; this change does not relax sail-only routing.

The local regression suite exercises Quick, Standard and Professional with
calm and light wind, stronger sailing wind, adverse currents, different
configured thresholds, weather limits and wind returning during a leg.
Multiple polars, efficiency factors, usable zeros and rejected wind ranges
are covered. Platform builds, native Windows host checks and Android tablet
qualification are reviewed before Alpha publication.
