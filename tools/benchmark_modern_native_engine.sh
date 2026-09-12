#!/bin/sh
set -eu

build_dir=${1:-../../build}
repeat=${2:-5}
test_binary="$build_dir/plugins/weather_routing_pi/test/weather_routing_pi_tests"

if [ ! -x "$test_binary" ]; then
  echo "Test binary not found: $test_binary" >&2
  echo "Build target weather_routing_pi_tests first." >&2
  exit 2
fi

echo "Modern native Irish Sea benchmark: $repeat repetitions"
echo "Build: $build_dir"
library_path="$build_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ -x /usr/bin/time ]; then
  LD_LIBRARY_PATH="$library_path" /usr/bin/time \
    -f 'wall=%e s max_rss=%M KiB' \
    "$test_binary" \
    --gtest_filter=ModernNativeEngine.RoutesIrishSeaDeterministically \
    --gtest_repeat="$repeat" \
    --gtest_brief=1
else
  time env LD_LIBRARY_PATH="$library_path" \
    "$test_binary" \
    --gtest_filter=ModernNativeEngine.RoutesIrishSeaDeterministically \
    --gtest_repeat="$repeat" \
    --gtest_brief=1
fi
