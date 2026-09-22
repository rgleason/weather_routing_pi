::
:: Build the MSVC artifacts for OpenCPN plugins
:: Supports flexible CONFIGURATION (Release / RelWithDebInfo)
::

@echo off
setlocal

set "CONFIGURATION=RelWithDebInfo"

goto :main

REM ------------------------------------------------------------
REM Helper: Fail early if a command fails
REM ------------------------------------------------------------
:check_error
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: %1
    exit /b 1
)
goto :eof

REM ------------------------------------------------------------
REM Helper: Fail if a file does not exist
REM ------------------------------------------------------------
:require_file
if not exist "%~1" (
    echo ERROR: Required file not found: %~1
    exit /b 1
)
goto :eof


REM ------------------------------------------------------------
REM  Setup basic environment
REM ------------------------------------------------------------
:main
set "SCRIPTDIR=%~dp0"
set "GIT_HOME=C:\Program Files\Git"

REM Default to RelWithDebInfo if not provided
if "%CONFIGURATION%"=="" set "CONFIGURATION=RelWithDebInfo"

set "wx_vers=wx%WX_VER%"
echo Building %wx_vers% using MSVC %MSVC_VERSION%
echo Build configuration: %CONFIGURATION%

REM Add local tools to PATH
set "PATH=%SCRIPTDIR%.local\bin;%PATH%"
echo SCRIPTDIR: %SCRIPTDIR%
dir "%SCRIPTDIR%"
dir "%SCRIPTDIR%.."
dir "%SCRIPTDIR%..\msvc"

REM ------------------------------------------------------------
REM  wxWidgets + dependency setup
REM ------------------------------------------------------------
call "%SCRIPTDIR%..\msvc\win_deps.bat" %wx_vers%

REM Add CMake and wx-config to PATH
set "PATH=%SCRIPTDIR%.local\bin;%PATH%;C:\Program Files\CMake\bin"
call "%SCRIPTDIR%..\cache\wx-config.bat"

REM Extra PATH from wx-config
set "PATH=%EXTRA_PATH%;%PATH%"
echo EXTRA_PATH: %EXTRA_PATH%
echo wxWidgets_LIB_DIR: %wxWidgets_LIB_DIR%
echo wxWidgets_ROOT_DIR: %wxWidgets_ROOT_DIR%
echo OCPN_TARGET_TUPLE: %TARGET_TUPLE%

REM ------------------------------------------------------------
REM  Ensure MSVC environment is active
REM ------------------------------------------------------------
nmake /? >nul 2>&1
if errorlevel 1 (
  set "VS_HOME=C:\Program Files\Microsoft Visual Studio\2022"
  call "%VS_HOME%\Community\VC\Auxiliary\Build\vcvars32.bat"
)

REM ------------------------------------------------------------
REM  Update required submodules
REM ------------------------------------------------------------
git submodule update --init opencpn-libs
dir

REM ------------------------------------------------------------
REM  Create fresh build directory
REM ------------------------------------------------------------
if exist build (rmdir /s /q build)
mkdir build
cd build
dir

REM ------------------------------------------------------------
REM Download OpenCPN Windows toolchain + NSIS
REM ------------------------------------------------------------
echo Downloading opencpn.lib
wget https://sourceforge.net/projects/opencpnplugins/files/opencpn.lib
call :check_error "Failed to download opencpn.lib"
call :require_file "opencpn.lib"

echo Downloading OpenCPN_buildwin
wget https://download.opencpn.org/s/oibxM3kzfzKcSc3/download/OpenCPN_buildwin-4.99a.7z
call :check_error "Failed to download OpenCPN_buildwin"
call :require_file "OpenCPN_buildwin-4.99a.7z"

echo Extracting OpenCPN_buildwin
7z x -y OpenCPN_buildwin-4.99a.7z -o..\buildwin
call :check_error "Failed to extract OpenCPN_buildwin"

echo Downloading NSIS installer
wget https://download.opencpn.org/s/54HsBDLNzRZLL6i/download/nsis-3.04-setup.exe
call :check_error "Failed to download NSIS"
call :require_file "nsis-3.04-setup.exe"

echo Installing NSIS
nsis-3.04-setup.exe /S
call :check_error "Failed to install NSIS"

echo Checking for Poedit installation
poedit -version >nul 2>&1
call :check_error "Poedit is not installed or not in PATH"
echo Poedit check complete

REM ------------------------------------------------------------
REM  Install vcpkg + gettext
REM ------------------------------------------------------------
echo Installing vcpkg
set "VCPKG_ROOT=%CD%\vcpkg"

git clone https://github.com/microsoft/vcpkg "%VCPKG_ROOT%"
call :check_error "Failed to clone vcpkg repository"

call "%VCPKG_ROOT%\bootstrap-vcpkg.bat"
call :check_error "vcpkg bootstrap failed"

echo Installing gettext
"%VCPKG_ROOT%\vcpkg" install gettext:x86-windows
call :check_error "Failed to install gettext via vcpkg"
echo gettext installed

REM ------------------------------------------------------------
REM  Configure CMake project
REM ------------------------------------------------------------
echo Configuring CMake project

if "%MSVC_VERSION%"=="2019" (
  cmake -T v141_xp -G "Visual Studio 16 2019" ^
    -DCMAKE_GENERATOR_PLATFORM=Win32 ^
    -DCMAKE_BUILD_TYPE=%CONFIGURATION% ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    -DGETTEXT_MSGMERGE_EXECUTABLE=%VCPKG_ROOT%/installed/x86-windows/tools/gettext/msgmerge.exe ^
    -DGETTEXT_MSGFMT_EXECUTABLE=%VCPKG_ROOT%/installed/x86-windows/tools/gettext/msgfmt.exe ^
    ..
) else (
  cmake -A Win32 -G "Visual Studio 17 2022" ^
    -DCMAKE_GENERATOR_PLATFORM=Win32 ^
    -DCMAKE_BUILD_TYPE=%CONFIGURATION% ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    -DGETTEXT_MSGMERGE_EXECUTABLE=%VCPKG_ROOT%/installed/x86-windows/tools/gettext/msgmerge.exe ^
    -DGETTEXT_MSGFMT_EXECUTABLE=%VCPKG_ROOT%/installed/x86-windows/tools/gettext/msgfmt.exe ^
    ..
)

call :check_error "CMake configuration failed"


REM ------------------------------------------------------------
REM  Build plugin + install staging directory
REM ------------------------------------------------------------
echo Building plugin
cmake --build . --config %CONFIGURATION%
call :check_error "MSVC build failed"

echo Installing plugin into staging directory
cmake --build . --target INSTALL --config %CONFIGURATION%
call :check_error "Install step failed"

REM ------------------------------------------------------------
REM  Run CPack to generate TGZ package
REM ------------------------------------------------------------
echo Running CPack
cpack -G TGZ
call :check_error "CPack failed to generate TGZ package"


REM ------------------------------------------------------------
REM  Locate CPack output and copy tarball to top-level build directory
REM ------------------------------------------------------------
echo Searching for CPack TGZ output
set "TARBALL_PATH="

for /r "%CD%\_CPack_Packages" %%f in (*.tar.gz) do (
    echo Found package: %%f
    set "TARBALL_PATH=%%f"
)

if "%TARBALL_PATH%"=="" (
    echo ERROR: No TGZ package found under _CPack_Packages.
    echo CPack likely failed before producing an artifact.
    exit /b 1
)

copy "%TARBALL_PATH%" "%CD%"
call :check_error "Failed to copy TGZ package to build directory"

echo Copied TGZ package to build directory.


REM ------------------------------------------------------------
REM  Automatically detect DLL configuration (Release / RelWithDebInfo / Debug)
REM ------------------------------------------------------------
echo Detecting DLL location...

REM Enable delayed expansion so variables inside FOR loops update correctly
setlocal enabledelayedexpansion

set "DLL_PATH="
set "DLL_CONFIG="

for %%C in (Release RelWithDebInfo Debug MinSizeRel) do (
    if exist "%CD%\%%C\weather_routing_pi.dll" (
        set "DLL_PATH=%CD%\%%C\weather_routing_pi.dll"
        set "DLL_CONFIG=%%C"
    )
)

REM Restore normal expansion
endlocal & set "DLL_PATH=%DLL_PATH%" & set "DLL_CONFIG=%DLL_CONFIG%"

if "%DLL_PATH%"=="" (
    echo ERROR: No DLL found in any configuration directory.
    echo Searched: Release, RelWithDebInfo, Debug, MinSizeRel
    exit /b 1
)

echo Found DLL in configuration: %DLL_CONFIG%
echo DLL path: %DLL_PATH%

copy "%DLL_PATH%" "%CD%"
call :check_error "Failed to copy DLL to build directory"

echo DLL copied to build directory.

REM ------------------------------------------------------------
REM Summary of produced artifacts
REM ------------------------------------------------------------
echo.
echo ------------------------------------------------------------
echo Build Summary
echo ------------------------------------------------------------

if exist "%CD%\weather_routing_pi.dll" (
    echo DLL: weather_routing_pi.dll
) else (
    echo DLL: NOT FOUND
)

for %%f in ("%CD%\weather_routing_pi-*.xml") do (
    echo XML: %%~nxf
)

for %%f in ("%CD%\weather_routing_pi-*.tar.gz") do (
    echo TGZ: %%~nxf
)

echo ------------------------------------------------------------
echo Summary complete.
echo.

endlocal
