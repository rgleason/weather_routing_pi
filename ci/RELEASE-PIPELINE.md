# WeatherRouting release pipeline: CircleCI → Cloudsmith → OpenCPN/plugins

This pipeline takes one build all the way to a pull request on OpenCPN/plugins.
It stops for your review before anything is published. The pipeline is defined
in `.circleci/config.yml`. The tokens are kept in one CircleCI context.

- **Which settings to use when you trigger a pipeline:** [`PIPELINE-SETTINGS.md`](PIPELINE-SETTINGS.md)
- **How the catalogue PR is built and checked:** [`catalogue/PULL-REQUESTS.md`](catalogue/PULL-REQUESTS.md)

## The flow

```
your trigger → build all platforms → validate XML → hold: you approve
  → Cloudsmith upload (all or nothing) → Phase 1 dry run
  → Phase 3: push branch to rgleason/plugins and open PR on OpenCPN/plugins
  → upstream checks → maintainer merges
```

1. **Validate.** The build jobs and `stage-publication` check every XML and tarball pair.
   They check the plugin identity, the platforms, that the embedded and external XML match,
   and the XML schema.
2. **Review.** At `hold-for-publication-approval` you look over `reviewed-publication` and approve.
3. **Cloudsmith, all or nothing.** Before uploading anything, `publish-reviewed` checks all
   22 files with Cloudsmith and confirms no earlier build of this plugin version is
   already published (any build of 1.36.5 blocks another 1.36.5). Each upload then waits
   until Cloudsmith has finished processing it. If any upload fails, every package from that
   run is deleted again, so a re-run starts clean. The job artifacts record what happened in
   `published.json` or `rollback.json`.
4. **Phase 1, dry run.** `catalogue-pr-dry-run` runs automatically after every publish. It
   uses no token and pushes nothing. It:
   - clones OpenCPN/plugins master;
   - makes branch `weather_routing_pi-<version>-build<N>`;
   - removes the old WeatherRouting XML for the same platforms and adds the new ones
     (it refuses downgrades, and refuses to replace the same version from another build);
   - runs upstream's own PR checks: both of its XML schemas, its link checker, and a whitespace check.

   The XML files, patch and PR text are kept as CircleCI artifacts under `catalogue-pr/`.
5. **Phase 2, fork test (optional, rarely needed).** Pushes the branch to rgleason/plugins
   and opens a draft PR *inside the fork*. OpenCPN/plugins is not touched.
6. **Phase 3, upstream.** Opens the PR from `rgleason:<branch>` to `OpenCPN/plugins:master`.
   Upstream's checks run on it and an OpenCPN maintainer merges it.

Phases 2 and 3 rebuild the branch from scratch. They refuse to push unless the result
matches, byte for byte, the dry run that was reviewed.

## Starting the PR

- **Separate trigger:** start a pipeline with `run_catalogue_pr=true` and
  `release_workflow_id=<deploy workflow ID>`. `catalogue_phase` defaults to `upstream`
  (phase 3); set `fork-test` only for a test inside the fork. This runs
  `catalogue-pr-review`, then `hold-for-catalogue-pr-approval`, then `catalogue-pr-push`.
- **Single trigger (off by default):** set `catalogue_auto_pr=upstream` on the deploy
  trigger to open the PR in the same run. It still only happens after the Cloudsmith upload
  and the dry run both succeed.

The exact settings for each situation are in [`PIPELINE-SETTINGS.md`](PIPELINE-SETTINGS.md).

## Pipeline settings

| Setting | Default | Purpose |
|---|---|---|
| `run_workflow_deploy` | `false` | Start a deploy (build or reuse, review, Cloudsmith, dry run) |
| `run_catalogue_pr` | `false` | Start the PR workflow as a separate step |
| `catalogue_phase` | `upstream` | Phase for the separate trigger: `upstream`, or `fork-test` for a test PR inside the fork |
| `catalogue_auto_pr` | `off` | Single-trigger mode: `off`, `fork-test` or `upstream` |
| `catalogue_fork` | `rgleason/plugins` | Fork the PR branch is pushed to |
| `catalogue_base` | `master` | OpenCPN/plugins branch the PR targets: `master`, `Beta`, `Alpha` or `Experimental` |
| `release_workflow_id` | `""` | In a deploy: the validate workflow whose builds to reuse. In a PR run: the deploy workflow whose published files to use. |
| `publication_channel` | `production` | `production`, or `xweather-alpha` for the xWeather alpha channel |

## One-time setup

**CircleCI:** Organization Settings → Contexts → **`opencpn-plugins-pr`**.

- Restrict it to the weather_routing_pi project (project restriction) and to
  `pipeline.git.branch == "master"` (expression restriction). Security-group restrictions
  need GitHub teams, which a personal account such as rgleason doesn't have.
- Environment variables (only the PR push jobs can read them; spell the names with "-UE"):
  - `CATALOGUE_GITHUB_TOKEN`: phase 3, the normal PR. Required.
  - `CATALOGUE_FORK_TOKEN`: phase 2, the fork test. Only while you need a fork test.

**GitHub:**

- `rgleason/plugins` is the fork of OpenCPN/plugins that the PRs come from.
- **Phase 3 token:** classic, with only the `public_repo` scope, created by rgleason.
  GitHub's fine-grained tokens can't open a PR into a repository you don't own. A
  one-year expiry is fine; note the date.
- **Phase 2 token (only for a fork test):** fine-grained, covering only rgleason/plugins,
  with Contents read/write and Pull requests read/write.
- Either phase refuses a token whose account could write to OpenCPN/plugins directly.
- The script passes the token to git only through environment variables, never on a command
  line, and never prints git commands. It pushes over HTTPS, ignoring CircleCI's rewrite of
  GitHub addresses to its read-only SSH deploy key. If a token ever appears in a log or a
  message, revoke it on GitHub immediately.

## Releasing a new version

Bump `VERSION_PATCH` (or the minor version) in `CMakeLists.txt` on master before deploying.
The catalogue and Cloudsmith use a three-part version, so a tweak-only change counts as the
same release. The pipeline refuses to publish a version that already exists, both on
Cloudsmith (any earlier build of it) and in the catalogue (the same version from another
build). A rebuild of an unchanged version therefore stops before uploading anything.

## Files

| File | What it does |
|---|---|
| `.circleci/config.yml` | Jobs, workflows and pipeline settings |
| `ci/catalogue-pull-request.py` | Builds the catalogue branch, runs the checks, pushes and opens the PR |
| `ci/deploy-reviewed-artifacts.py` | All-or-nothing Cloudsmith upload |
| `ci/check-publication-config.py` | Checks that the pipeline's approval and safety rules haven't been weakened |
| `ci/tests/test_catalogue_pull_request.py`, `ci/tests/test_publication.py` | Tests, run in `publication-contracts` |
| `ci/PIPELINE-SETTINGS.md` | Which trigger settings to use for each situation |
| `ci/catalogue/PULL-REQUESTS.md` | Detailed catalogue PR instructions and trigger commands |

## Status (17 Nov 2026)

- 1.36.6 is on Cloudsmith (`opencpn/weather-routing-prod`, `1.36.6+19399.9c39ac6`), from
  deploy workflow `bb579770-5212-42a0-ae1c-db75aecb1bce`.
- Phase 2 was tested (draft PR rgleason/plugins#11). Phase 3 opened
  [OpenCPN/plugins#1492](https://github.com/OpenCPN/plugins/pull/1492); OpenCPN's check
  passed, and it is waiting for a maintainer to merge.
- `catalogue_phase` now defaults to `upstream`; the phase 2 token has been removed.
- Still to tidy up:
  1. After #1492 is merged, delete its branch from rgleason/plugins.
  2. Close rgleason/plugins#11 and delete its two `catalogue-test/` branches.
  3. Optional: delete the unused Cloudsmith packages `1.36.5+19369.88c905c` (keep
     `1.36.5+19290.802a388`), and re-pin `ci/catalogue/ocpn-plugin.xsd` to clear the
     dry-run warning.
