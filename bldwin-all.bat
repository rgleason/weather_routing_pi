@echo off
REM bldwin-All
REM Run it from the plugin root directory.

setlocal

REM ============================================================
REM  FE2 Testplugin — Windows Build Script -DOCPN_TARGET=MSVC
REM  Builds Debug + RelWithDebInfo
REM  Auto‑copies plugin DLL/PDB into OpenCPN dev tree
REM  Uses existing wxWidgets build (never rebuilds wxWidgets)
REM ============================================================

REM ------------------------------------------------------------
REM  wxWidgets CONFIG-mode root  
REM ------------------------------------------------------------
set "wxWidgets_ROOT_DIR=C:\Users\fcgle\source\wxWidgets"

REM  Setting WXWIN will force CMake back into legacy mode,
REM  overriding your modern detection logic.
REM  and break pluginconfiguration logic.

REM ------------------------------------------------------------
REM  OpenCPN development tree — ADAPT IF NEEDED
REM ------------------------------------------------------------
set "OCPN_DEV=C:\Users\fcgle\source\opencpn\build\Debug\plugins"

REM ------------------------------------------------------------
REM  Plugin build directory
REM ------------------------------------------------------------
set "PLUGIN_BUILD=build"

REM Clean old build directory
echo Cleaning build directory...
pushd ..
rmdir /s /q "weather_routing_pi\build"
popd
REM del CMakeCache.txt
REM rmdir /s /q "%PLUGIN_BUILD%" 2>nul

REM Make a new build directory and change to it
mkdir "%PLUGIN_BUILD%"
cd "%PLUGIN_BUILD%"

REM ============================================================
REM  CONFIGURE PLUGIN (once)
REM ============================================================
echo.
echo Using wxWidgets from: %wxWidgets_ROOT_DIR%
echo
echo === CONFIGURING PLUGIN ===

cmake -T v143 -A Win32 -DOCPN_TARGET=MSVC ^
  -DwxWidgets_ROOT_DIR="%wxWidgets_ROOT_DIR%" ^
  .. > cmake_configure_log.txt 2>&1

if errorlevel 1 (
    echo CMake configuration failed.
    exit /b 1
)

REM ============================================================
REM  BUILD DEBUG
REM ============================================================
echo.
echo === BUILDING DEBUG ===

cmake --build . --target package --config Debug > buildlog-debug.txt 2>&1

if errorlevel 1 (
    echo Debug build failed.
    exit /b 1
)

REM ============================================================
REM  BUILD RELWITHDEBINFO
REM ============================================================
echo.
echo === BUILDING RELWITHDEBINFO ===

cmake --build . --target package --config RelWithDebInfo > buildlog-rdeb.txt 2>&1

if errorlevel 1 (
    echo RelWithDebInfo build failed.
    exit /b 1
)

REM ============================================================
REM  AUTO‑COPY DLL + PDB INTO OPENCPN DEV TREE
REM ============================================================
echo.
echo === COPYING PLUGIN DLL/PDB INTO OPENCPN DEV TREE ===

for %%C in (Debug RelWithDebInfo) do (
    if exist "%%C\*.dll" copy /Y "%%C\*.dll" "%OCPN_DEV%"
    if exist "%%C\*.pdb" copy /Y "%%C\*.pdb" "%OCPN_DEV%"
)

echo Copy complete.

REM ============================================================
REM  RUN CLOUDSMITH METADATA INJECTION
REM ============================================================
echo.
echo === RUNNING cloudsmith-upload.sh ===
"C:\Program Files\Git\bin\bash.exe" ./cloudsmith-upload.sh

echo.
echo === BUILD COMPLETE ===
echo Debug + RelWithDebInfo builds are ready.
echo Plugin deployed to: %OCPN_DEV%
echo wxWidgets used from: %wxWidgets_ROOT_DIR%
echo.

endlocal

REM NOTES
REM The tarball is always the RelWithDebInfo package.
REM OpenCPN plugin packaging rules:
REM Debug build → used only for local debugging
REM RelWithDebInfo build → used for packaging
REM CPack → always packages the RelWithDebInfo artifacts
REM So this file:
REM     weather_routing_pi-1.15.45.7-msvc-x86-wx32-10.0.26200-MSVC.tar.gz
REM contains:
REM    The RelWithDebInfo DLL
REM    The metadata (after  ./cloudsmith-upload.sh is run)
REM    The XML manifest
REM    The plugin resources
REM    This is the file you would upload to Cloudsmith