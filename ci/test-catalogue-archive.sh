#!/usr/bin/env bash
set -euo pipefail

build_dir=${1:?package build directory is required}
stage_dir=${2:?stage directory is required}

package_name=weather_routing_pi
plugin_name=WeatherRouting
other_package_name=xweather_routing_pi
if grep -q '^WEATHER_ROUTING_XWEATHER_IDENTITY:BOOL=ON$' \
    "$build_dir/CMakeCache.txt"; then
  package_name=xweather_routing_pi
  plugin_name=xWeatherRouting
  other_package_name=weather_routing_pi
fi

mapfile -t archives < <(
  find "$build_dir" -maxdepth 1 -type f -name "${package_name}-*.tar.gz" |
    sort
)
test "${#archives[@]}" -eq 1
archive=${archives[0]}
listing=$(mktemp)
trap 'rm -f "$listing"' EXIT
tar -tzf "$archive" >"$listing"

test "$(grep -c "/lib/opencpn/lib${package_name}\\.so$" "$listing")" -eq 1
grep -q "/share/opencpn/plugins/${package_name}/data/" "$listing"
grep -q "/LC_MESSAGES/opencpn-${package_name}\\.mo$" "$listing"
if grep -Eqi "/libg(test|mock)|/lib${other_package_name}\\.so|opencpn-${other_package_name}\\.mo" \
    "$listing"; then
  echo "Archive contains a test library or the other plugin identity" >&2
  exit 1
fi

plugin="$stage_dir/usr/lib/opencpn/lib${package_name}.so"
test -f "$plugin"
if nm -D --undefined-only "$plugin" |
    grep -E 'PlugIn_(CheckSegmentSafety|Prewarm|ServiceSegmentSafety|RegisterSegmentSafety)'; then
  echo "Plugin has a direct dependency on the optional enhanced host API" >&2
  exit 1
fi

mapfile -t metadata < <(
  find "$build_dir" -maxdepth 1 -type f -name "${package_name}-*.xml" |
    sort
)
test "${#metadata[@]}" -eq 1
grep -q "<name> ${plugin_name} </name>" "${metadata[0]}"
grep -q '<api-version> 1.21 </api-version>' "${metadata[0]}"
echo "Catalogue archive and stock-host ABI contract validated: $archive"
