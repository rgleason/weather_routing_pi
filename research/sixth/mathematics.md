# Mathematical problem and architecture decisions

## The problem before choosing a search algorithm

For a prescribed departure epoch `t0`, find an admissible hybrid control
history and a first arrival epoch `T` minimising `T - t0`. A position alone is
not a Markov state. A useful state is

```
z = (x, t, heading, tack, propulsion mode, profile, sail plan,
     mode dwell, motor time, wait time, fuel, risk, manoeuvre count,
     coastal departure-egress privilege).
```

Here `x` is a position on the sphere. Controls choose a through-water heading,
an available sailing/motoring performance configuration, duration, and
optionally the shared stationary-hold action. The deterministic environmental
fields are water-relative wind `w(x,t)`, current `c(x,t)` and wave information.
For a selected performance mode the local tangent-plane equation is

```
dx/dt = c(x,t) + s(|w|, TWA(w,heading), waves, profile) u(heading).
```

The implementation advances on the sphere. Tack, gybe, propulsion and sail
changes have the existing finite time penalties and state transitions. Motor
run minima and cumulative resource limits matter. Waiting consumes time and
its allowed budget; it is the existing model's stationary hold, not a newly
invented drift or free cancellation of current. This model assumption deserves
future refinement if anchoring/station-keeping availability is to be represented.

Admissibility requires environmental availability under the requested fallback
policies, polar-angle and environmental limits, no land/forbidden crossings,
requested stand-off/depth, duration/exploration/resource bounds and consistent
chronological dynamics. The independent validator additionally checks the
exported plotting chords and can use more authoritative chart checks than
search guidance. Atlas optimises *this common model*. Changing the physical
model or weakening validation would not demonstrate an algorithmic improvement.

For sailing, travel time is directional, time dependent and often nonmetric.
The shortest geographical path can be much slower than a detour. A region
which is temporarily unusable may become useful after a tide or wind change.
A later arrival can beat an earlier arrival if the latter cannot spend enough
wait budget or has a different heading, tack, dwell or fuel state. An earlier
position/time label is therefore not a general dominance certificate.

## Equivalent formulations, and where equivalence ends

Ignoring hybrid switches and resources, the remaining time `V(x,t)` satisfies
an HJB equation in the feasible domain:

```
0 = V_t + 1 + min_{a in A(x,t)} f(x,t,a) dot grad(V),    V(goal,t)=0.
```

Switching requires separate mode values and nonlocal switching operators
which advance time by the actual transition delay. Fuel, waiting and motor
budgets either add dimensions or require Pareto labels. A single scalar field
in `(latitude, longitude)` cannot encode the whole requested problem.

A forward reachable-set level function uses the corresponding maximal
Hamiltonian, for example `phi_t + c dot grad(phi) + s |grad(phi)| = 0` for an
isotropic vessel. It advances a *field*, not individual boat histories. A
front need not be stored as a linked contour. State constraints enforce
obstacles. Extracting a physical route, including switches/resources and finite
penalties, is a separate problem. In a space-time, non-FIFO problem a record of
the first reach alone can discard useful later states.

A time-independent isotropic vessel with subcritical drift has a Randers
interpretation of Zermelo navigation. General sailing polars are anisotropic,
nonconvex in heading and can have forbidden cones; currents can exceed boat
speed, and forecasts vary in time. The standard smooth, strongly convex
Randers assumptions do not hold in general. An anisotropic/Finsler travel-time
view remains useful locally, but ordinary Euclidean fast marching or a static
shortest-path metric does not thereby become an exact forecast solver.

A spatial path can instead parameterise time: if a course tangent is `d`,
solve `c + s(h)u(h) = g d` and integrate `dt/distance = 1/g`. There may be no
positive course speed, multiple admissible headings, or a need for explicit
tacks. Near a tidal stall the slowness is singular. Atlas solves a finite
physical boundary-value problem rather than assuming one positive slowness
root exists on every spatial edge.

## Polar geometry removes avoidable angular branching

In a *homogeneous* field with zero switching costs, define the set of feasible
ground velocities `U = {c + s(h)u(h)}`. Time sharing yields its convex hull.
For a desired displacement direction, the largest reachable mean speed is the
radial intersection with this hull. A boundary point on an exposed hull edge
in two dimensions needs at most two active velocities. This gives a small
family of explicit two-control tack/gybe hypotheses, not a claim that every
point in a planar hull needs only two controls (a general interior point can
need three).

Finite tack penalties, spatially changing fields, hazards and mode constraints
break the exact time-sharing equivalence. Atlas uses hull geometry to suggest
positions and estimate arc cost. It then sails two distinct checked legs and
charges the actual switches. A hull chord is never delivered as a fictitious
boat velocity. Constant upwind tests use an independent fine angular VMG
calculation plus a tack penalty as a reference; they are not a proof for an
inhomogeneous voyage.

## Architectures considered before selecting the hybrid

Let `N` denote spatial samples, `K` control samples, `L` temporal layers, `B`
resource/mode combinations, `E` graph arcs, and `J` physical-integration work.
These are scaling descriptions, not measured speedup claims.

| Architecture | Why it could win | Main costs and failure modes | Decision/evidence |
|---|---|---|---|
| Eulerian HJB/level-set reachability | Analytically maximises controls; SIMD/GPU-friendly, no individual heading histories; naturally sees disconnected fronts | Roughly `O(N K L B)` (or analytic Hamiltonian), field memory `O(N B)` plus route history; CFL/refinement cost, narrow channels, hybrid/resource dimensions | Implemented independent forward PDE prototype and production coarse backward field. Retain field as guidance; fine all-state PDE remains open work |
| Static anisotropic ordered-upwind/Finsler field | Efficient causal value propagation in static anisotropic media | Requires causal stencil consistent with anisotropy; time varying/nonconvex modes do not fit a single static metric; large anisotropy widens stencil | Use local velocity geometry; do not assert static OUM solves the full hybrid forecast problem |
| Continuous PMP / direct collocation / multiple shooting | Low dimensional on smooth open-ocean routes; continuous headings/times; can use gradients | Local basins, switching/event structure and obstacle boundary arcs; derivatives through forecast/polar discontinuities; multistart expense | Implemented independent PMP and static continuous waypoint experiments; production uses coupled boundary-value shooting as a connection solver |
| Time-dependent spatial graph with multilabel search | Skips empty temporal/spatial decisions; explicit geography choices, reusable topology | `O(E)` expensive arc integrations per retained temporal/resource history; approximation of spatial family and label caps; not globally FIFO | Selected fine representation, with deferred physical arc evaluation and independent replay |
| Visibility/navigation/homotopy graph | Very small static polygonal search; distinguishes island sides before expensive physics | Weather optima may turn far from obstacle vertices; black-box providers expose neither all polygons nor exact inflated visibility vertices | Static experiment shows value of topology; adaptive water samples and angular diversity avoid requiring polygon extraction. No formal homology classes claimed |
| PRM / PRM* / RRT / RRT* | Useful very large/nonconvex decision spaces; probabilistic coverage | Narrow-passage sampling, steering in time-dependent sailing, rewiring arrival histories; asymptotic results do not certify a finite budget | Literature assessment; not implemented as a full competitor. Deterministic adaptive sampling chosen first; randomised recovery remains a useful experiment |
| A*, ALT, ARA*, branch-and-bound | Strong admissible bounds can avoid most expansions, anytime certificates | Need a certified upper envelope of physically possible speed and valid directed/time-dependent lower bounds. Provider API supplies no certified global bound | Best-first scheduling used, but current heuristic is explicitly non-admissible. No ARA* epsilon bound or branch-and-bound certificate claimed |
| Hierarchical graph / contraction | Reusable geographical connectivity; coarse long routes plus fine harbours | Forecasts and mode/resource histories alter edge costs/shortcuts; maintaining exact time-dependent profiles can dominate preprocessing | Multiresolution samples and coarse value guidance used. Persistent navigation hierarchy is a future opportunity |
| Mathematical programming / evolutionary optimisation | Joint speeds, mode/resource decisions, multiple objectives; rich constraints | Large nonconvex event constraints, many physical evaluations; MILP linearisation error; local NLP minima or population cost | Static Powell prototype tested a limited continuous candidate. Full mixed-integer/collocation/evolutionary solver not implemented; insufficient evidence to reject the field |
| Monte-Carlo / belief-tree search | Explicit forecast uncertainty and information acquisition | Branching across controls *and* weather beliefs; deterministic minimum-time task has no probability model to justify extra dimensions | Literature assessment only. Better suited to a future ensemble objective than current deterministic baseline |
| Learned heuristics / RL | Amortise repeated tasks; predict promising decisions | Distribution shift, data/oracle cost, hazard fidelity, uncertified pruning | Not implemented. Learned ordering could preserve validation; learned hard pruning would need separate evidence |
| Parallel/GPU formulations | Batch field/polar work and independent candidates | Existing host providers may serialise UI/GRIB/chart access; transfers and thread safety can erase gains | Engine is reentrant; parallel use regression tested with independent providers. No GPU or parallel speedup claimed |

These are not all rejections. Several formulations are useful *within* the
selected solver. The experiments do not establish that the chosen hybrid is
the best possible weather-routing algorithm.

## Selected method

1. **Cheap physical incumbents.** Solve direct heading and duration, test
   explicit hull-derived two-leg sailing hypotheses and bounded departure
   holds. If these fail, test broad two-leg spatial detours at several
   along-route fractions and offsets, then locally optimise a feasible shape.
   These can find a detached weather basin which a homogeneous envelope misses.
   All accepted candidates undergo chronological validation.
2. **Temporal events.** If a timed barrier defeats a long chord, locate a safe
   prefix and test waiting there. Refine an observed failed/successful time
   interval. If stationary waiting is disabled, test real out-and-back sailing
   loops and refine a successful radius. This is local candidate optimisation,
   not a global monotonic-feasibility assumption.
3. **Weather-first feedback.** Probe temporal/spatial changes. If indicated,
   build a small backward semi-Lagrangian Bellman field using original
   weather/polars/current and coarse hazard checks. Its 64 time layers and 24
   control directions are guidance. Extract two feedback pilot lineages with
   finer explicit physical integration *before* detailed geography. Preserve
   validated incumbents and sparse pilot anchors. Optional tile averaging is
   restricted to guidance and defaults off after the jet ablation.
   Spatial variation also triggers three-leg entry/cruise/exit shapes, with
   bounded coordinate optimisation of entry fraction, exit fraction and
   lateral offset. This found short-jet routes that the coarse feedback and
   two-leg families missed. These lane candidates do not displace graph
   samples: adding their anchors changed the finite graph and regressed some
   cases, so that variant was discarded.
4. **Adaptive spatial choices.** Install globally spread coarse water samples
   before coastal refinement can consume the budget. Add endpoint rings,
   polar/pilot anchors and mixed land/water-cell samples. Refine rather than
   assuming a wet cell centre proves connectivity. Use nearest neighbours,
   nearest angular-sector neighbours and longer anchor/goal connections.
   Static land tests penalise scheduling arcs but do not certify dynamic
   feasibility or permanently discard an arc.
   Optional cheap geography hints supply these static tests, while original
   point admission and all physical motion retain the authoritative provider.
   An unsuccessful guided pass retries original geography when another pass
   remains, including on per-pass queue/label saturation. Misleading hints
   are tested explicitly; no finite hint strategy is a completeness guarantee.
5. **Arrival-history search.** Schedule physical edge solves by elapsed time,
   frozen anisotropic reverse potential and, when available, the time-dependent
   field. Retain multiple arrival/tack/heading/mode/resource histories. Waiting
   is an explicit outgoing control even when sailing is possible. The finite
   label cap is an acknowledged approximation, not a FIFO dominance proof.
6. **Continuous connection solve.** Unknowns are heading and duration. An
   initial current-compensated guess is corrected by a coupled 2-by-2 endpoint
   Jacobian, with bounded Newton updates and fallback corrections. The heading
   derivative comes from an extra checked integration; the time derivative
   uses endpoint ground velocity. Strong jets exposed why independent
   bearing/range corrections oscillate. Integer-second duration and finite
   endpoint tolerance are inherited numerical approximations.
7. **Replay and improve.** Retain actual integrated endpoints rather than
   snapping a boat onto a waypoint. Reconstruct the same action partition and
   independently validate. Exact terminal coastal connections use the shared
   terminal solver; internal points retain full stand-off. Preserve the best
   validated incumbent under resource exhaustion, widen/refine and continue
   within the improvement allowance. Cancellation clears the result.

With `N` graph vertices and nearest count `k`, present neighbour discovery is
`O(N^2 log k)` plus angular binning, with `O(Nk)` links (and longer root/goal
links). Reverse scheduling costs `O(E log N)`. Search cost is dominated by
checked physical integrations and provider calls, not abstract heap pops.
The coarse field costs `O(Nc K L)` with about a few hundred coarse positions,
`K=24`, `L=64`. Its memory is `O(Nc L)` for temporal interpolation. Neither the
roadmap nor the scalar field contains a combinatorially complete
representation of the hybrid control problem.

## What measurement changed

- A depth-first coastal quadtree exhausted the node budget near the first
  headland. Installing global coverage first recovered slalom route families.
- Centre-only coastal samples missed an offset 0.2 NM passage. Mixed-cell
  refinement and independent endpoint rings recovered it; small unresolved
  passages remain possible.
- Independent heading/time correction oscillated in a strong jet. Coupled
  Newton shooting recovered the useful detour without changing currents.
- A frozen geographical potential struggled with wind reversal and long
  variable weather. A coarse time-dependent field recovered these route
  classes; feedback pilots improved several arrival times further.
- A 15-minute gate schedule left measurable oracle error. Safe-event hold
  relocation plus local time refinement reduced it to numerical tolerance,
  including gates which open later than the maximum *departure* wait budget.
- Increasing the spatial sample set improved some costs slightly but increased
  coastal graph cost sharply. Smaller and larger spaces were both tested.
- Sparse weather-change probes missed a detached moving storm. Broad spatial
  hypotheses and local two-leg shape optimisation recovered it; a full field
  forced at the same coarse resolution alone did not. A coastal replay mismatch
  exposed that the terminal solver must be reconstructed with its original
  search window, not its solved leg duration; the window was made identical.
- Averaging coarse currents hid a narrow jet. Field averaging is an optional
  experiment, not an unconditional forecast simplification.
- Chart-backed profiling found hundreds of thousands of expensive calls in
  geography guidance. Separating approximate scheduling from authoritative
  motion/validation reduced chart calls in a real 5 m depth test by about 96%,
  retaining the same independently accepted arrival. See the qualified native
  A/B evidence in the results report; this is one case, not a universal bound.

Formal completeness, convergence under coupled space/time refinement and a
certified a-posteriori error bound remain unsolved for this implementation.
The benchmark is evidence about finite tested cases, not a substitute for
those proofs.

The strong north-current negative case also accounts for endpoint tolerance:
the polar's largest south component is below 4.7 kn, leaving more than
0.3 kn northward ground motion in a 5 kn current. A route remaining in the
destination's latitude band needs almost 20 NM of eastward progress, at no
more than 6 kn eastward speed. That entails roughly 1 NM of northward drift,
well beyond the 0.05 NM arrival tolerance; stationary holds cannot undo it.
Thus its expected failure is not inferred merely from another engine's miss.
