# xWeatherRouting 1.29.2

Professional routing now preserves the bounded coastal departure allowance
established by independent chronological validation when it selects a contour
route as its incumbent. The final host and plotting checks receive that same
validated prefix, rather than incorrectly rejecting an otherwise valid route
at its departure point. Actual land, drying areas, minimum depth and the full
stand-off outside the local departure prefix remain enforced.

The wave-coverage improvements in 1.29.1 exposed this older metadata omission:
a shorter contour candidate became eligible and displaced a deliverable
Professional result. The fix records the departure prefix during existing
validation and copies its metadata after validation passes. It adds no weather
or chart-provider calls to that recording step and does not change search
budgets, motoring rules or polar policies. Android uses the same corrected
routing source.

Validation: the delivered-chord regression fails before the fix and passes
after it; offshore departures receive no coastal allowance. All 482 local
automated checks pass. Isolated OpenCPN runs using the reported Holyhead–Conwy
endpoints, current forecast, Nicholson 35, motor settings, 0.4 NM land margin
and 3 m minimum depth complete all 13 departure times, including the full
comfort-alternatives setting. Every delivered and plotted route passes final
chart-safety validation.

The final versioned desktop build also completes the reported route with
comfort alternatives. The ARM64 Android package passes the 16 KB ELF/page
alignment gate. On the attached Samsung SM-X210 tablet, Quick, Standard and
Professional each complete a two-mile light-wind motoring route in 21 minutes
49 seconds. All three Auto attempts reject the corresponding required-wave
case when wave heights are absent. Results touch controls and chart display
were checked. The tablet tests use a temporary synthetic GRIB; the full
Holyhead–Conwy chart/forecast replay was performed on the desktop.
