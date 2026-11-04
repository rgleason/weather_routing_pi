# Reviewed publication

Rick's repository defaults to the native **WeatherRouting** identity.
An ordinary pipeline validates every platform against both API header versions
without publishing. The normal-installation checks remain enabled.

Publishing requires `run_workflow_deploy=true`. The selected channel binds the
plugin identity and destination:

| Channel | `build_xweather_identity` | Plugin | Cloudsmith repository |
| --- | --- | --- | --- |
| `production` (default) | `false` (default) | WeatherRouting / `weather_routing_pi` | `opencpn/weather-routing-prod` |
| `xweather-alpha` | `true` | xWeatherRouting / `xweather_routing_pi` | `pob220/xweather-routing-alpha-oss` |

A mismatched selection fails the preflight job before the build matrix starts.
Production requests and recovered production artifacts must come from the
reviewed `master` branch of `rgleason/weather_routing_pi`.

## Publish an already-qualified build

Run a pipeline on master with:

```yaml
run_workflow_deploy: true
publication_channel: production
build_xweather_identity: false
release_workflow_id: "<UUID of the completed validation workflow>"
```

The `publish_validated_artifacts` workflow checks the source repository and
exact commit, requires all eleven platforms to have passed with **both** API
1.21 and 1.22 headers, and downloads the original API 1.21 package/XML pairs.
It checks normal desktop/Flatpak runtime receipts and recorded package hashes.
Android retains its packaging/ELF qualification. It does not rebuild plugins or
OpenCPN. A workflow whose publication step failed is usable if all 22 build
jobs passed; a missing or failed build is not usable.

`stage-publication` resolves the destination URLs, embeds matching metadata and
retains `publication.json` and `validated-source.json` with the exact source
revision and package hashes. **Review these artifacts before approving
`hold-for-publication-approval`.** Only `publish-reviewed`, after approval,
receives the existing `xgrib-deployment` context and invokes Cloudsmith. The
context's key must have permission to publish to the selected repository.

If `release_workflow_id` is empty, `build_and_publish_reviewed` builds and tests
the complete matrix first, then follows the same staging/approval/publication
path. The first qualification can still take 30 minutes or more because Linux
builds stock OpenCPN. Artifact reuse avoids repeating that build for publication.
Linux stock-host compilation also uses a bounded compiler cache separated by
distribution/architecture, pinned host source and build setup. Compiler contents,
headers and options still determine cache hits; no relaxed cache matching is
enabled. Configure, link, install and all runtime checks run on each qualification.
Cache statistics are retained with the runtime evidence. Existing Windows SDK
caching remains enabled.

The historical `recover_alpha_deploy` and `recover_alpha_workflow_id` pipeline
parameters have been replaced by the channel and source workflow parameters.
The legacy Alpha scripts remain for compatibility, but Rick's active workflows
use the channel-aware publisher. There is no automatic publication on push or
tag, and the OpenCPN catalogue is not edited by this workflow.

## Local preparation without uploading

```sh
python3 ci/recover-validated-artifacts.py WORKFLOW_UUID qualified-source --channel production
python3 ci/prepare-publication-artifacts.py qualified-source/artifacts reviewed-publication \
  --channel production --build-number 1 --source-receipt qualified-source/validated-source.json
python3 ci/deploy-reviewed-artifacts.py reviewed-publication --channel production --dry-run
```

The dry run checks every staged file and destination without needing deployment
credentials or invoking Cloudsmith. Changing a staged file prevents all uploads.
Preparing packages rewrites publication metadata and retains the tested plugin
libraries and bundled data. The plugin version comes from the qualified build;
this CI correction does not bump it.
