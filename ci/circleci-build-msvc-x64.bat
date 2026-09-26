::
:: Build the msvc artifacts (Windows x64)
::

@echo on
setlocal EnableExtensions

:: -------------------------------
:: Basic environment setup
:: -------------------------------
set "SCRIPTDIR=%~dp0"
set "GIT_HOME=C:\Program Files\Git"
if "%CONFIGURATION%" == "" set "CONFIGURATION=RelWithDebInfo"

:: Determine plugin identity (WeatherRouting vs xWeatherRouting)
set "WR_XWEATHER_IDENTITY=OFF"
if /I "%WEATHER_ROUTING_CI_XWEATHER%" == "true" set "WR_XWEATHER_IDENTITY=ON"
if /I "%CIRCLE_PROJECT_REPONAME%" == "xweather_routing_pi" set "WR_XWEATHER_IDENTITY=ON"

:: -------------------------------
:: Install gettext (required by CMake FindGettext)
:: -------------------------------
choco install gettext --version 1.0.0.20260310 -y --no-progress
if errorlevel 1 (
  timeout /t 10 /nobreak
  choco install gettext --version 1.0.0.20260310 -y --no-progress
)
if errorlevel 1 exit /b %errorlevel%
call refreshenv
where msgfmt
where msgmerge

:: -------------------------------
:: wxWidgets dependency setup
:: -------------------------------
set wx_vers="wx%WX_VER%"
PATH %SCRIPTDIR%.local\bin;%PATH%
call %SCRIPTDIR%..\msvc\win_deps.bat %wx_vers%
path %SCRIPTDIR%.local\bin;%PATH%;"C:\Program Files\CMake\bin"
call "%SCRIPTDIR%..\cache\wx-config.bat"
set PATH=%EXTRA_PATH%;%PATH%

echo USING wxWidgets_LIB_DIR: %wxWidgets_LIB_DIR%
echo USING wxWidgets_ROOT_DIR: %wxWidgets_ROOT_DIR%

:: -------------------------------
:: Switch to MSVC x64 toolchain
:: -------------------------------
nmake /?  >nul 2>&1
if errorlevel 1 (
  set "VS_HOME=C:\Program Files\Microsoft Visual Studio\2022"
  call "%VS_HOME%\Community\VC\Auxiliary\Build\vcvars64.bat"
)

:: Ensure wxWidgets DLLs are available for test execution
set "PATH=%wxWidgets_LIB_DIR%;%PATH%"

:: -------------------------------
:: Initialize OpenCPN submodules
:: -------------------------------
git submodule update --init opencpn-libs

:: -------------------------------
:: Prepare clean build directory
:: -------------------------------
if exist build (rmdir /s /q build)
mkdir build && cd build

:: -------------------------------
:: Download OpenCPN core libs + NSIS
:: -------------------------------
curl.exe --fail --location --retry 5 --retry-delay 5 --output opencpn.lib https://sourceforge.net/projects/opencpnplugins/files/opencpn.lib
curl.exe --fail --location --retry 5 --retry-delay 5 --output OpenCPN_buildwin-4.99a.7z https://download.opencpn.org/s/oibxM3kzfzKcSc3/download/OpenCPN_buildwin-4.99a.7z
7z x -y OpenCPN_buildwin-4.99a.7z -o..\buildwin
curl.exe --fail --location --retry 5 --retry-delay 5 --output nsis-3.04-setup.exe https://download.opencpn.org/s/54HsBDLNzRZLL6i/download/nsis-3.04-setup.exe
nsis-3.04-setup.exe /S

:: -------------------------------
:: Verify gettext tools
:: -------------------------------
msgfmt --version
msgmerge --version

:: -------------------------------
:: Configure CMake for x64 build
:: -------------------------------
if "%MSVC_VERSION%" == "2019" (
cmake -T v141_xp -G "Visual Studio 16 2019" ^
    -DCMAKE_GENERATOR_PLATFORM=x64 ^
    -DCMAKE_BUILD_TYPE=%CONFIGURATION% ^
    -DWEATHER_ROUTING_STANDALONE_API=ON ^
    -DWEATHER_ROUTING_XWEATHER_IDENTITY=%WR_XWEATHER_IDENTITY% ^
    -DOCPN_BUILD_TEST=ON ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    ..
) else (
cmake -A x64 -G "Visual Studio 17 2022" ^
    -DCMAKE_GENERATOR_PLATFORM=x64 ^
    -DCMAKE_BUILD_TYPE=%CONFIGURATION% ^
    -DWEATHER_ROUTING_STANDALONE_API=ON ^
    -DWEATHER_ROUTING_XWEATHER_IDENTITY=%WR_XWEATHER_IDENTITY% ^
    -DOCPN_BUILD_TEST=ON ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    ..
)

:: -------------------------------
:: Build plugin (x64)
:: -------------------------------
cmake --build . --config %CONFIGURATION% --parallel 2

:: -------------------------------
:: Run tests
:: -------------------------------
if not exist test-results mkdir test-results
ctest --test-dir . -C %CONFIGURATION% --output-on-failure --timeout 180 --output-junit test-results\ctest.xml

:: -------------------------------
:: Install plugin into staging directory
:: -------------------------------
if exist stage rmdir /s /q stage
cmake --install . --config %CONFIGURATION% --prefix stage

:: Determine plugin identity (WeatherRouting vs xWeatherRouting)
set "PLUGIN_PACKAGE=weather_routing_pi"
set "OTHER_PACKAGE=xweather_routing_pi"
if /I "%WR_XWEATHER_IDENTITY%" == "ON" (
  set "PLUGIN_PACKAGE=xweather_routing_pi"
  set "OTHER_PACKAGE=weather_routing_pi"
)

:: Verify staged DLL exists
if not exist stage\plugins\%PLUGIN_PACKAGE%.dll (
  echo Staged WeatherRouting DLL is missing
  exit /b 1
)

:: -------------------------------
:: Package plugin (tar.gz)
:: -------------------------------
cpack -G TGZ -C %CONFIGURATION% --config CPackConfig.cmake

:: Validate exactly one archive + metadata file
for /f %%C in ('dir /b /a:-d %PLUGIN_PACKAGE%-*.tar.gz 2^>nul ^| find /c /v ""') do set "ARCHIVE_COUNT=%%C"
for /f %%C in ('dir /b /a:-d %PLUGIN_PACKAGE%-*.xml 2^>nul ^| find /c /v ""') do set "METADATA_COUNT=%%C"

if not "%ARCHIVE_COUNT%"=="1" exit /b 1
if not "%METADATA_COUNT%"=="1" exit /b 1

:: -------------------------------
:: Embed metadata.xml into tarball
:: -------------------------------
for %%A in (%PLUGIN_PACKAGE%-*.tar.gz) do for %%M in (%PLUGIN_PACKAGE%-*.xml) do python "%SCRIPTDIR%embed-package-metadata.py" "%%A" "%%M" "%%A"

:: Verify metadata.xml and correct DLL inside tarball
for %%F in (%PLUGIN_PACKAGE%-*.tar.gz) do tar -tzf "%%F" > package-contents.txt
findstr /x /c:"metadata.xml" package-contents.txt
findstr /i /c:"plugins/%PLUGIN_PACKAGE%.dll" package-contents.txt

:: Ensure wrong identity DLL is NOT present
findstr /i /c:"plugins/%OTHER_PACKAGE%.dll" package-contents.txt
if not errorlevel 1 exit /b 1

:: -------------------------------
:: Shoreline validation (WeatherRouting-specific)
:: -------------------------------
python "%SCRIPTDIR%verify-shoreline-package.py" "%SCRIPTDIR%..\build"

:: -------------------------------
:: Save verified x64 artifacts
:: -------------------------------
if exist ..\artifacts\windows-x64 rmdir /s /q ..\artifacts\windows-x64
mkdir ..\artifacts\windows-x64\package
copy /y %PLUGIN_PACKAGE%-*.tar.gz ..\artifacts\windows-x64\package\
copy /y %PLUGIN_PACKAGE%-*.xml ..\artifacts\windows-x64\package\
mkdir ..\artifacts\windows-x64\tests
copy /y test-results\ctest.xml ..\artifacts\windows-x64\tests\ctest.xml

endlocal
