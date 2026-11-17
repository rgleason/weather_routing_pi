# CircleCI trigger settings for WeatherRouting

Which parameters to set when you trigger a pipeline. Always trigger on branch **master**.
Any parameter that isn't listed for a scenario stays at its default (see
[Defaults](#defaults)). How the pipeline works is described in
[`RELEASE-PIPELINE.md`](RELEASE-PIPELINE.md).

## Before every release

Bump `VERSION_PATCH` (or the minor version) in `CMakeLists.txt` on master and push it.
The pipeline refuses to publish a version that already exists, both on Cloudsmith and in
the OpenCPN catalogue. `VERSION_TWEAK` doesn't count: the catalogue uses a three-part version.

## Scenarios

### 1. Ordinary push

No parameters. The `validate` workflow runs on its own.

### 2. Release, with the PR as a separate step

Two triggers, two approvals.

**Step A: deploy**

| Parameter | Value |
|---|---|
| `run_workflow_deploy` | `true` |
| `release_workflow_id` | The **validate** workflow ID of the release commit (all builds green). Leave it empty to build fresh instead. |

You approve the publication, it uploads to Cloudsmith, and the phase 1 dry run runs
automatically. Check its `catalogue-pr/SUMMARY.txt`.

**Step B: PR to OpenCPN**

| Parameter | Value |
|---|---|
| `run_catalogue_pr` | `true` |
| `release_workflow_id` | The **deploy** workflow ID from step A (the one with `publish-reviewed`) |

`catalogue_phase` defaults to `upstream`, so leave it. Check the review's summary, approve
`hold-for-catalogue-pr-approval`, and the PR opens on OpenCPN/plugins.

### 3. Release in one trigger (once you trust it)

| Parameter | Value |
|---|---|
| `run_workflow_deploy` | `true` |
| `release_workflow_id` | The **validate** workflow ID of the release commit, or empty to build fresh |
| `catalogue_auto_pr` | `upstream` |

One approval covers everything. After the upload and the dry run succeed, the OpenCPN PR
opens without a second hold, so read the dry-run summary when the job finishes.

### 4. Test PR inside your fork (rarely needed)

As step 2B, plus `catalogue_phase` = `fork-test`. It opens a draft PR inside
rgleason/plugins and leaves OpenCPN/plugins alone. It needs `CATALOGUE_FORK_TOKEN` in the
`opencpn-plugins-pr` context again. Afterwards, close the test PR and delete its two
`catalogue-test/` branches.

### All scenarios at a glance

| Scenario | Step | Parameter | Value | Notes |
|---|---|---|---|---|
| 1. Ordinary push | — | (none) | — | The validate workflow runs on its own. |
| 2. Release, PR as a separate step | A: Deploy | `run_workflow_deploy` | `true` | Approve the publication; Cloudsmith upload and phase 1 dry run follow. Check `SUMMARY.txt`. |
| | A: Deploy | `release_workflow_id` | Validate workflow ID of the release commit, or empty to build fresh | Reuses that workflow's builds. |
| | B: PR to OpenCPN | `run_catalogue_pr` | `true` | `catalogue_phase` defaults to `upstream`. Check the summary, approve the hold. |
| | B: PR to OpenCPN | `release_workflow_id` | Deploy workflow ID from step A | Reuses what step A published. |
| 3. Release in one trigger | Deploy + PR | `run_workflow_deploy` | `true` | One approval covers everything. |
| | Deploy + PR | `release_workflow_id` | Validate workflow ID, or empty to build fresh | |
| | Deploy + PR | `catalogue_auto_pr` | `upstream` | The PR opens without a second hold. Read the dry-run summary afterwards. |
| 4. Test PR inside the fork (rare) | PR | `run_catalogue_pr` | `true` | |
| | PR | `release_workflow_id` | Deploy workflow ID | |
| | PR | `catalogue_phase` | `fork-test` | Needs `CATALOGUE_FORK_TOKEN` in the context. |

## Defaults

| Parameter | Default | Change it only if |
|---|---|---|
| Branch (trigger dialog) | `master` | Never: production deploys and the token context are meant for master only. |
| `publication_channel` | `production` | You are releasing the xWeather alpha instead: set `xweather-alpha`. |
| `catalogue_fork` | `rgleason/plugins` | The PR branch should come from a different fork. |
| `catalogue_base` | `master` | You are targeting a test catalogue: Beta, Alpha or Experimental. |
| `catalogue_auto_pr` | `off` | You want scenario 3 (one trigger): set `upstream`. |
| `catalogue_phase` | `upstream` | You want a test PR inside the fork: set `fork-test`. |
| `run_workflow_deploy` | `false` | You are deploying a release (scenarios 2A and 3). |
| `run_catalogue_pr` | `false` | You are opening the PR as a separate step (scenarios 2B and 4). |

## Which workflow ID goes where

`release_workflow_id` means two different workflows:

- **In a deploy** (2A, 3) it is a **validate** workflow, so the deploy reuses its builds.
- **In a PR run** (2B, 4) it is the **deploy** workflow, so the PR uses exactly what was
  published. Use step A's deploy ID in step B.

To find an ID, open the workflow in CircleCI. The address bar shows
`…/pipelines/github/rgleason/weather_routing_pi/<number>/workflows/<workflow-id>`: the ID is
the long string after `/workflows/`, not the pipeline number.

## What to check

- **Before approving the publication:** in `stage-publication` → Artifacts →
  `reviewed-publication/publication.json`, check that `channel` is `production`, the upload
  names carry the new version, and `source_revision` is the release commit.
- **After the dry run:** `catalogue-pr/SUMMARY.txt` lists the old version's files removed
  and the new version's added, one per platform (11 each). The only expected warning is
  that `ci/catalogue/ocpn-plugin.xsd` differs from upstream.

## Tokens

They live in the CircleCI context `opencpn-plugins-pr` (Organization Settings → Contexts),
restricted to this project and to `pipeline.git.branch == "master"`.

| Variable | Used by | Token |
|---|---|---|
| `CATALOGUE_GITHUB_TOKEN` | Scenarios 2B and 3 (upstream PR) | Classic, only `public_repo`, created by rgleason |
| `CATALOGUE_FORK_TOKEN` | Scenario 4 only (fork test) | Fine-grained, only rgleason/plugins, Contents and Pull requests read/write. Not kept normally. |

The names are spelled with "-UE", exactly as above. If a token ever appears in a log or a
message, revoke it on GitHub immediately and make a new one.
