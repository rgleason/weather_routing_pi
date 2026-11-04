#!/usr/bin/env bash
# Complete the reviewable publication before the approval hold.
set -euo pipefail

release_source_workflow=${WR_RELEASE_WORKFLOW_ID:-${CIRCLE_WORKFLOW_ID:?}}
python3 -m unittest discover -s ci/tests -v
python3 ci/recover-validated-artifacts.py "$release_source_workflow" qualified-source \
  --channel "${WR_RELEASE_CHANNEL:?}"
python3 ci/prepare-publication-artifacts.py qualified-source/artifacts reviewed-publication \
  --channel "$WR_RELEASE_CHANNEL" --build-number "${CIRCLE_BUILD_NUM:?}" \
  --source-receipt qualified-source/validated-source.json
python3 ci/validate-catalogue-packages.py reviewed-publication
python3 ci/deploy-reviewed-artifacts.py reviewed-publication \
  --channel "$WR_RELEASE_CHANNEL" --dry-run
