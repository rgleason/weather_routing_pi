# Atlas result names in 1.33.0

The completed-route label had its own five-engine name mapping, omitting Atlas. It now uses the common engine-title mapping, so Atlas winners appear as **Complete — Atlas**, with Auto or All appended when applicable. This affects both the main route list and departure optimisation results. Android uses a colon in place of the dash.

The Advanced selector, Auto/All rows and status description use **Atlas** without the experimental parenthetical. Saved engine IDs, membership bits, defaults, routing mathematics and validation requirements are unchanged.

All 58 relevant settings, sequencing and scenario regression tests passed. Four isolated native OpenCPN runs verified the actual GUI completion text for Atlas, Auto selecting Atlas, All selecting Professional, and All selecting Atlas. All four engine configuration contracts passed. Headless route-result records now include `ui_state` to expose the actual GUI label to regression checks.
