# xWeatherRouting 1.29.1

- Consistent wave-height handling across Quick, Standard, Professional, internal searches and final validation. Missing data remain unknown; limited local estimates use the maximum of nearby connected-water cells within 15 NM, with no time extrapolation. Retained results and route reports identify estimates and unchecked coverage.
- Advanced **Require wave coverage** rejects routes with unknown wave heights. The configured maximum significant wave height stays in force wherever usable data are available. Height-only data do not invent direction or period.
- Auto failures lead with **No validated route found with the selected settings**, followed by each attempted engine’s result. Search allowance exhaustion is distinguished from invalid endpoints, coverage gaps and final chart rejection.
- Modern engines honour **Anchoring**: stationary waiting remains available when enabled, bounded to six hours total, and is reported with location, UTC time and duration. Disabled anchoring cannot imply stationkeeping against currents.
- Android retains its tablet workflow and touch controls, with the shared policies and new control.

Validation: all 480 automated tests passed, including missing spatial/temporal wave coverage, required coverage with no height ceiling, local estimates, shoreline barriers and date-line grids. Isolated OpenCPN replays covered the Holyhead forecast with motoring and anchoring on/off, missing-wave warnings and required-wave rejection. Android route and touch-control checks passed on the attached tablet.

Personal boat polars are unchanged. This patch is installed locally; publication requires a separate release decision.
