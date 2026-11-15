# WeatherRouting / xWeatherRouting 1.36.4

* Real GRIB cache misses wake the host-service timer promptly. Cached valid and
  unavailable frames do not post extra wakeups. Idle routing retains its normal
  timer cadence, and workers are joined before the notification is detached.
* Auto and All diagnostic details retain each invoked routing method's elapsed
  wall time and returned work counters, including the method stopped by the user.
  Wall time includes service waits; these figures are not CPU or RAM measurements.
* Cancelling discards the delivered route but preserves known work from earlier
  methods. The last observed worker progress is also retained and explicitly
  labelled as a sample, rather than an exhaustive total of unfinished work.
* Diagnostic inputs identify whether automatic adaptive settings were enabled
  and distinguish configured numerical inputs from calculation-local choices.

Routing methods, execution order, Auto's first accepted route, All's comparison,
search allowances, adaptive policy and safety settings are unchanged. The separate
2.3.2 internal-reader acknowledgement fix does not apply to this branch, which
already acknowledges its external-provider requests before publication.

Qualification on 15 November 2026: all 640 enabled Linux tests pass; the existing
ocean timing comparison remains disabled. An isolated real-GRIB Standard route
matches 1.36.3 exactly (fingerprint `857383926234c8c9`, arrival
28 September 2026 01:30:53 UTC). A real All run completed Quick2, then stopped
Professional and retained both timings, 4,884,811 reported generated states and
the separately labelled worker sample. No accepted route remained after stopping.
Tests used disposable profiles and copied external providers; working OpenCPN
was not modified. Windows qualification is recorded separately when completed.
