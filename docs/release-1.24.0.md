# xWeatherRouting 1.24.0 alpha

Optional fastest / comfort alternatives now work with Quick, Standard,
Professional and Auto as well as All (slow), for one departure or departure
optimisation. Enable them in the routing configuration. Saved percentage and
seconds limits bound additional computation after the ordinary valid result is
secured; they do not reduce the normal search budget or remove that result.
Auto explores using its successful engine. All shares the extra allowance
across successful engines and retains their validated results.

Up to four useful additional comfort candidates are retained per departure.
Identical or dominated outcomes need not appear. The comparison slider allows
extra passage time for comfort, while selecting a retained candidate updates
the chart without another search. Additional computation time and additional
passage time are separate controls. Wind-only ranking remains the saved default;
full wave ranking requires complete coverage. Wave coverage, missing-wave
duration and the worst known leg, including available waves, remain visible.

The established Android Plan / Routes / Results / Tools interface has been
extended with native touch controls for these options. Tablet testing covered
all five engines, real-GRIB routing, retained alternatives, slider/chart
selection, saved limits, status and recomputation, portrait/landscape layouts,
and repeated plugin import. A short offshore All fixture retained five
candidates and demonstrated a fastest/comfort trade-off. These small fixtures
do not establish long coastal passage performance. Original settings, six
route configurations and 749 routing files were preserved. Physical acceptance
used a Samsung SM-X210, Android 15, and the OpenCPN development 5.14.1 app.

Shared runtime fixes preserve route ownership in the status dialog, handle
absent chart cursors and nonfinite angles, and finish deferred Android Qt widget
deletion before hot plugin replacement. Desktop and Android source validation
passed 376 native regression cases before release preparation. Subsequent
xGRIB 0.3.5 tablet checks also verified generated wind/wave data reaching Auto,
Standard and All routing with complete wave coverage and comfort metrics.

The release workflow validates eleven platform targets against API 1.21 and
1.22 headers. All 22 builds must pass before the deployment hold is released.
Eleven API 1.21 package/XML pairs are published to
`pob220/xweather-routing-alpha-oss`, with root `metadata.xml` embedded for
manual plugin-manager import. API 1.22 builds are compatibility checks. Native
Linux, Windows and Mac jobs run regression suites; Android and Flatpak jobs
compile and verify packages. Windows jobs also exercise isolated OpenCPN host
routes. The historical Mac job produces native ARM64.

Publication is confined to Alpha and uses immutable download versions. It
does not change installed desktop/tablet plugins or the beta/master catalogue.
