# Meridian in Auto and All; David follow-up, 23 November 2026

Meridian is now an opt-in member of both Auto and All in 1.44.0 / 2.11.0.
The displayed and serial execution order is:

**Quick → Quick2 → Atlas → Meridian → Standard → Alternative → Professional.**

It is unticked on first use and upgrades preserve every existing selection.
Auto and All remember their selections independently in route XML and last-used
preferences. Saved bit positions 0–5 remain unchanged; Meridian is appended as
bit 6 (64), despite being displayed after Atlas. Parallel All includes it within
the existing shared worker budget and merges results in stable comparison order.
An invalid or incomplete Meridian result allows later selected methods to run.
No existing solver's search implementation or Meridian's numerical search policy
was changed in this integration.

The cancellation regression additionally found that the portfolio coordinator
could return legs from a just-completed method after the whole calculation had
been cancelled. It now clears delivered legs and validation while retaining
diagnostic evidence. No additional work proceeds after whole-operation cancellation.

## Verification

- Full native suites: 736 production and 826 development enabled tests passed;
  each suite deliberately disables its timing-only benchmark.
- CI tooling: 89 production / 73 development tests passed. Remote platform builds
  were not triggered by this local change.
- Actual settings-dialog host contracts passed in both branches.
- Eight actual-plugin cases reached David's exact revised offshore endpoint,
  including currents on/off in both branches, Auto fallback from Atlas to
  Meridian, and serial/parallel All comparison.
- Independent Full GSHHG buffered-segment checks found zero land crossings and
  zero two-mile buffer breaches in all eight exported tracks.
- The existing calculation-source freeze still verifies 196 qualified inputs;
  requalification is limited to the membership/orchestration changes. All six
  existing solver sources and Meridian's search implementation are unchanged.

## David's updated report

The [updated production report](https://github.com/pob220/xweather_routing_pi/issues/40#issuecomment-7104878319)
contains the revised destination **35.533333 N, 24.333333 E**. It reports Weather
Routing 1.38.0, a 32-bit process on Windows 10 64-bit, the bundled Climatology Power
boat, November 2017 departure, Most Likely climatology, Full GSHHG and a two-mile
margin. No GRIB is selected. Its Full GSHHG hash matches the replay data. The
updated location removes the original on-land endpoint issue from this case.
Both current settings still fail to progress in his report.

The new replay uses that exact endpoint and retains his start, season, boat,
Full shoreline and margin. It completes with currents both on and off. In the
production Auto replay with Atlas and Meridian selected, Atlas returns an
incomplete search and Meridian then supplies a validated result. These are Linux
64-bit replays, not a Windows 32-bit reproduction. They do not establish that no
other engine or settings could ever solve the passage.

The [development installation report](https://github.com/pob220/xweather_routing_pi/issues/40#issuecomment-7106850382)
is a separate UX investigation. He successfully imported the **32-bit 2.9.0**
preview into his existing 32-bit OpenCPN; he did not complete a migration to a
64-bit host. He reports repeated Windows 'Not Responding' states on New passage
and Edit route before computation, confusing longitude signs, difficulty finding
position editing, and flickering plot axes. These findings cannot be attributed
to Meridian, which is absent from that release. The new-passage/edit hangs still
need independent reproduction or a Windows thread dump; this change does not
claim to fix them. Positive west longitudes also explain the misplaced Cadiz and
Gibraltar points in that document, but do not explain the reported UI hangs.

All requested code work is local. No GitHub reply was posted and the working
OpenCPN installation was not changed. The attached reports and complete replay
logs are retained under `output/meridian-auto-20261123/` in the local workspace.
The original standalone benchmarks and their limitations remain in
[the earlier evaluation](MERIDIAN-EVALUATION-20261123.md).
