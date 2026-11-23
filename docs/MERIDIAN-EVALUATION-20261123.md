# Meridian evaluation — 23 November 2026

Meridian is an additional standalone experimental engine in 1.44.0 / 2.11.0. It completed Gibraltar–Souda and 11 of the 13 saved real-weather cases. The established methods retained all 12 previously completed reference cases in both branches. The last Hanalei departure remains unresolved by these bounded reference runs.

Meridian can now be selected in Auto and All after Atlas and before Standard. It is unticked by default; existing enabled methods and their relative order are unchanged. The data do not establish a universal speed ranking or justify putting Meridian first: it misses the second Hanalei departure that Standard completes, and some completed Meridian passages take longer at sea.

## Saved real-GRIB controls

These compare fixed individual methods with identical inputs. They are **not** Auto cascade times and use **1.43.0 / 2.10.0 baselines**, not 1.38.0. All use Full GSHHG, the saved boat/polar/weather inputs and opt-in calculation helpers; parallel routing methods are off. Calculation time excludes application startup. Real-route timings overlapped private builds/replays, so individual timings are observations rather than confidence intervals or clean machine-wide speedup estimates.

| Case | Reference method | 1.43 s | 1.44 same method s | Meridian 1.44 s | Meridian 2.11 s | Meridian passage difference* |
|---|---|---:|---:|---:|---:|---:|
| farallon-pinole--06h | Quick | 2.16 | 3.47 | 2.65 | 3.60 | +0.44 h |
| farallon-pinole-+00h | Quick2 | 6.35 | 5.68 | 3.08 | 3.22 | -1.47 h |
| farallon-pinole-+06h | Quick2 | 4.38 | 4.18 | 2.90 | 2.96 | +0.59 h |
| hout-simonstown--06h | Quick | 3.93 | 3.25 | 2.83 | 2.77 | +1.38 h |
| hout-simonstown-+00h | Quick | 2.31 | 2.24 | 2.60 | 2.64 | +0.82 h |
| hout-simonstown-+06h | Quick | 2.33 | 2.10 | 2.41 | 2.14 | +1.17 h |
| conwy-leenan--06h | Quick2 | 6.32 | 5.42 | 5.13 | 5.22 | +1.91 h |
| conwy-leenan-+00h | Quick2 | 6.24 | 5.48 | 7.60 | 7.85 | +1.95 h |
| conwy-leenan-+06h | Quick2 | 13.22 | 12.77 | 7.84 | 7.88 | +1.31 h |
| hanalei-nuku-+00h | Quick2 | 44.52 | 29.97 | 15.53 | 15.54 | +5.09 h |
| hanalei-nuku-+12h | Standard | 176.26 | 117.61 | unresolved (98.21) | unresolved (68.82) | — |
| lizard-provincetown | Quick2 | 93.89 | 73.06 | 37.13 | 53.55 | +0.97 h |
| hanalei-nuku-+24h | Quick2 | unresolved (52.55) | unresolved (49.49) | unresolved (48.80) | unresolved (47.26) | — |

*Positive means more time at sea than the validated 1.44 reference candidate. Both route-search time and modeled passage quality matter when choosing Auto order. An unresolved run is not an arrival and is not proof of infeasibility.

Meridian reaches the exact destination for every reported completion. Its later Hanalei searches run out of usable forecast wind before finishing. Standard completes the +12 h case on the same inputs, demonstrating a limitation of this Meridian search rather than infeasibility of that route. The +24 h reference attempts reach their search allowances; their failure is not a proof that the passage cannot be sailed.

The shared full-buffer fix changes some Conwy and Hanalei candidates. Independent segment audits find no land or 0.4 NM buffer breaches in either of the two changed Conwy before/after tracks. Admission decisions can change because the complete buffer replaces the previous probe approximation. All previously completed reference cases still complete.

## Development baseline pairs

| Case | 2.10 s | 2.11 same method s | Reference/new validated candidates |
|---|---:|---:|---|
| farallon-pinole--06h | 2.11 | 2.21 | 1 / 1 |
| farallon-pinole-+00h | 4.33 | 6.39 | 1 / 1 |
| farallon-pinole-+06h | 5.20 | 5.12 | 1 / 1 |
| hout-simonstown--06h | 6.71 | 7.12 | 1 / 1 |
| hout-simonstown-+00h | 4.47 | 4.04 | 1 / 1 |
| hout-simonstown-+06h | 4.12 | 4.08 | 1 / 1 |
| conwy-leenan--06h | 14.15 | 9.12 | 1 / 1 |
| conwy-leenan-+00h | 12.79 | 10.61 | 1 / 1 |
| conwy-leenan-+06h | 14.44 | 13.85 | 1 / 1 |
| hanalei-nuku-+00h | 24.33 | 18.96 | 1 / 1 |
| hanalei-nuku-+12h | 179.29 | 128.24 | 1 / 1 |
| lizard-provincetown | 116.04 | 77.03 | 1 / 1 |
| hanalei-nuku-+24h | unresolved (34.51) | unresolved (36.77) | 0 / 0 |

## Gibraltar–Souda and Cowboy

Controlled Linux 64-bit replay of the reported departure: 35°55′ N, 5°37′ W; 7 November 2017 00:00 UTC; bundled Climatology Power boat and its supplied polars; Most Likely climatology; currents requested on; Full GSHHG 2.3.7; 2 NM margin. This is not an exact reconstruction of David’s Windows 32-bit environment or unknown private polar edits.

| Engine/control | Outcome | Calculation / observation |
|---|---|---|
| Cowboy 1.15.45.9, reported endpoint, 12 h step | Land: Failed; no exported route | 1.285 s observed |
| Cowboy 1.15.45.9, offshore endpoint, 12 h step | Land: Failed; no exported route | 2.534 s observed |
| Cowboy 1.15.45.9, offshore endpoint, 15 min step | Still computing when the test stopped; no exported route | 600.568 s observation guard |
| Meridian v8, offshore endpoint, 120° search cone | No validated complete route | Same-binary control; stuck near Crete |
| Meridian v8, offshore endpoint, full heading fan | Validated exact arrival | 4.888 s total calculation; 1.247 s reported search |

Only the cone setting differs in the v8 pair. The delivered offshore route has 403 legs, 1,803.508 NM and a modeled duration of 237 h 24 min 43 s. Actual wind and current usage are both 100% climatology. An independent complete-buffer audit finds zero land crossings and zero 2 NM margin breaches in all 403 segments. Missing wave data are reported; a wave ceiling cannot be established from this replay.

The final Meridian implementation deliberately uses a full-circle heading fan. Max Search Angle is disabled for this method and its saved value is preserved for other methods. This is a search-pruning difference, not permission to exceed physical wind-angle or safety limits. Original 120°-configured scenarios now complete with the final standalone planner at both the reported and offshore Souda endpoints.

Cowboy requires the cached compatible Climatology 1.6.37 provider; it refuses Climatology 2.x before search. All 36 monthly wind/current data files match the modern provider byte for byte. Its requested current setting was retained, but no delivered Cowboy track exists to establish actual current usage. The 600 s observation does not prove eventual failure.

## Validation and host controls

- Complete native suites after the Auto/All integration: 736 production / 826 development enabled tests passed. Each suite intentionally disables its timing-only benchmark.
- CI tooling unit tests: 89 production / 73 development passed. This is not a claim that remote CircleCI platform builds ran.
- Actual modules: real external-GRIB suite in both branches, incorporated GRIB control in development, and successful chart-aware offshore controls with the 65,536-region qualified global starting layer.
- Stock OpenCPN 5.14.2: Quick2 and Meridian complete ordinary shoreline controls in both branches. Enforced chart-depth requests report that the stock host lacks the required service.
- New tests cover exact arrival and independent replay, missing weather, forbidden endpoints, shallow water, cancellation, deterministic tidal tracks, narrow upwind tacking, long peninsula detours, bounded retries, time-dependent keys, complete shoreline buffers, data-preserving upgrades, effective settings and live copied diagnostics.

The original global candidate SHA256 is `6331a7db17b70a22f5ddaa9fe05112ec18e53f1a44d35c51cdcf0216eb2c65da`. Chart tests use its requalified CM93 proof file SHA256 `4926553b4c6d8883d67f35dfe340adabe04b24ccf92e6dc0808556b29f829ad6`: identical 65,536 spatial keys, with catalogue/chart identities and checked depth proofs bound to the isolated chart database. The original global file is unchanged.

The additional full chart-aware Atlantic run stopped at the proposal allowance after 299.553 s with 949.357 NM remaining, 2,000,000 proposals and no validated arrival. It accepted the full starting layer and enforced chart/depth checks. This is a Meridian limitation requiring further investigation, not evidence that the passage is infeasible or proof of an OpenCPN service defect. The matrix above remains GSHHG-only.

## Auto ordering

The selectable execution order is Quick → Quick2 → Atlas → Meridian → Standard → Alternative → Professional. Meridian remains experimental and opt-in in each mode. Its speed on the Atlantic and Gibraltar is promising, but a universal speed ranking cannot be inferred from a table containing one selected established method per route. A ranking study must run each candidate method with its proposed Auto allowance on the same inputs, charge failures and service waits, compare modeled arrival quality, and include serial and opt-in parallel policies. Placing the optional method after Atlas adds another way to solve passages without changing the existing engines. Enabling it can also add its unsuccessful Hanalei search cost before Standard runs; the recorded search and passage-quality limits still apply.

## Reproduction evidence

Local workspace evidence: `output/routing-143-2100-coastal-20261123/`. Each run retains its scenario, binary/weather/polar/shoreline hashes, result, timed track when completed, and OpenCPN log. `testdata/meridian-evaluation-20261123.json` preserves the summarized matched inputs and run records. The real-route matrix used immutable v9 modules; subsequent source cleanup changes reporting text, completed closest-distance reporting and packaging summary text, with no search-policy changes.

The archived 1.38 / 1.40 / 1.41 comparison is separate evidence in `output/routing-141-vs-legacy-20261122/earlier-138-to-141-table.md`; its native/parallel timings must not be mixed into this SDK/fixed-method table.

## Final endpoint confirmations

| Endpoint | Meridian 1.44 calculation | Meridian 2.11 calculation |
|---|---:|---:|
| Offshore Souda | 4.116 s | 5.059 s |
| Reported Souda within 2 NM margin | 4.257 s | 4.626 s |

All four final endpoint confirmations complete and validate with the original saved 120-degree input; Meridian explicitly manages its own full-circle fan. The reported water endpoint retains the existing bounded access treatment. No shore margin or current setting was reduced to obtain these results. Nothing was installed into the working OpenCPN profile or pushed/published.
