# WeatherRouting / xWeatherRouting 1.36.2

Long native-engine passages using Climatology fallback no longer issue a GRIB service request
for every new timestamp when the forecast provider is not answering.

* Confirmed missing timeline frames have a separate 32,768-entry limit from
  the 512 copied forecast frames. Payload memory remains bounded by the user's
  existing MiB allowance and physical-memory guard. Missing-frame lookups use
  constant-time LRU updates and survive forecast-frame eviction.
* An unanswered provider request is a transient service failure, not proof of
  missing forecast data. Uncached requests use a shared 1, 2, 4, 8, 16, 30 second
  retry backoff, capped at 30 seconds. A real reply, including a confirmed
  missing frame, resets it. Starting a new computation batch resets both the
  retry state and cached frame statuses. Cached replies remain usable during
  the cooldown; callers can use Climatology only when already permitted.
* The cache log now includes actual cache hits, negative hits, lookups,
  positive/negative entry counts, unanswered requests, backoff skips, request
  timeouts and cumulative condition-variable waiting. Reading a newly fetched
  reply no longer counts as a cache hit. Retries after a transport failure do
  not count as reloads after eviction.

The same correction is applied to the 2.2.3 development branch. No solver,
sampling default, safety constraint or engine search allowance is changed.

Regression coverage includes a 30-day quarter-hour missing-frame working set
over three attempts; retention during forecast memory pressure; bounded
negative eviction; provider recovery and backoff reset; concurrent queued
callers; cached forecast use during an outage; and the actual GRIB timeline
cache wrapper. Full-suite and headless qualification receipts are recorded with
the local build artifacts.

Local Linux qualification passed 618 enabled production tests and 697 enabled
development tests, plus AddressSanitizer/UndefinedBehaviorSanitizer cache tests.
The missing-provider Cornwall–Massachusetts control retained exactly the same
route, ETA and validation result: unanswered requests fell from 7,960 to 9.
Its patched solver time was 139.985 seconds, compared with 139.498 seconds
for explicit Climatology-only routing (baseline: 174.762 seconds). These are
local observations, not a Windows performance guarantee.

Paired application runs with real GRIBs/polars and verified Full shoreline data
preserved the delivered and retained routes for the offshore TPXO case (all six
engines attempted) and Holyhead–Conwy in both branches. The development reader
also continues to reject the oversized archived global GRIB at its existing
decode limit.

Headless external-provider initialization no longer requests the provider's UI
timeline after opening a file. Explicit scenario departure times and dated
weather-frame requests are sufficient. This prevents a null-active-file crash
in external GRIB versions when loading an unsupported file. Regular interactive
timeline synchronization and forecast routing with a loaded file are unchanged.
Both GRIB and xGRIB use the same public file-open/frame-request interface.

The same pending 2.2.3 release also fixes a distinct incorporated-loader crash:
JPEG2000 decoding now owns and serializes the JasPer initialization/decode/
cleanup lifetime. It no longer relies on an external plugin to initialize it.
The archived Farallon forecast loads under address, undefined-behaviour and
leak sanitizers and completes its route. A seven-message real-data fixture
checks all 1,260 wind/wave values and missing masks against ECMWF ecCodes and
repeated concurrent worker loads. No additional version bump is used.
