::
:: Build the MSVC artifacts
::

@echo off
setlocal

set "SCRIPTDIR=%~dp0"
set "GIT_HOME=C:\Program Files\Git"
if "%CONFIGURATION%"=="" set "CONFIGURATION=RelWithDebInfo"

set "wx_vers=wx%WX_VER%"
echo Building %wx_vers%
echo Building with MSVC %MSVC_VERSION%

rem Base PATH setup
set "PATH=%SCRIPTDIR%.local\bin;%PATH%"
echo SCRIPTDIR: %SCRIPTDIR%
dir "%SCRIPTDIR%"
dir "%SCRIPTDIR%.."
dir "%SCRIPTDIR%..\msvc"

rem wx + deps
call "%SCRIPTDIR%..\msvc\win_deps.bat" %wx_vers%
set "PATH=%SCRIPTDIR%.local\bin;%PATH%;C:\Program Files\CMake\bin"
call "%SCRIPTDIR%..\cache\wx-config.bat"

set "PATH=%EXTRA_PATH%;%PATH%"
echo EXTRA_PATH: %EXTRA_PATH%
echo USING wxWidgets_LIB_DIR: %wxWidgets_LIB_DIR%
echo USING wxWidgets_ROOT_DIR: %wxWidgets_ROOT_DIR%
echo USING OCPN_TARGET_TUPLE: %TARGET_TUPLE%

rem Ensure MSVC environment
nmake /? >nul 2>&1
if errorlevel 1 (
  set "VS_HOME=C:\Program Files\Microsoft Visual Studio\2022"
  call "%VS_HOME%\Community\VC\Auxiliary\Build\vcvars32.bat"
)

rem Submodules
git submodule update --init opencpn-libs

dir

rem Fresh build dir
if exist build (rmdir /s /q build)
mkdir build
cd build
dir

rem OpenCPN + NSIS toolchain
wget https://sourceforge.net/projects/opencpnplugins/files/opencpn.lib
wget https://download.opencpn.org/s/oibxM3kzfzKcSc3/download/OpenCPN_buildwin-4.99a.7z
7z x -y OpenCPN_buildwin-4.99a.7z -o..\buildwin
wget https://download.opencpn.org/s/54HsBDLNzRZLL6i/download/nsis-3.04-setup.exe
nsis-3.04-setup.exe /S

echo Check if poedit has been installed
poedit -version
echo Done check

rem vcpkg + gettext
echo Install vcpkg
set "VCPKG_ROOT=%CD%\vcpkg"
git clone https://github.com/microsoft/vcpkg "%VCPKG_ROOT%"
call "%VCPKG_ROOT%\bootstrap-vcpkg.bat"

echo Install gettext
"%VCPKG_ROOT%\vcpkg" install gettext:x86-windows
echo Gettext installed.

echo Create build environment

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

echo Build for Windows

rem Build everything
cmake --build . --config %CONFIGURATION%

rem Install into staging directory for CPack
cmake --build . --target INSTALL --config %CONFIGURATION%

rem Run CPack to generate the plugin package
cpack -G TGZ

endlocal
