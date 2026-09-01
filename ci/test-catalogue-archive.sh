#!/usr/bin/env bash
set -euo pipefail

build_dir=${1:?package build directory is required}
stage_dir=${2:?stage directory is required}

mapfile -t archives < <(
  find "$build_dir" -maxdepth 1 -type f -name 'weather_routing_pi-*.tar.gz' |
    sort
)
test "${#archives[@]}" -eq 1
archive=${archives[0]}
listing=$(mktemp)
trap 'rm -f "$listing"' EXIT
tar -tzf "$archive" >"$listing"

test "$(grep -c '/lib/opencpn/libweather_routing_pi.so$' "$listing")" -eq 1
grep -q '/share/opencpn/plugins/weather_routing_pi/data/' "$listing"
grep -q '/LC_MESSAGES/opencpn-weather_routing_pi.mo$' "$listing"
if grep -Eqi '/libg(test|mock)|/libxweather_routing_pi\.so|opencpn-xweather_routing_pi\.mo' "$listing"; then
  echo "Archive contains a test library or preview plugin identity" >&2
  exit 1
fi

plugin="$stage_dir/usr/lib/opencpn/libweather_routing_pi.so"
test -f "$plugin"
if nm -D --undefined-only "$plugin" |
    grep -E 'PlugIn_(CheckSegmentSafety|Prewarm|ServiceSegmentSafety|RegisterSegmentSafety)'; then
  echo "Plugin has a direct dependency on the optional enhanced host API" >&2
  exit 1
fi

mapfile -t metadata < <(
  find "$build_dir" -maxdepth 1 -type f -name 'weather_routing_pi-*.xml' |
    sort
)
test "${#metadata[@]}" -eq 1
grep -q '<name> WeatherRouting </name>' "${metadata[0]}"
grep -q '<api-version> 1.21 </api-version>' "${metadata[0]}"
echo "Catalogue archive and stock-host ABI contract validated: $archive"
