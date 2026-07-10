# Build notes

- Target branch: `real-wr-refactor-engine-ui`
- Base commit: `bc8a8a4bab49073ebe8319cead431111843dac2c` (`Update TP1.0.362`)
- Target repository: `https://github.com/rgleason/weather_routing_pi.git`
- OpenCPN source inspected read-only: `/home/paul/src/OpenCPN`
- OpenCPN build inspected read-only: `/home/paul/src/OpenCPN/build`
- OpenCPN version supplied/verified: 5.15.0
- Local OpenCPN plugin API: 1.21, from `/home/paul/src/OpenCPN/include/ocpn_plugin.h`
- Plugin-declared and bundled build API: 1.18
- Compiler: GCC/G++ 16.1.1
- CMake: 4.3.4
- wxWidgets: 3.2.10, GTK 3

## Build commands

```sh
git submodule update --init --recursive
cmake -S . -B build-isolated -DCMAKE_BUILD_TYPE=Debug
cmake --build build-isolated -j4
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j4
```

## Result

Both Debug and clean Release standalone builds completed successfully. The
Release output is `build-release/libweather_routing_pi.so`.

The compile fixes restore the missing `BoatDialog::OnUpPolar` function name,
use an unambiguous `wxString` fallback in the route filter, and include
`<cstdint>` before direct inclusion of the bundled OpenCPN API header. No
OpenCPN source/build tree or bundled `opencpn-libs` source was modified.

## Remaining limitations

- This verifies standalone compilation and linking only; the plugin was not
  installed into or runtime-tested with OpenCPN.
- Existing compiler warnings (primarily member initialization order and
  signed/unsigned comparisons) remain unchanged because they do not block the
  build and are outside this focused compile fix.
- Runtime compatibility with OpenCPN plugin API 1.21 remains to be verified;
  this branch still declares and builds against plugin API 1.18.
