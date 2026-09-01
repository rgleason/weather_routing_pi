::
:: Build the msvc artifacts
::

@echo on
setlocal EnableExtensions

set "SCRIPTDIR=%~dp0"
set "GIT_HOME=C:\Program Files\Git"
if "%CONFIGURATION%" == "" set "CONFIGURATION=RelWithDebInfo"

rem CMake 4's FindGettext module requires msgfmt and msgmerge at configure
rem time.  Install the same pinned package used by xGRIB's validated Windows
rem build before configuring the wxWidgets and Visual Studio environments.
choco install gettext --version 1.0.0.20260310 -y --no-progress
if errorlevel 1 exit /b %errorlevel%
call refreshenv
if errorlevel 1 exit /b %errorlevel%
where msgfmt
if errorlevel 1 exit /b 1
where msgmerge
if errorlevel 1 exit /b 1

set wx_vers="wx%WX_VER%"
echo Building %wx_vers%
echo Building with %MSVC_VERSION%

PATH %SCRIPTDIR%.local\bin;%PATH%
echo SCRIPTDIR: %SCRIPTDIR%
dir %SCRIPTDIR%
dir %SCRIPTDIR%..
dir %SCRIPTDIR%..\msvc
call %SCRIPTDIR%..\msvc\win_deps.bat %wx_vers%
if errorlevel 1 exit /b %errorlevel%
path %SCRIPTDIR%.local\bin;%PATH%;"C:\Program Files\CMake\bin"
call "%SCRIPTDIR%..\cache\wx-config.bat"
if errorlevel 1 exit /b %errorlevel%
set PATH=%EXTRA_PATH%;%PATH%
echo EXTRA_PATH: %EXTRA_PATH%
echo USING wxWidgets_LIB_DIR: %wxWidgets_LIB_DIR%
echo USING wxWidgets_ROOT_DIR: %wxWidgets_ROOT_DIR%
echo USING OCPN_TARGET_TUPLE: %TARGET_TUPLE%

nmake /?  >nul 2>&1
if errorlevel 1 (
  set "VS_HOME=C:\Program Files\Microsoft Visual Studio\2022"
  call "%VS_HOME%\Community\VC\Auxiliary\Build\vcvars32.bat"
)

rem The test executable links the same wxWidgets DLLs supplied by an OpenCPN
rem host. Make that recorded runtime directory available before GoogleTest
rem discovery; EXTRA_PATH contains gettext tools, not wxWidgets.
set "PATH=%wxWidgets_LIB_DIR%;%PATH%"

git submodule update --init opencpn-libs
if errorlevel 1 exit /b %errorlevel%

dir

if exist build (rmdir /s /q build)
mkdir build && cd build
dir

wget https://sourceforge.net/projects/opencpnplugins/files/opencpn.lib
if errorlevel 1 exit /b %errorlevel%
wget https://download.opencpn.org/s/oibxM3kzfzKcSc3/download/OpenCPN_buildwin-4.99a.7z
if errorlevel 1 exit /b %errorlevel%
7z x -y OpenCPN_buildwin-4.99a.7z -o..\buildwin
if errorlevel 1 exit /b %errorlevel%
wget https://download.opencpn.org/s/54HsBDLNzRZLL6i/download/nsis-3.04-setup.exe
if errorlevel 1 exit /b %errorlevel%
nsis-3.04-setup.exe /S
if errorlevel 1 exit /b %errorlevel%

echo Check if poedit has been installed
poedit -version
echo Done check

echo Create build environment

if "%MSVC_VERSION%" == "2019" (
cmake -T v141_xp -G "Visual Studio 16 2019" ^
    -DCMAKE_GENERATOR_PLATFORM=Win32 ^
    -DCMAKE_BUILD_TYPE=%CONFIGURATION% ^
    -DWEATHER_ROUTING_STANDALONE_API=ON ^
    -DOCPN_BUILD_TEST=ON ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    ..
) else (
cmake -A Win32 -G "Visual Studio 17 2022" ^
    -DCMAKE_GENERATOR_PLATFORM=Win32 ^
    -DCMAKE_BUILD_TYPE=%CONFIGURATION% ^
    -DWEATHER_ROUTING_STANDALONE_API=ON ^
    -DOCPN_BUILD_TEST=ON ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    ..
)
if errorlevel 1 exit /b %errorlevel%


cd
dir

echo Build for windows

cmake --build . --config %CONFIGURATION% --parallel 2
if errorlevel 1 exit /b %errorlevel%

if not exist test-results mkdir test-results
ctest --test-dir . -C %CONFIGURATION% --output-on-failure --timeout 180 --output-junit test-results\ctest.xml
if errorlevel 1 exit /b %errorlevel%

if exist stage rmdir /s /q stage
cmake --install . --config %CONFIGURATION% --prefix stage
if errorlevel 1 exit /b %errorlevel%
if not exist stage\plugins\weather_routing_pi.dll (
  echo Staged WeatherRouting DLL is missing
  exit /b 1
)

rem CMake's Visual Studio generator has historically exposed PACKAGE.vcxproj,
rem but this is not guaranteed by all CMake/CPack combinations.  Invoke CPack
rem directly so packaging does not depend on a generated convenience project.
cpack -G TGZ -C %CONFIGURATION% --config CPackConfig.cmake
if errorlevel 1 exit /b %errorlevel%

for /f %%C in ('dir /b /a:-d weather_routing_pi-*.tar.gz 2^>nul ^| find /c /v ""') do set "ARCHIVE_COUNT=%%C"
if not "%ARCHIVE_COUNT%"=="1" (
  echo Expected exactly one WeatherRouting archive, found %ARCHIVE_COUNT%
  exit /b 1
)
for /f %%C in ('dir /b /a:-d weather_routing_pi-*.xml 2^>nul ^| find /c /v ""') do set "METADATA_COUNT=%%C"
if not "%METADATA_COUNT%"=="1" (
  echo Expected exactly one WeatherRouting metadata file, found %METADATA_COUNT%
  exit /b 1
)
for %%F in (weather_routing_pi-*.tar.gz) do tar -tzf "%%F" > package-contents.txt
if errorlevel 1 exit /b %errorlevel%
findstr /i /c:"plugins/weather_routing_pi.dll" package-contents.txt >nul
if errorlevel 1 (
  echo Package does not contain the WeatherRouting DLL
  exit /b 1
)
findstr /i /c:"plugins/xweather_routing_pi.dll" package-contents.txt >nul
if not errorlevel 1 (
  echo Package contains the preview xWeatherRouting DLL identity
  exit /b 1
)

endlocal
