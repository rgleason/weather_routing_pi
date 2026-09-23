::
:: Build the MSVC artifacts for OpenCPN plugins
:: Supports flexible CONFIGURATION (Release / RelWithDebInfo)
::

@echo off
setlocal

set "CONFIGURATION=RelWithDebInfo"
REM set VCPKG_ROOT=C:\Users\circleci\vcpkg
REM Do NOT prepend global vcpkg to PATH
REM We will use the local vcpkg inside build/


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

:main

REM ------------------------------------------------------------
REM  Setup basic environment
REM ------------------------------------------------------------
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
call :check_error "win_deps.bat failed"

REM Add CMake and wx-config to PATH
set "PATH=%SCRIPTDIR%.local\bin;%PATH%;C:\Program Files\CMake\bin"
call "%SCRIPTDIR%..\cache\wx-config.bat"
call :check_error "wx-config.bat failed"

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
  call :check_error "Failed to load MSVC environment"  
)

REM ------------------------------------------------------------
REM  Update required submodules
REM ------------------------------------------------------------
git submodule update --init opencpn-libs
call :check_error "Submodule update failed"
dir

REM ------------------------------------------------------------
REM  Create fresh build directory
REM ------------------------------------------------------------
if exist build (rmdir /s /q build)
mkdir build
call :check_error "Failed to create build directory"
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
if %ERRORLEVEL% NEQ 0 (
    echo Poedit not found — continuing without it.
) else (
    echo Poedit found.
)

REM ------------------------------------------------------------
REM  Download and install gettext tools (future-proof, never breaks)
REM ------------------------------------------------------------

echo Downloading native Windows gettext tools

REM Known working versions (fallback chain)
set GETTEXT_URL_1=https://github.com/mlocati/gettext-iconv-windows/releases/download/v0.22.5/gettext-iconv-windows-x86_64-0.22.5.zip
set GETTEXT_URL_2=https://github.com/mlocati/gettext-iconv-windows/releases/download/v0.22.4/gettext-iconv-windows-x86_64-0.22.4.zip
set GETTEXT_URL_3=https://github.com/mlocati/gettext-iconv-windows/releases/download/v0.22.3/gettext-iconv-windows-x86_64-0.22.3.zip

REM Try each URL until one succeeds
for %%U in ("%GETTEXT_URL_1%" "%GETTEXT_URL_2%" "%GETTEXT_URL_3%") do (
    echo Trying %%~U
    curl -L -f -o gettext.zip %%~U && goto :gettext_download_ok
)

echo ERROR: All gettext download attempts failed.
exit /b 1

:gettext_download_ok
REM Validate file size (must be > 1MB)
for %%A in (gettext.zip) do set GETTEXT_SIZE=%%~zA
echo Downloaded gettext.zip size: %GETTEXT_SIZE%

if "%GETTEXT_SIZE%"=="" (
    echo ERROR: Downloaded file is empty.
    exit /b 1
)

if %GETTEXT_SIZE% LSS 1000000 (
    echo ERROR: Downloaded file too small. GitHub returned HTML instead of ZIP.
    type gettext.zip
    exit /b 1
)

REM Extract using 7zip (PowerShell ZIP is unreliable)
7z x -y gettext.zip -ogettext-tools
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to extract gettext tools with 7zip.
    exit /b 1
)

REM Verify required binaries
if not exist "gettext-tools\bin\msgfmt.exe" (
    echo ERROR: msgfmt.exe missing after extraction.
    exit /b 1
)
if not exist "gettext-tools\bin\msgmerge.exe" (
    echo ERROR: msgmerge.exe missing after extraction.
    exit /b 1
)
if not exist "gettext-tools\bin\xgettext.exe" (
    echo ERROR: xgettext.exe missing after extraction.
    exit /b 1
)

REM Add gettext tools to PATH
set GETTEXT_BIN=%CD%\gettext-tools\bin
set PATH=%GETTEXT_BIN%;%PATH%

echo Gettext installed successfully.


REM ------------------------------------------------------------
REM  Configure CMake project
REM ------------------------------------------------------------
echo Configuring CMake project

if "%MSVC_VERSION%"=="2019" (
  cmake -T v141_xp -G "Visual Studio 16 2019" ^
    -DCMAKE_GENERATOR_PLATFORM=Win32 ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    -DGETTEXT_MSGFMT_EXECUTABLE=%GETTEXT_BIN%\msgfmt.exe ^
    -DGETTEXT_MSGMERGE_EXECUTABLE=%GETTEXT_BIN%\msgmerge.exe ^
    -DGETTEXT_XGETTEXT_EXECUTABLE=%GETTEXT_BIN%\xgettext.exe ^
    ..
) else (
  cmake -A Win32 -G "Visual Studio 17 2022" ^
    -DCMAKE_GENERATOR_PLATFORM=Win32 ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    -DGETTEXT_MSGFMT_EXECUTABLE=%GETTEXT_BIN%\msgfmt.exe ^
    -DGETTEXT_MSGMERGE_EXECUTABLE=%GETTEXT_BIN%\msgmerge.exe ^
    -DGETTEXT_XGETTEXT_EXECUTABLE=%GETTEXT_BIN%\xgettext.exe ^
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

setlocal enabledelayedexpansion

set "DLL_PATH="
set "DLL_CONFIG="

for %%C in (
    Release
    RelWithDebInfo
    Debug
    MinSizeRel
    x64\Release
    x64\RelWithDebInfo
    x64\Debug
    x64\MinSizeRel
	) do (
    if exist "%CD%\%%C\weather_routing_pi.dll" (
        set "DLL_PATH=%CD%\%%C\weather_routing_pi.dll"
        set "DLL_CONFIG=%%C"
    )
)

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