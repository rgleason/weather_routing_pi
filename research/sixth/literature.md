# Independent literature review

Search date: 16 October 2026. Searches covered weather routing, hybrid and
continuous optimal control, ocean-flow navigation, Hamilton–Jacobi numerics,
robot motion planning, time-dependent networks and uncertainty. Links below
are papers, author/institutional manuscripts, or author software; search-result
snippets and secondary summaries are not the technical authority. Access
ranged from full open manuscripts to publisher abstracts. No external
implementation's performance claim is treated as a result for this repository.
The **implication** paragraphs are our assessment, not claims by the authors.

## Weather routing and sailing-specific control

[Zis et al., *Ship weather routing: A taxonomy and survey* (2020)](https://doi.org/10.1016/j.oceaneng.2020.107697)
organises approaches including isochrones, dynamic programming, variational
methods and heuristics. It is a map of the field rather than a reason to choose
one method. The objective, vessel model, uncertainty and computational budget
must be made comparable before using published speed or fuel results.

[Ferretti and Festa, *A Hybrid control approach to the route planning problem
for sailing boats* (2017 preprint; subsequent journal formulation)](https://arxiv.org/abs/1707.08103)
models continuous sailing and discrete manoeuvres using hybrid control.
**Implication:** tack/gybe state and delay belong in the control problem; a
single smooth speed field without switches is an incomplete foundation for
this plugin's finite-penalty sailing problem.

[Miles and Vladimirsky, *Stochastic Optimal Control of a Sailboat* (2021)](https://arxiv.org/abs/2109.08260)
reduces state dimension and uses an adaptive, nearly causal semi-Lagrangian
discretisation, including a switching operator over future wind states.
**Implication:** dynamic programming need not mean a uniform brute-force
space-time array; exploiting causality and symmetry is valuable. Their
match-racing stochastic model is not the full coastal forecast/provider task.

[Thornton, *Minimum-Time Sailing Routes* (1993)](https://doi.org/10.1017/S037346330001136X)
is an early sailing-specific minimum-time reference. Publisher access was
limited; it is a historical lead, not a basis for a claimed implementation or
quantitative comparison here.

[Mannarini et al., *VISIR-2: ship weather routing in Python* (2024)](https://gmd.copernicus.org/articles/17/4355/2024/)
presents a modular graph-based model with dynamic costs, wind/current sailing
performance and spatial coast-intersection indexing. **Implication:** graph
formulations with serious physical validation are established alternatives to
isochrone contours. Its shipping results do not establish Atlas speedups.

[Dickson et al., *Uncertainty in marine weather routing* (2019 preprint)](https://arxiv.org/abs/1901.03840)
examines numerical and vessel-performance uncertainty. **Implication:** a very
precise ETA is not necessarily a very accurate real-world ETA. Keep numerical
oracle error separate from forecast/polar uncertainty and validator fidelity.

## Zermelo, Finsler geometry and Hamilton–Jacobi formulations

[Bao, Robles and Shen, *Zermelo navigation on Riemannian manifolds*](https://arxiv.org/abs/math/0311233)
connects navigation with Randers geometry under specific regularity and
convexity assumptions. **Implication:** geometry can replace explicit heading
branching in suitable models, but a sailing polar with forbidden directions,
supercritical currents and temporal mode switches does not automatically
satisfy those assumptions.

[Lolla et al., *Time-optimal path planning in dynamic flows using level set
equations* (2012)](https://mseas.mit.edu/publications/PDF/lolla_etal_path_plan_LSM_ICRA2012.pdf)
and the [MIT record for the later dynamic-flow work](https://dspace.mit.edu/entities/publication/e28f54c4-00d6-4532-933b-1f3d86058651)
provide a direct ocean-navigation reachable-set alternative. **Implication:**
a field can encode globally different route families without enumerating a
boat at every heading. Resources, hybrid switching and route reconstruction
must still be represented. A small isotropic PDE prototype was implemented
in this lab; it is not a replication of their complete method.

[Sethian and Vladimirsky, *Ordered Upwind Methods for Static Hamilton–Jacobi
Equations: Theory and Algorithms*](https://www.ljll.fr/~frey/papers/levelsets/Sethian%20J.A.%2C%20Vladimisrski%20A.%2C%20Ordered%20upwind%20methods%20for%20static%20Hamilton-Jacobi%20equations.pdf)
develop causal treatment of static anisotropic Hamilton–Jacobi problems.
**Implication:** standard isotropic fast marching is not the only efficient
field method. Strong anisotropy changes the necessary stencil; time dependence
and hybrid resources prevent simply importing a static one-pass guarantee.

[Yershov and Frazzoli, *Asymptotically optimal feedback planning using a
numerical Hamilton–Jacobi–Bellman solver and an adaptive mesh* (2016)](https://journals.sagepub.com/doi/10.1177/0278364915602958)
combines adaptive representation and feedback planning. **Implication:**
mesh adaptation and value functions are compatible. The lab's fixed-grid PDE
shortcomings do not reject this more capable family. Atlas currently uses a
coarse value field for ordering/feedback and a different fine representation.

[Lee et al., *Efficient Optimal Planning in non-FIFO Time-Dependent Flow Fields*](https://arxiv.org/abs/1909.02198)
and [the institutional manuscript](https://opus.lib.uts.edu.au/rest/bitstreams/ec053ea4-06ce-4e01-936e-43e3d74b9763/retrieve)
are relevant to temporal decisions and waiting in flow fields. **Implication:**
waiting is a real optimisation variable, not merely a fallback when all moves
fail. Finite wait budgets and other resources require care with dominance.

## Continuous optimisation and mathematical programming

[Kelly, *An Introduction to Trajectory Optimization: How to Do Your Own Direct
Collocation* (2017)](https://epubs.siam.org/doi/abs/10.1137/16M1062569)
provides a practical continuous optimal-control transcription reference.
**Implication:** heading and time can be continuous optimisation variables;
mesh refinement, constraint sampling and local minima require independent
checks. Atlas uses continuous boundary-value shooting for individual
connections; it is not a direct-collocation implementation.

[Precioso et al., *BERS: Locally Optimal Continuous Algorithm for Maritime
Weather Routing with Just-in-Time Arrival* (May 2026 preprint)](https://arxiv.org/abs/2605.31533)
combines Bézier route parametrisation, CMA-ES global exploration and local
variational refinement. **Implication:** global route-shape search plus local
refinement is a promising further candidate beyond graphs. The paper's cargo
vessel/energy/arrival setting differs from this finite-tack minimum-time sail
model. Its abstract and formulation were reviewed; the full algorithm was not
replicated, and its savings are not claimed for Atlas.

[A multiobjective approach for sustainable ship routing and scheduling](https://research.manchester.ac.uk/en/publications/multiobjective-approach-for-sustainable-ship-routing-and-scheduli/)
connects routing with mathematical programming and operational objectives.
[An exact branch-and-price ship-routing study](https://arxiv.org/abs/1809.10584)
is another operations-research lead. **Implication:** exact discrete optimisation
can certify a specified discretised/logistics problem; it does not remove
forecast/polar nonlinearity or certify a continuous sailing route by itself.
These were formulation leads, not implemented competitors.

[Mixed-Integer Path-Stable Optimisation](https://arxiv.org/abs/2001.08121)
studies a structured family of nonconvex hybrid optimal-control
transcriptions. **Implication:** branch-and-bound becomes attractive when
usable relaxations exist. No evidence was found that this repository's
black-box weather/safety providers satisfy that paper's structured assumptions.

[De, Choudhary and Tiwari, *Multiobjective Approach for Sustainable Ship
Routing and Scheduling with Draft Restrictions*](https://research.manchester.ac.uk/en/publications/multiobjective-approach-for-sustainable-ship-routing-and-scheduli/)
uses NSGA-II and multiobjective particle swarm optimisation for a maritime
MINLP with port, tide and draft constraints. Its logistics objective differs
from continuous sailing. The BERS work cited above is a more direct example
of population-based continuous weather-route shape optimisation.
**Implication:** population methods can explore shape and tradeoffs without
a causal grid, but require repeated model evaluations. A naive multistart
waypoint optimiser was prototyped here; it does not stand in for a tuned
evolutionary method.

## Time-dependent graphs, heuristic bounds and hierarchy

[Dean, *Algorithms for minimum-cost paths in time-dependent networks with
waiting policies* (2004)](https://doi.org/10.1002/net.20013)
and [*Shortest Paths in FIFO Time-Dependent Networks: Theory and Algorithms*](https://people.csail.mit.edu/bdean/tdsp.pdf)
distinguish waiting-policy and FIFO structure. **Implication:** ordinary
one-label Dijkstra is justified only with the right conditions. Unlimited
costless waiting can restore useful arrival-order properties in models where
an earlier state can reproduce all later departures. Atlas has bounded
waiting and mode/resource histories, so it does not assume that equivalence.

[Goldberg and Harrelson, *Computing the Shortest Path: A* Search Meets Graph
Theory*](https://www.microsoft.com/en-us/research/publication/computing-the-shortest-path-a-search-meets-graph-theory/)
uses landmark/triangle-inequality lower bounds on directed graphs.
**Implication:** ALT requires valid lower bounds. A reverse shortest path in
an approximate frozen forecast graph is useful ordering but not automatically
an admissible lower bound for a changing forecast.

[Likhachev, Gordon and Thrun, *ARA*: Anytime A* with Provable Bounds on
Sub-Optimality* (CMU-CS-03-148)](https://www.cs.cmu.edu/~maxim/techreports.html)
provides a principled anytime-search reference. **Implication:** a fast first
incumbent plus continued improvement is attractive. Atlas's heuristic and
label caps do not meet the assumptions needed to claim an ARA* bound.

[Lazy Theta*: Any-Angle Path Planning and Path Length Analysis in 3D](https://ojs.aaai.org/index.php/SOCS/article/view/18152)
is an any-angle planning reference. **Implication:** line-of-sight connections
reduce grid-stencil distortion; a spatial shortcut still needs a sailing and
time-dependent safety connection solve.

[A time-dependent contraction-hierarchy academic study](https://essay.utwente.nl/essays/96877)
was considered as a hierarchy lead. **Implication:** reusable graph shortcuts
can amortise repeated searches, but shortcut profiles must preserve the
underlying time-dependent problem. This is future preprocessing work, not an
implemented contraction hierarchy.

## Topology, sampling and maritime roadmaps

[Karaman and Frazzoli, *Sampling-based Algorithms for Optimal Motion Planning*](https://arxiv.org/abs/1105.1186)
provides PRM*/RRT* asymptotic analysis. **Implication:** asymptotic optimality is
not a finite-budget solvability or quality guarantee, especially when steering
has wind/current/mode histories. These methods remain useful recovery
candidates; Atlas does not claim their guarantees.

[Bhattacharya, Kumar and Likhachev, *Search-based Path Planning with Homotopy
Class Constraints*](https://publications.ri.cmu.edu/search-based-path-planning-with-homotopy-class-constraints)
explicitly represents topological alternatives. **Implication:** which side of
an island a path takes can be a discrete high-value choice. Atlas preserves
angular/spatial alternatives but has no formal homotopy-signature machinery.

[Hsu et al., *The Bridge Test for Sampling Narrow Passages with Probabilistic
Roadmap Planners*](https://users.cs.duke.edu/~reif/paper/sunz/bridge/pageproofs.bridge.pdf)
addresses a characteristic weakness of uniform sampling. **Implication:**
coast/portal-directed samples can be more important than simply increasing
sample count. Atlas's mixed-cell refinement is deterministic, not that test.

[Charalambopoulos, Xidias and Nearchou, *Efficient ship weather routing using
probabilistic roadmaps* (2023)](https://www.sciencedirect.com/science/article/pii/S0029801823004158)
builds a sampled maritime roadmap with weather-weighted connections and
modified Dijkstra search. Publisher abstract/section summaries were reviewed;
its reported runtime is not transferred to this repository.
[Wu et al., *Hybrid Probabilistic Road Map Path Planning for Maritime
Autonomous Surface Ships Based on Historical AIS Information and Improved
DP Compression* (2025)](https://www.mdpi.com/2077-1312/13/1/184)
is another application-specific sampling lead. **Implication:** a maritime
roadmap can reason about static geography separately from expensive temporal
traversal, but weather-optimal turns also occur far from coasts. This motivated
combining geography with weather-first pilot anchors rather than requiring
all turns at obstacle vertices.

## Stochastic, learned and parallel formulations

[Nuñez et al., *Risk-aware stochastic ship routing using constrained continuous
belief tree search* (2024), institutional full text](https://opus.lib.uts.edu.au/rest/bitstreams/be6bb36c-d233-4c7e-b68a-d9e0d8a697a7/retrieve)
uses ensemble forecasts and risk constraints in a continuous belief-tree
formulation. **Implication:** this changes the objective and information state,
not just the search queue. Atlas currently benchmarks a deterministic problem;
implementing this properly would need a common ensemble/risk contract and a
separate validation criterion. Reported shipping fuel gains are not transferred.

[IBoat-PMCTS, author implementation](https://github.com/PBarde/IBoat-PMCTS)
is a sailing Monte-Carlo/tree-search lead. **Implication:** rollout methods can
explore diverse control sequences, but need a meaningful rollout policy and
uncertainty model to justify their computation. No full MCTS engine was
implemented in this iteration.

[Latinopoulos et al., *Marine Voyage Optimization and Weather Routing with
Deep Reinforcement Learning* (2025)](https://www.mdpi.com/2077-1312/13/5/902)
and [a UCL wind-assisted routing manuscript](https://discovery.ucl.ac.uk/id/eprint/10230693/)
provide learned routing examples. Publisher access to the first was rate
limited after its abstract. **Implication:** learning could order decisions
or predict a coarse value field, while preserving exact acceptance. Training
cost, coverage and model mismatch must be measured. No learned solver or
learned safety certificate is claimed here.
The first paper's congested-water strategy includes entry/exit teleportation;
that abstraction cannot supply the explicit physically checked sailing legs
required here. It remains useful as a learning/optimisation lead, rather than
a directly comparable safety-qualified route generator.

[Real-Time Hamilton–Jacobi Reachability Analysis of Autonomous System With
an FPGA (IROS 2021)](https://www2.cs.sfu.ca/~ashriram/papers/2021_IROS_HJ.pdf),
[the author Optimized DP toolbox](https://github.com/SFU-MARS/optimized_dp) and
[LevelSetPy: A GPU-Accelerated Package for Hyperbolic Hamilton–Jacobi Partial
Differential Equations (2025)](https://doi.org/10.1145/3730407) illustrate parallel
field computation on different hardware. **Implication:** the coarse guidance
field could be batched on a GPU if immutable environment/polar data were
available. Host chart/GRIB callbacks cannot simply be assumed GPU-accessible
or thread safe. No GPU performance claim is made.

## Result of the review

The main opportunities were a hybrid-control state, control maximisation in
velocity/value geometry, topology-aware spatial decisions, continuous
connection solving, explicit arrival histories and multi-resolution feedback.
These informed actual prototypes and the selected implementation. Several
unimplemented candidates remain credible: adaptive full-state HJB, certified
heuristic search, informed sampling recovery, global continuous route-shape
optimisation and GPU/batched feedback fields. The current evidence does not
justify declaring those approaches inferior.
