# xWeatherRouting 1.34.0

The boat editor again gives the polar list and its controls their full space.
The low- and high-wind settings now have a separate **Wind range** page, which
identifies the selected polar. This fixes the layout regression that could
obscure the list and make the existing multi-polar editor appear unavailable.

On desktop, open the route configuration, click **Edit** beside the boat file,
select a polar in the list, then use the **Wind range** tab. On Android, use
**Plan → Edit boat and polars**, select the polar on the **Polars** page, and
choose **Wind range** in the page selector. The existing touch controls and
layout are retained.

Add, Edit, Remove, Up, Down, Open Boat, Save Boat and Save As remain available.
Each polar retains its own below/above-range settings and crossover allowance
when the boat XML is saved, reopened or reordered. Legacy boat files continue
to use Automatic where no wind-range policy was saved.

The routing engines, polar interpolation and crossover selection are unchanged.

Validation includes the full native test suite (559 enabled tests passed;
one pre-existing optional timing test disabled), production Boat XML round trips
with two polars, legacy XML defaults and multi-polar selection limits. Isolated
desktop GUI tests cover adding, editing, removing and reordering polars,
per-polar policies, saving/reopening and resizing. Attached Android tablet
checks cover the touch file picker and polar editor, reordering, independent
saved policies, and portrait/landscape layouts. Final 1.34 real-GRIB routes with
a two-polar boat completed with both Quick and Atlas.
