# WeatherRouting 1.30.1

Flatpak packages install bundled boats, polars and GSHHG shoreline archives in the directory searched by stock OpenCPN. This fixes the 1.30.0 failure `Cannot open shoreline download or installation directory` when Detect Land is enabled, and makes bundled shoreline data available to the configuration controls.

The package retains `share/opencpn/plugins/<plugin identity>` because OpenCPN's Flatpak importer preserves the suffix after `share/`, while its data lookup searches `~/.var/app/org.opencpn.OpenCPN/data/opencpn/plugins`. The Flatpak-only path override introduced with the unpublished 1.29.3 changes was incorrect.

Flatpak integration checks now run in isolated normal profiles with a private message bus. They verify stock OpenCPN's actual bundled-data lookup and complete Quick, Standard and Professional routes with Detect Land both off and on, using verified Intermediate shoreline data and the normal writable shoreline cache. The archive audit rejects shoreline payloads outside the host's lookup path. Earlier portable-profile tests supplied an explicit polar and disabled Detect Land, so they did not exercise that lookup.

Routing algorithms, saved route settings, forecast requirements, resource policies, minimum plugin API 1.21 and non-Flatpak data paths retain their 1.30.0 behaviour. This release does not introduce a minimum-RAM requirement or claim to resolve the separately reported netbook crash.

For a routing test, the GRIB must cover the departure and expected passage duration, and a sufficiently broad area around the intended route to allow the search to consider alternatives. A forecast-coverage failure needs the actual GRIB and saved route configuration to distinguish a valid coverage limit from a defect.

The original report is [WeatherRouting issue #690](https://github.com/rgleason/weather_routing_pi/issues/690#issuecomment-6127303879).
