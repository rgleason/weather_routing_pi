# Integration patches

`../weather-routing-engine.version` identifies the imported upstream baseline.
The integration repository also carries subsequent fixes; it is not a claim
that this directory is byte-for-byte identical to that upstream commit.

Weather Routing 1.17.5 adds bounded worker progress publication (including effort
identity), diagnostic graph-frontier summaries, and an optional wider forward
retry for long land-aware routes obstructed inside a narrow corridor. The
OpenCPN adapter enables this retry only for land-aware, non-scout calculations.
Existing resource ceilings and independent route validation remain authoritative.
The regression fixture is `ModernNativeEngine.WidensCollapsedContinentalForwardCorridorWithinBudget`.
