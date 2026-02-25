```markdown
# Windows build by Action

## Goal
Build **weather_routing_pi** on **Windows** using **GitHub Actions** with:
- **MSVC**
- **Win32 (x86)** target
- **wxWidgets** found by CMake (`find_package(wxWidgets ...)`)

## How the repo finds wxWidgets (Windows)
The wxWidgets discovery is done by CMake logic (not by a custom Windows-only finder in `cmake/`).

- `cmake/PluginConfigure.cmake` calls:

 ```cmake
 find_package(wxWidgets REQUIRED COMPONENTS ${wxWidgets_USE_LIBS})
 include(${wxWidgets_USE_FILE})
 ```

 It also configures options like `wxWidgets_USE_UNICODE`, `wxWidgets_USE_STATIC`, etc., and adjusts the library list (e.g., removes `GLU` on Windows).

- On Windows, builds typically succeed by *providing hints* to CMake’s `FindwxWidgets.cmake` using variables set before configuring:
 - `wxWidgets_ROOT_DIR`
 - `wxWidgets_LIB_DIR`
 - often `WXWIN`

 Example (from existing scripts/CI patterns) passes these to CMake:
 - `-DwxWidgets_ROOT_DIR=...`
 - `-DwxWidgets_LIB_DIR=...`

## Recommended GitHub Actions approach
Use **vcpkg** to install wxWidgets (x86), then configure CMake with `-A Win32` and point `wxWidgets_ROOT_DIR` and `wxWidgets_LIB_DIR` at the vcpkg install.

### Add `vcpkg.json` (repo root)
```json
{
 "name": "weather-routing-pi",
 "version-string": "0.0.0",
 "dependencies": ["wxwidgets"]
}
```

### Add workflow: `.github/workflows/windows-win32-wxwidgets.yml`
```yaml
name: windows-win32-wxwidgets

on:
 workflow_dispatch:
 push:
 pull_request:

jobs:
 build-win32:
 runs-on: windows-2022

 steps:
 - uses: actions/checkout@v4

 - name: MSVC dev cmd (x86)
 uses: ilammy/msvc-dev-cmd@v1
 with:
 arch: x86

 - name: Install wxWidgets (vcpkg x86-windows)
 uses: lukka/run-vcpkg@v11
 with:
 vcpkgTriplet: x86-windows
 runVcpkgInstall: true
 vcpkgJsonGlob: vcpkg.json

 - name: Configure (CMake, Win32)
 shell: pwsh
 run: |
 cmake -S . -B build `
 -G "Visual Studio 17 2022" -A Win32 -T v143 `
 -DOCPN_TARGET=MSVC `
 -DwxWidgets_ROOT_DIR="${env:VCPKG_INSTALLED_DIR}\x86-windows" `
 -DwxWidgets_LIB_DIR="${env:VCPKG_INSTALLED_DIR}\x86-windows\lib"

 - name: Build (RelWithDebInfo)
 run: cmake --build build --target package --config RelWithDebInfo

 - name: Upload artifact
 uses: actions/upload-artifact@v4
 with:
 name: windows-win32-package
 path: build/**
```

## Notes / follow-ups
- Confirm whether the build requires **wxWidgets DLL** layout (like `vc14x_dll`/`vc_dll`) or if vcpkg’s layout is acceptable.
- If DLL-specific output is required, we may need:
 - a custom vcpkg triplet/features, or
 - building wxWidgets from source in the workflow (more complex but closer to local scripts).

## Chat retention (GitHub.com)
GitHub.com Copilot Chat generally keeps conversation history, but org retention policies can affect it. For a permanent record, store this markdown in the repo (e.g., `docs/windows-build-by-action.md`).
```