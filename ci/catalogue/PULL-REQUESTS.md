# Publishing catalogue metadata to OpenCPN/plugins

`ci/catalogue-pull-request.py` turns the reviewed, Cloudsmith-published
release into a pull request against https://github.com/OpenCPN/plugins.
The XML files always come from the `reviewed-publication` artifact (the same
bytes that were reviewed and uploaded to Cloudsmith). They are hash-checked
against its `publication.json`, so the XML stays a CircleCI artifact end to end.

## Sequence

1. **Validate.** The build jobs and `stage-publication` validate every XML
   (identity, targets, embedded/external match, pinned XSD).
2. **Review and approve.** `hold-for-publication-approval`.
3. **Deploy.** `publish-reviewed` uploads the tarballs and XML to Cloudsmith
   **all or nothing**. Before anything is uploaded, every one of the 22
   files must pass two checks: Cloudsmith's own upload check, and a search
   showing that the name and version don't exist yet. Uploads then wait for
   Cloudsmith to finish processing each file. If any upload fails, every
   package created by that run is deleted again, and a re-run starts clean.
   `rollback.json` and `published.json` record what happened. If a rollback
   delete itself fails, the job lists the packages to delete by hand.
4. **Phase 1, dry run.** `catalogue-pr-dry-run` runs automatically after every
   publish. It needs no secrets and pushes nothing. It:
   - clones OpenCPN/plugins `master`; your fork is not used because it may be stale
   - creates branch `weather_routing_pi-<version>-build<N>`
   - removes the older WeatherRouting XML for the same targets and refuses downgrades
   - adds the new XML, named like the existing entries:
     `weather_routing_pi-<ver>-weather_routing_pi-<ver>-<target>-metadata.xml`
   - runs upstream's own PR checks: xmllint with `ocpn-plugin.xsd` and
     `ocpn-plugins.xsd`, `tools/check-metadata-urls`, and `git diff --check`

   The results are stored as artifacts under `catalogue-pr/`:
   - `SUMMARY.txt`
   - `report.json`
   - `pull-request.md`
   - `catalogue.patch` and `catalogue.diff`
   - `metadata/*.xml`
5. **Phase 2, fork test.** Pushes the branch and a copy of the upstream base
   to the fork as `catalogue-test/...`, then opens a **draft PR inside the
   fork**. OpenCPN/plugins is not touched. The token must *not* be able to
   write to OpenCPN/plugins.
6. **Phase 3, upstream.** Pushes the branch to the fork and opens the PR
   `rgleason:<branch> -> OpenCPN/plugins:master`.

Phases 2 and 3 run in their own workflow, `publish_catalogue_pr`. It runs only
when you trigger it, and it has its own approval:

    catalogue-pr-review  ->  hold-for-catalogue-pr-approval  ->  catalogue-pr-push

`catalogue-pr-review` recovers `reviewed-publication` from the deploy workflow
and rebuilds the branch against the current upstream. At this stage the
Cloudsmith URLs must be live. Review its `catalogue-pr/` artifacts, then approve.

`catalogue-pr-push` rebuilds the branch again and **refuses to push unless
the diff is byte-identical to the one you approved**. Only this job holds the
GitHub token.

## Single-trigger mode (off by default)

Set `catalogue_auto_pr` to `fork-test` or `upstream` on the deploy trigger:

    your trigger → build → validate → approve once → Cloudsmith (all or nothing)
      → dry run → catalogue-pr-auto (push + PR)

`catalogue-pr-auto` is skipped while the setting is `off`. When it is on,
the job runs only after the Cloudsmith publish and the dry run have both
succeeded. It still rebuilds the branch, requires every URL to be live and
the diff to match the dry run, and it is the only job that holds the GitHub
token. Turn it on for `fork-test` first, then `upstream`, after the separate
trigger has worked a few times.

## One-time setup

Create a CircleCI context named `opencpn-plugins-pr`. Restrict it to a
security group that only you belong to. If your plan offers context
expression restrictions, also limit it to `pipeline.git.branch == "master"`.

Add two environment variables to the context:

- `CATALOGUE_FORK_TOKEN` (phase 2): a fine-grained token for
  rgleason/plugins *only*, with these permissions:
  - Contents: read/write
  - Pull requests: read/write
- `CATALOGUE_GITHUB_TOKEN` (phase 3): a token that can open a PR on
  OpenCPN/plugins. GitHub's fine-grained tokens cannot open cross-fork PRs
  into a repository you don't own, so this is normally a classic token with
  `public_repo` scope. The PR is only opened: upstream's checks run on it and
  an OpenCPN maintainer merges it. Every phase refuses a token whose account
  can write to OpenCPN/plugins.

## Triggering phases 2 and 3

Use the workflow ID of the deploy workflow, the one that ran `publish-reviewed`:

    curl -X POST https://circleci.com/api/v2/project/gh/rgleason/weather_routing_pi/pipeline \
      -H "Circle-Token: $CIRCLE_TOKEN" -H 'Content-Type: application/json' \
      -d '{"branch":"master","parameters":{"run_catalogue_pr":true,
           "release_workflow_id":"<deploy workflow uuid>","catalogue_phase":"fork-test"}}'

Repeat with `"catalogue_phase":"upstream"` once the phase 2 draft PR looks
right. After that, close the test PR and delete the two `catalogue-test/` branches.

## Running locally

    python3 ci/catalogue-pull-request.py prepare <reviewed-publication> out/catalogue-pr
    python3 -m unittest discover -s ci/tests -p 'test_catalogue*'
