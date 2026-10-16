# Professional search policy

`ProfessionalEngine` selects the private policy in `src/ProfessionalPolicy.h`.
Generic `RoutingEngine` callers and MotionKernel entry points retain baseline
behavior, including internal Quick bridges. The host supplies contour Quick's
incumbent through the existing overload. There is one Professional engine, one
shared motion implementation and no public API or OpenCPN core change.

## Search sequence and budgets

1. If internal Quick has a valid route inside the requested horizon, retain it
   and run one focused native improvement at 100%, bounded by Quick's arrival.
   This preserves the established optional-work budget. Running two native
   improvements exhausted a previously solvable passage's overall deadline.
2. If Quick failed, begin native search without a forward cross-track corridor
   or fringe quota. Keep forward refinement, reverse recovery, local/frontier
   recovery and global graph fallback with their existing requested budgets.
3. If unrestricted native search succeeds, retain its candidate and give the
   focused policy a fresh comparison at the completed native effort tier,
   starting from the original position/UTC.
   Its horizon cannot exceed the incumbent's arrival. Lower tiers are not
   repeated: a 400% broad solve must not receive only a 100% comparison.
4. If unrestricted search fails, give focused recovery its full original fresh
   ladder. Keep the earliest complete, independently validated candidate.

Failed comparisons do not erase incumbents. Whole-request cancellation clears
all candidates, including cancellation during acceptance. No blanket rule bans
tidal backtracking, tacks, waits or west-going headings.

The focused policy is a regression guard: removing the strip alone made
Farallon–Pinole 4,257 seconds slower. Its old constants are not presented as
physically derived. The unrestricted first search has no replacement 6/12/20 NM
boundary. Progressive graph corridors defer labels and end with the caller's
maximum width; production supplies infinity. Explicit angle, radius, latitude,
duration and resource constraints still apply.

## Arrival horizons

The original reverse and global graph paths could exceed the request's route
horizon while passing physical replay. Professional now clips reverse bridge
windows to remaining arrival time, clips final forward/graph expansion steps,
and rejects candidates or Quick incumbents beyond the permitted deadline.
A goal exactly on the deadline is allowed; another positive-duration expansion
is not. Bounds derive from the original request or an already valid arrival,
not a guessed geographical corridor. Controlled regressions expose the old
reverse/graph defect and verify its correction.

When an actual arrival horizon clips a connection window, an overlong initial
VMG estimate is refined from the available moving-time boundary. Evolving tide
can permit an in-window arrival even when the initial estimate is too long.
The captured Dublin clamp/no-clamp intervention demonstrates this. Ordinary
approach windows retain cheap overlong-estimate rejection; identical shooting
trials are not repeated. Every accepted connection still passes full replay.

## Continuation-aware pruning

Professional's cell key additionally separates sail plan/profile role and
identity, remaining configured minimum motor-run time and departure-egress
permission. States with different next-motion permissions or sail-change
penalties cannot dominate each other merely because first-leg speed/cost is
equal. A controlled changing-wind/two-sail-plan test demonstrates the lost
route. Keys add no chart query, and hashing reads fields by reference.

Spatial/heading bucketing, epsilon resource dominance, bounded lineage
alternatives, sector caps and graph time buckets remain approximations. They
do not prove global continuation equivalence, completeness or optimal ETA.

## Other corrections and integration

- Reuse weather only at an identical parent position and UTC. Delayed starts,
  later integration boundaries and midpoints resolve their own conditions.
- A generated graph goal remains an incumbent while queue/deferred cost bounds
  can improve it. Production uses zero heuristic; callers supplying a positive
  heuristic remain responsible for admissibility under currents.
- Rejection of one complete reverse prefix does not terminate other eligible
  lineages. Chronological forward validation remains mandatory.
- Cumulative native generation includes independent searches. Stage notes
  distinguish work; per-stage counters still describe the returned search.

The host's Fast/Comfort collector and scorer remain. Unrestricted-first elapsed
comparison applies to fastest objectives, not weighted comfort objectives.
The private historical envelope/post-pilot retry controls, safe-fringe refill
and arrival shortcuts are not exposed by the public wrapper. They are disabled
in the default search sequence; portable tests retain explicit controls for
the earlier interventions and incumbent/cancellation contracts.

Qualification combines portable regression checks with actual GRIB, chart,
depth, motor, tide, departure-window and combined-engine application replays.
The investigation records computation time separately from sailed passage.
