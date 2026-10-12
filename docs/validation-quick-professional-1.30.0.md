# Quick inside Professional: 1.30.0 qualification

The 1.29.3 release was withheld after an updated forecast exposed the three-hour timestep override left in Professional's internal Quick candidate. Removing that override revealed a historical Holyhead–Conwy reconstruction failure. Version 1.30.0 removes the override and adds bounded contour reconnection under the original route request.

## Behaviour checked

- Standalone Quick and Professional's internal Quick both receive the configured nominal timestep. Departure and destination reductions still apply.
- Only Professional's existing interim-bridge recovery may rejoin a later contour vertex. There are at most four rejoins per candidate, a lookahead of three vertices and a 30 NM distance limit. Proposed stationary waits cannot be skipped.
- Reconnection creates actual sailed legs through the existing connector or validated interim bridge. It does not accept a straight line solely because a contour vertex is reachable in the search geometry.
- Independent chronological validation and final plotted-route checks remain required. The fixed endpoint allowance does not move along the coast with the boat.
- Standard's timestep policy, configured engine budgets and ordinary Quick reconstruction are unchanged.

## Completed local checks

The final routing source passed all **484 automated tests**. The historical replay set passed **17/17 Professional cases**: ten to Conwy, one to Dublin and six to Lough Foyle, using the original saved forecasts, route settings and one-hour nominal timestep. The historical 27 September Conwy noon-local departure now reconstructs and completes; logs confirm bounded contour reconnection was used.

The updated Holyhead–Conwy forecast passed **13/13 departure candidates**, including the reported 06:30-local candidate. The batch retained **seven Quick and six Professional** routes. All **30 delivered historical/current routes passed final chart safety checks**, with no final display safety failures.

The completed source was installed and verified in the working laptop host and Samsung SM-X210 tablet. The actual loaded libraries reported 1.30.0/API 1.21/64-bit. Existing personal routing files and logical navigation records were preserved. Android was imported through the normal plugin manager, and its ELF layout passed the 16 KiB alignment check.

## Earlier coverage retained and release checks

The unchanged standalone Quick and package fixes also have the earlier 1.29.3 qualification: 195 Irish Sea Auto departures across current and historical forecasts, cases exercising all three Auto engines, two completed ocean routes, and reproduction of a pre-existing westbound Atlantic resource limit. That report describes the old internal Professional override; this report supersedes that part. The 195-case run was not repeated wholesale for 1.30.0: its 17 Professional cases were replayed against the completed reconstruction fix, alongside the updated 13-departure Conwy batch.

Release qualification runs both repositories' complete 22-job platform/API matrices. Package checks compare actual linked ABIs, platform labels, architecture, root-level embedded metadata and installed data paths. Flatpak imports are checked on current stock hosts and on the exact OpenCPN 5.14.0 build reported in issue #690. Public archives must retain the reviewed CI binary payload and match their external metadata and catalogue checksums.

Private profiles, GRIB paths, screenshots and navigation backups stay local. Public release assets contain packages, checksums and a qualification summary. These checks establish the tested behaviour; they do not guarantee that every possible routing request will solve.
