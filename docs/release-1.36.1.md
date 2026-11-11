# xWeatherRouting / WeatherRouting 1.36.1

This maintenance hotfix identifies consecutive waypoints at the same position
before routing starts. The message names both waypoints and the leg number and
explains how to correct the route. The whole sequence is checked before any legs
are created and before a saved multi-leg sequence or departure optimisation is
started. Individually computed saved legs retain the specific setup diagnostic.

Remove or move one of the coincident consecutive points in OpenCPN, recreate the
Weather Routing legs, then compute the multi-leg sequence. No waypoint is removed
or moved automatically. Nearby distinct points and legitimate A to B to A return
passages are accepted. Equivalent longitudes and polar coordinates are handled;
the numerical tolerance is 1e-10 degrees (approximately 0.01 mm), not a waypoint
spacing requirement. No routing engine, allowance, weather setting or shoreline
safety policy has changed.

Version 1 retains its existing interface and configuration format. Version 2.1.3
adds the corresponding Route card/readiness diagnostic and waypoint editor.
