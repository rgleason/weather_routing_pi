#!/usr/bin/env bash
set -euo pipefail
set +x

if [[ -z "${CLOUDSMITH_API_KEY:-}" ]]; then
  echo "CLOUDSMITH_API_KEY is not available from the deployment context." >&2
  exit 2
fi

python3 ci/prepare-alpha-artifacts.py artifacts alpha-publication \
  "$(git rev-parse HEAD)" "${CIRCLE_BUILD_NUM:?CircleCI build number required}"

python3 - <<'PY'
import json
import subprocess
from pathlib import Path

for item in json.loads(Path("alpha-publication/uploads.json").read_text()):
    subprocess.run([
        "cloudsmith", "push", "raw", "--republish", "--no-wait-for-sync",
        "--name", item["name"], "--version", item["version"],
        "--summary", "xWeatherRouting OpenCPN Alpha preview",
        "pob220/xweather-routing-alpha-oss", item["file"],
    ], check=True)
PY
