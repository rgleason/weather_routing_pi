#!/usr/bin/env bash
# Embed the generated same-version metadata into a completed package archive.
set -euo pipefail

build_dir=${1:?build directory is required}
package_name=weather_routing_pi
if [[ -f "$build_dir/CMakeCache.txt" ]] &&
    grep -q '^WEATHER_ROUTING_XWEATHER_IDENTITY:BOOL=ON$' "$build_dir/CMakeCache.txt"; then
  package_name=xweather_routing_pi
fi

shopt -s nullglob
archives=("$build_dir"/"${package_name}"-*.tar.gz)
metadata_files=("$build_dir"/"${package_name}"-*.xml)
test "${#archives[@]}" -eq 1
test "${#metadata_files[@]}" -eq 1

python3 "$(dirname "$0")/embed-package-metadata.py" \
  "${archives[0]}" "${metadata_files[0]}" "${archives[0]}"
python3 - "${archives[0]}" "${metadata_files[0]}" <<'PY'
import sys
import tarfile
import xml.etree.ElementTree as ET
from pathlib import Path

archive, metadata = map(Path, sys.argv[1:])
with tarfile.open(archive, "r:gz") as package:
    if package.getnames().count("metadata.xml") != 1:
        raise SystemExit("archive does not contain exactly one metadata.xml")
    embedded = package.extractfile("metadata.xml").read()
if ET.tostring(ET.fromstring(embedded)) != ET.tostring(ET.parse(metadata).getroot()):
    raise SystemExit("embedded metadata differs from generated metadata")
PY
