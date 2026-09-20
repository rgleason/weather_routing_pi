::
:: Build the msvc artifacts
::

@echo on
setlocal EnableExtensions

set "SCRIPTDIR=%~dp0"
set "GIT_HOME=C:\Program Files\Git"
if "%CONFIGURATION%" == "" set "CONFIGURATION=RelWithDebInfo"
set "WR_XWEATHER_IDENTITY=OFF"
if /I "%WEATHER_ROUTING_CI_XWEATHER%" == "true" set "WR_XWEATHER_IDENTITY=ON"
if /I "%CIRCLE_PROJECT_REPONAME%" == "xweather_routing_pi" set "WR_XWEATHER_IDENTITY=ON"

rem CMake 4's FindGettext module requires msgfmt and msgmerge at configure
rem time.  Install the same pinned package used by xGRIB's validated Windows
rem build before configuring the wxWidgets and Visual Studio environments.
choco install gettext --version 1.0.0.20260310 -y --no-progress
if errorlevel 1 (
  timeout /t 10 /nobreak
  choco install gettext --version 1.0.0.20260310 -y --no-progress
)
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

curl.exe --fail --location --retry 5 --retry-delay 5 --output opencpn.lib https://sourceforge.net/projects/opencpnplugins/files/opencpn.lib
if errorlevel 1 exit /b %errorlevel%
curl.exe --fail --location --retry 5 --retry-delay 5 --output OpenCPN_buildwin-4.99a.7z https://download.opencpn.org/s/oibxM3kzfzKcSc3/download/OpenCPN_buildwin-4.99a.7z
if errorlevel 1 exit /b %errorlevel%
7z x -y OpenCPN_buildwin-4.99a.7z -o..\buildwin
if errorlevel 1 exit /b %errorlevel%
curl.exe --fail --location --retry 5 --retry-delay 5 --output nsis-3.04-setup.exe https://download.opencpn.org/s/54HsBDLNzRZLL6i/download/nsis-3.04-setup.exe
if errorlevel 1 exit /b %errorlevel%
nsis-3.04-setup.exe /S
if errorlevel 1 exit /b %errorlevel%

echo Check gettext tools
msgfmt --version
if errorlevel 1 exit /b %errorlevel%
msgmerge --version
if errorlevel 1 exit /b %errorlevel%
echo Done check

echo Create build environment

if "%MSVC_VERSION%" == "2019" (
cmake -T v141_xp -G "Visual Studio 16 2019" ^
    -DCMAKE_GENERATOR_PLATFORM=Win32 ^
    -DCMAKE_BUILD_TYPE=%CONFIGURATION% ^
    -DWEATHER_ROUTING_STANDALONE_API=ON ^
    -DWEATHER_ROUTING_XWEATHER_IDENTITY=%WR_XWEATHER_IDENTITY% ^
    -DOCPN_BUILD_TEST=ON ^
    -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
    -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
    ..
) else (
cmake -A Win32 -G "Visual Studio 17 2022" ^
    -DCMAKE_GENERATOR_PLATFORM=Win32 ^
    -DCMAKE_BUILD_TYPE=%CONFIGURATION% ^
    -DWEATHER_ROUTING_STANDALONE_API=ON ^
    -DWEATHER_ROUTING_XWEATHER_IDENTITY=%WR_XWEATHER_IDENTITY% ^
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
set "PLUGIN_PACKAGE=weather_routing_pi"
set "OTHER_PACKAGE=xweather_routing_pi"
if /I "%WR_XWEATHER_IDENTITY%" == "ON" (
  set "PLUGIN_PACKAGE=xweather_routing_pi"
  set "OTHER_PACKAGE=weather_routing_pi"
)
if not exist stage\plugins\%PLUGIN_PACKAGE%.dll (
  echo Staged WeatherRouting DLL is missing
  exit /b 1
)

rem CMake's Visual Studio generator has historically exposed PACKAGE.vcxproj,
rem but this is not guaranteed by all CMake/CPack combinations.  Invoke CPack
rem directly so packaging does not depend on a generated convenience project.
cpack -G TGZ -C %CONFIGURATION% --config CPackConfig.cmake
if errorlevel 1 exit /b %errorlevel%

for /f %%C in ('dir /b /a:-d %PLUGIN_PACKAGE%-*.tar.gz 2^>nul ^| find /c /v ""') do set "ARCHIVE_COUNT=%%C"
if not "%ARCHIVE_COUNT%"=="1" (
  echo Expected exactly one WeatherRouting archive, found %ARCHIVE_COUNT%
  exit /b 1
)
for /f %%C in ('dir /b /a:-d %PLUGIN_PACKAGE%-*.xml 2^>nul ^| find /c /v ""') do set "METADATA_COUNT=%%C"
if not "%METADATA_COUNT%"=="1" (
  echo Expected exactly one WeatherRouting metadata file, found %METADATA_COUNT%
  exit /b 1
)
for %%F in (%PLUGIN_PACKAGE%-*.tar.gz) do tar -tzf "%%F" > package-contents.txt
if errorlevel 1 exit /b %errorlevel%
for %%A in (%PLUGIN_PACKAGE%-*.tar.gz) do for %%M in (%PLUGIN_PACKAGE%-*.xml) do python "%SCRIPTDIR%embed-package-metadata.py" "%%A" "%%M" "%%A"
if errorlevel 1 exit /b %errorlevel%
for %%F in (%PLUGIN_PACKAGE%-*.tar.gz) do tar -tzf "%%F" > package-contents.txt
findstr /x /c:"metadata.xml" package-contents.txt >nul
if errorlevel 1 (
  echo Package does not contain root-level metadata.xml
  exit /b 1
)
findstr /i /c:"plugins/%PLUGIN_PACKAGE%.dll" package-contents.txt >nul
if errorlevel 1 (
  echo Package does not contain the WeatherRouting DLL
  exit /b 1
)
findstr /i /c:"plugins/%OTHER_PACKAGE%.dll" package-contents.txt >nul
if not errorlevel 1 (
  echo Package contains the preview xWeatherRouting DLL identity
  exit /b 1
)

rem Use script-relative paths while SCRIPTDIR is still in scope.
python "%SCRIPTDIR%verify-shoreline-package.py" "%SCRIPTDIR%..\build"
if errorlevel 1 exit /b %errorlevel%

rem Retain the verified package explicitly.  CircleCI's Windows executor puts
rem find.exe ahead of Git's Unix find, so the shared Unix retention command
rem cannot safely discover Windows artifacts.
if exist ..\artifacts\windows-x86 rmdir /s /q ..\artifacts\windows-x86
mkdir ..\artifacts\windows-x86\package
if errorlevel 1 exit /b %errorlevel%
copy /y %PLUGIN_PACKAGE%-*.tar.gz ..\artifacts\windows-x86\package\
if errorlevel 1 exit /b %errorlevel%
copy /y %PLUGIN_PACKAGE%-*.xml ..\artifacts\windows-x86\package\
if errorlevel 1 exit /b %errorlevel%
mkdir ..\artifacts\windows-x86\tests
if errorlevel 1 exit /b %errorlevel%
copy /y test-results\ctest.xml ..\artifacts\windows-x86\tests\ctest.xml
if errorlevel 1 exit /b %errorlevel%
dir ..\artifacts\windows-x86\package

endlocal
