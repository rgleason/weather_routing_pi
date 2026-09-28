# Windows host acceptance forecast

`irish-sea-uniform-wind.grb` is a synthetic GRIB1 fixture created with ECMWF
ecCodes `grib_set` and its `regular_ll_sfc_grib1.tmpl` sample. It contains 10 m
U/V wind fields on a 41 by 31 regular latitude/longitude grid: 57 to 51 N,
9 to 1 W, 0.2 degree spacing. U is 6 m/s and V is zero at every point.
The reference time is 2026-09-28 00:00 UTC; forecast steps are 0 through 24
hours in three-hour increments. It contains no operational forecast data.

The host test uses a short offshore route, the packaged example polar, the
bundled GRIB plugin's public file-open interface, and Weather Routing's
existing headless scenario interface. It checks actual packaged-plugin
loading and route completion in the unmodified upstream Windows host.
