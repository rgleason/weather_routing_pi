# xWeatherRouting 1.29.3

Quick now uses the route's configured nominal timestep, one hour by default,
instead of silently taking a three-hour base step. Its existing synchronous
reductions near departure and destination remain. Auto tries this corrected
Quick search first. Standard's timestep policy and Professional's independent
contour recovery search retain their previous behavior.

Quick also gains the shared bounded coastal endpoint handling. Shore-buffer
access is limited to fixed areas around the original endpoints, with reach
clamped between 0.5 and 2 NM and normally twice the configured land margin.
It cannot roll along the coast with the route. Actual land, drying hazards,
required minimum depths and the full clearance outside those areas remain
checked during search and independent chronological validation, including the
delivered and plotted route geometry.

Flatpak target labels now match the stock host, without the incorrect wxWidgets
suffix. Ubuntu Jammy labels reflect the wxWidgets version actually linked.
Flatpak boat and polar assets install under its stock plugin data directory.
CI validates package ABI labels for every platform before deployment.

Validation details are recorded in `validation-quick-nominal-1.29.3.md`.
