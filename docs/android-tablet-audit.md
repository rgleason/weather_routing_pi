# Android tablet audit for Weather Routing 1.17.12

Test device: Samsung Galaxy Tab SM-X210, Android 15, with the manually
installed OpenCPN 5.14.0 development build. Companion xGRIB build: 0.2.5.3
from `feature/android-arm64`. This is a device test of the arm64 packages,
not a claim about every Android screen size or OpenCPN distribution.

## Touch workflow

| Step | Device result |
| --- | --- |
| Open and close Weather Routing | In-content File, Position, Routing, View, Help and Close controls are reachable. Both list panes and the route action row fit in portrait and landscape. |
| Set positions and configure a route | Position and Routing menus are reachable; start/end pickers restore their saved selections. Basic and Advanced configuration controls can be scrolled and edited. |
| Boat and batch setup | The boat polar editor has in-content Save/Close controls; batch pages have an in-content Done control. |
| Compute with xGRIB | A synthetic 48-hour hourly coastal GRIB loaded through xGRIB's Android file picker was delivered to Weather Routing. AndroidStart (53.1, -5.6) to AndroidFinish (53.2, -5.5) completed at 7.0 nmi and 1:22 in the device run. |
| Inspect output | Settings, Statistics, Report, Plot, Filter, route table, cursor/route position and routing progress paths were opened or checked from the View/Routing menus. Modeless panels have visible Done or Close controls. Export actions become available for a computed route. |
| Shoreline data | GSHHG Intermediate 2.3.7 installed; the test route executed 581 shoreline checks. The offshore route had zero land rejections, so an actual land-crossing rejection remains to be tested. |
| Chart Awareness Settings | This OpenCPN Android build does not expose enhanced chart safety. The menu now explains that limitation and the available GSHHG land check in a readable native message. |

The GRIB was generated for the device test, not downloaded from a forecast
provider. xGRIB's Download path and its native Android settings screen open,
but an online download was not performed. The companion package includes the
Android toolbar SVG and was tested through file open, forecast step and Close.

The Android changes are guarded by `__OCPN__ANDROID__`; desktop layouts keep
their existing controls. Before publication, repeat this workflow with a
production OpenCPN Android package and a second screen size, and test a route
that actually crosses land.
