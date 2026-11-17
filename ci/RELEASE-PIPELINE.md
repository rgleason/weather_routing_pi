# WeatherRouting release pipeline: CircleCI → Cloudsmith → OpenCPN/plugins

This pipeline takes one build all the way to a pull request on OpenCPN/plugins.
It stops for your review before anything is published. The pipeline is defined
in `.circleci/config.yml`. The tokens are kept in one CircleCI context. Step-by-step
commands for the catalogue PR are in [`catalogue/PULL-REQUESTS.md`](catalogue/PULL-REQUESTS.md).

## The flow

```
your trigger → build all platforms → validate XML → hold: you approve
  → Cloudsmith upload (all or nothing) → Phase 1 dry run
  → Phase 2/3: push branch to rgleason/plugins and open PR → upstream checks → maintainer merges
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
5. **Phase 2, fork test.** Pushes the branch to rgleason/plugins and opens a draft PR
   *inside the fork*. OpenCPN/plugins is not touched.
6. **Phase 3, upstream.** Opens the PR from `rgleason:<branch>` to `OpenCPN/plugins:master`.
   Upstream's checks run on it and an OpenCPN maintainer merges it.

Phases 2 and 3 rebuild the branch from scratch. They refuse to push unless the result
matches, byte for byte, the dry run that was reviewed.

## Starting phases 2 and 3

- **Separate trigger (current mode):** start a pipeline with `run_catalogue_pr=true`,
  `release_workflow_id=<deploy workflow ID>` and `catalogue_phase=fork-test` or `upstream`.
  This runs `catalogue-pr-review`, then `hold-for-catalogue-pr-approval`, then `catalogue-pr-push`.
- **Single trigger (off by default):** set `catalogue_auto_pr=fork-test` or `upstream` on the
  deploy trigger to open the PR in the same run. It still only happens after the Cloudsmith
  upload and the dry run both succeed.

## Pipeline settings

| Setting | Default | Purpose |
|---|---|---|
| `run_catalogue_pr` | `false` | Start the phase 2/3 workflow |
| `catalogue_phase` | `fork-test` | Phase for the separate trigger: `fork-test` or `upstream` |
| `catalogue_auto_pr` | `"off"` | Single-trigger mode: `off`, `fork-test` or `upstream` |
| `catalogue_fork` | `rgleason/plugins` | Fork the PR branch is pushed to |
| `catalogue_base` | `master` | OpenCPN/plugins branch the PR targets |
| `release_workflow_id` | `""` | Deploy workflow whose published files are used |

## One-time setup

**CircleCI:** Organization Settings → Contexts → **`opencpn-plugins-pr`**.

- Restrict it to the weather_routing_pi project.
- Before phase 3, also add the expression restriction `pipeline.git.branch == "master"`.
- Environment variables (only the PR push job can read them):
  - `CATALOGUE_FORK_TOKEN`: phase 2
  - `CATALOGUE_GITHUB_TOKEN`: phase 3

**GitHub:**

- `rgleason/plugins` is the fork of OpenCPN/plugins that the PRs come from.
- **Phase 2 token:** fine-grained, covering only rgleason/plugins, with Contents read/write
  and Pull requests read/write.
- **Phase 3 token:** classic, with `public_repo` scope. GitHub's fine-grained tokens can't
  open a PR into a repository you don't own.
- Either phase refuses a token that could write to OpenCPN/plugins directly.

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
| `ci/catalogue/PULL-REQUESTS.md` | Detailed catalogue PR instructions and trigger commands |

## Status (16 Nov 2026)

- Done: the pipeline is merged to master on rgleason/weather_routing_pi, and the CircleCI
  context and phase 2 token are set up.
- Next:
  1. Bump to 1.36.6.
  2. Run a production deploy.
  3. Review the dry run's `catalogue-pr/SUMMARY.txt`.
  4. Run phase 2.
  5. Run phase 3.
