# Alternative routing engine

`AlternativeRoutingEngine` is the internal fourth solver used by **All (slow)**
in xWeatherRouting 1.21. It has no individually selectable plugin option and
cannot be saved as a plugin engine ID. Auto does not run Alternative.
The standalone benchmark runner can select it explicitly.

In All, Alternative uses Professional's saved polar, environment, safety,
shoreline and cache settings, with its own fixed-layer search policy. Each
engine runs independently and must produce a validated complete route before
its arrival can compete for selection. The plugin also checks the delivered
land-safety chords before comparing candidates.

## What it compares

The search policy follows the broad outline of the published [Windy Passage
Planner 0.4.0 worker](https://windy-plugins.com/509526/windy-plugin-sail-router/0.4.0/router.worker.js):

- Advance in fixed one-hour layers using headings spaced five degrees apart.
- Keep the candidate farthest from departure in each of 72 bearing sectors,
  plus the candidate closest to the destination. Apply the worker's motoring
  and near-land penalties during pruning.
- Check arrival candidates before sector pruning.

This is **not** a port of Windy. It uses the same request, polar/performance
model, weather provider, chart/land provider, motion kernel and route validator
as Quick and the main engine. The shared motion kernel integrates at no more
than five-minute slices even when the frontier advances by one hour; final
validation independently replays the route. A candidate rejected by validation
is not reported as a complete route. Arrival must connect to the exact
requested destination. Consequently this experiment isolates the search-policy
difference; it does **not** reproduce Windy's lighter physics and should not
be expected to match Windy's speed.

The plugin's chart-safety prewarm and GUI/GRIB-broker costs are absent from the
standalone runner. The historical comparisons below do not measure those costs.

## Build and compare

```sh
cmake -S vendor/weather-routing-engine -B /tmp/weather-routing-engine-bench -G Ninja
cmake --build /tmp/weather-routing-engine-bench
ctest --test-dir /tmp/weather-routing-engine-bench -R '^alternative-' --output-on-failure

/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head original coastal
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head quick coastal
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head main coastal
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head alternative coastal

# Santa Cruz to Oahu (about 2,077 NM direct, synthetic open water)
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head original pacific6h
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head quick pacific6h
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head main pacific6h
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head alternative pacific6h

# Same crossing with synthetic time- and position-varying wind
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head original pacific-variable6h
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head quick pacific-variable6h
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head main pacific-variable6h
/tmp/weather-routing-engine-bench/tests/weather_routing_head_to_head alternative pacific-variable6h
```

In this runner, `original` is the plugin's **Quick**, `quick` is **Standard**,
and `main` is **Professional**. The `original` comparison is available when
the sibling `vendor/original-routing-engine` source is present.

Each command emits JSON with status, elapsed calculation time, passage time,
generated states and an independent validation result. Use repeated runs on
the same host and input, and report failures as well as successful time and
route quality. The solver settings intentionally differ; this measures each
policy as configured, not a mathematical apples-to-apples implementation of
the same algorithm.

An initial **single-run synthetic coastal fixture** on this development host
gave the following figures. They are a smoke comparison, not a repeatable
performance claim or a benchmark of the Windy service:

| Plugin engine (runner ID) | Calculation | Validated passage | Weather-provider calls |
| --- | ---: | ---: | ---: |
| Quick (`original`) | 0.20 s | 10 h 45 m 59 s | 37,085 |
| Standard (`quick`) | 1.04 s | 10 h 36 m 38 s | 411,101 |
| Professional (`main`) | 8.19 s | 9 h 46 m 50 s | 1,881,747 |
| Alternative (`alternative`) | 2.14 s | 9 h 43 m 55 s | 988,257 |

All four results passed the runner's independent validator. Alternative's
one-hour layers and 72-heading fan cost more than both Quick's hardened
original contour search and Standard's bounded beam search here. Run multiple
repetitions and compare route quality and failure rate across coastal, ocean,
variable-weather and chart-backed host scenarios before drawing conclusions.

### Long Pacific passage

The `pacific6h` fixture runs from Santa Cruz (36.96° N, 122.02° W) to Oahu
(21.42° N, 157.79° W), **2,077.4 NM direct**. It uses the same synthetic
constant wind, sailing polar, open-water provider and 30-day route limit for
all four engines. The request specifies a nominal six-hour step; Standard
keeps its own default three-hour offshore step, and each engine may refine
internally. No chart or depth data, GRIB loading, network access or OpenCPN host
work is included.

Here **constant wind** means exactly 14 knots *toward* 140° true (therefore
*from* 320° true) at every position and time. Current is zero and wave effects
are disabled.

One sequential run on this host produced:

| Engine | Calculation | Validated passage | Route distance | Weather-provider calls |
| --- | ---: | ---: | ---: | ---: |
| Quick | 7.7 s | 13 d 16 h 09 m | 2,086.5 NM | 1.42 million |
| Standard | 34.7 s | 13 d 23 h 43 m | 2,123.1 NM | 16.95 million |
| Alternative | 73.9 s | 13 d 14 h 45 m | 2,079.1 NM | 39.42 million |
| Professional | 217.6 s | 13 d 16 h 46 m | 2,086.2 NM | 104.20 million |

All four routes passed independent chronological validation. Alternative found
the earliest arrival in this fixture, about **1 h 24 m** ahead of Quick, while
taking about **9.6 times** as long to calculate. This constant-weather case
shows that sector pruning can retain a very good long-passage route; the
companion case below tests changing synthetic wind. Neither exercises hazards.
The high provider-call count also shows that Windy's fast appearance
cannot be attributed to sector pruning alone when our detailed motion kernel
is used.

The companion `pacific-variable6h` fixture keeps the same endpoints, polar and
limits but varies wind speed and direction with time and longitude.

Specifically, with `h` the hours since departure and `lon` the numerical
longitude in degrees, the fixture uses:

```text
wind speed (knots)       = 12 + 2 sin(h / 8)
wind-toward bearing (°T) = 140 + 35 sin(h / 12 + lon)
```

Thus speed oscillates between 10 and 14 knots, and the bearing toward which
the wind blows between 105° and 175° true (wind *from* 285°–355°). At one fixed
longitude, the nominal periods are about 50 and 75 hours respectively. The
`sin` argument uses the degree-valued longitude directly as a numerical phase,
so its spatial pattern is artificial rather than meteorologically realistic.
Current remains zero and wave effects remain disabled.

A second sequential run on this host produced:

| Engine | Calculation | Validated passage | Route distance | Weather-provider calls |
| --- | ---: | ---: | ---: | ---: |
| Quick | 7.2 s | 15 d 17 h 55 m | 2,142.9 NM | 1.34 million |
| Standard | 35.3 s | 16 d 04 h 32 m | 2,220.7 NM | 20.38 million |
| Alternative | 74.0 s | 15 d 12 h 56 m | 2,080.5 NM | 45.31 million |
| Professional | 232.9 s | 15 d 16 h 03 m | 2,103.4 NM | 122.78 million |

All four routes again passed independent validation. Alternative arrived about
**4 h 59 m** ahead of Quick and **3 h 07 m** ahead of Professional, while
requiring much more calculation than Quick. The changing wind here is a
deterministic sinusoidal stress field, not an actual Pacific forecast. A real
GRIB/chart-backed host comparison is still needed before changing plugin
defaults or recommending an engine for a passage.

Standalone clients can link
`weather_routing_engine::alternative_weather_routing_engine` and include
`supercpn/weather_routing/AlternativeEngine.h`. The plugin integration and
selection policy are described in [the 1.21 release notes](release-1.21.0.md).
