@echo on
setlocal enabledelayedexpansion

echo RUN THIS PLUGIN FROM INSIDE THE PLUGIN DIRECTORY
REM -----------------------------------------------------------
REM Windows x64-only build
REM wxWidgets root: C:\Users\fcgle\source\ocpn_wxWidgets
REM Creates tarball with metadata.xml injected (DOUBLE‑TAR REPACK)
REM Copies x64 DLL/PDB → opencpn-x64 runtime
REM -----------------------------------------------------------

REM -----------------------------------------------------------
REM Use Git for Windows tar, gzip, bash
REM -----------------------------------------------------------
set PATH=C:\Program Files\Git\usr\bin;%PATH%

REM -----------------------------------------------------------
REM wxWidgets root
REM -----------------------------------------------------------
set WX_ROOT=C:\Users\fcgle\source\ocpn_wxWidgets

REM -----------------------------------------------------------
REM Plugin paths
REM -----------------------------------------------------------
set PLUGIN_ROOT=%cd%
set PLUGIN_NAME=
for %%D in ("%PLUGIN_ROOT%") do set PLUGIN_NAME=%%~nxD

echo Plugin detected: %PLUGIN_NAME%
echo PLUGIN_ROOT: %PLUGIN_ROOT%

REM -----------------------------------------------------------
REM OpenCPN x64 destination
REM -----------------------------------------------------------
set OCPN_ROOT64=C:\Users\fcgle\source\opencpn-64
set OCPN_BUILD64=%OCPN_ROOT64%\build\RelWithDebInfo\plugins

echo Win64 destination: %OCPN_BUILD64%

REM ============================================================
REM                       X64 BUILD ONLY
REM ============================================================
echo Removing build directories
set BUILD=%PLUGIN_ROOT%\build
set BUILD64=%PLUGIN_ROOT%\build64
if exist "%BUILD64%" (
    echo Removing existing x64 build directory...
    rmdir /S /Q "%BUILD64%"
	rmdir /S /Q "%BUILD%"
)
if exist "%BUILD%" (
    echo Removing existing x64 build directory...
	rmdir /S /Q "%BUILD%"
)

mkdir "%BUILD64%"
cd "%BUILD64%"

REM ------------------------------------------------------------
REM Configure x64 build
REM ------------------------------------------------------------
echo Configuring x64 build...
cmake -T v143 -A x64 ^
  -DwxWidgets_ROOT_DIR=%WX_ROOT% ^
  -DwxWidgets_LIB_DIR=%WX_ROOT%\lib\vc_x64_dll ^
  -DwxWidgets_INCLUDE_DIRS=%WX_ROOT%\lib\vc_x64_dll\mswud ^
  -DOCPN_TARGET=MSVC ^
  -DWEATHER_ROUTING_STANDALONE_API=ON ^
  .. >>OUTPUT.TXT 2>&1

echo Building x64 plugin (RelWithDebInfo)...
cmake --build . --config RelWithDebInfo >>OUTPUT.TXT 2>&1

call :check "RelWithDebInfo\%PLUGIN_NAME%.dll"
call :check "RelWithDebInfo\%PLUGIN_NAME%.pdb"

REM ------------------------------------------------------------
REM Run CPack to generate x64 tarball + XML
REM ------------------------------------------------------------
echo Running CPack for x64 Target Package...
cmake --build . --config RelWithDebInfo --target package >>OUTPUT.TXT 2>&1

REM ------------------------------------------------------------
REM X64 Tarball + metadata.xml injection
REM ------------------------------------------------------------
echo Creating x64 tarball...

for %%f in (%PLUGIN_NAME%-*.xml) do set XML_FILE=%%f
for %%f in (%PLUGIN_NAME%-*.tar.gz) do set TARBALL=%%f

call :check "%XML_FILE%"
call :check "%TARBALL%"
echo Next inject metadata.xml into TARBALL: %TARBALL%

gzip -d -c "%TARBALL%" > inner.tar

rmdir /s /q repack 2>nul
mkdir repack
tar -xf inner.tar -C repack

copy /Y "%XML_FILE%" "repack\metadata.xml"

set PLUGIN_DIR=
for /d %%D in (repack\*) do (
    echo %%~nxD | findstr /i "%PLUGIN_NAME%" >nul
    if !errorlevel! == 0 set PLUGIN_DIR=%%~nxD
)

if "%PLUGIN_DIR%"=="" (
    echo ERROR: x64 plugin directory not found!
    exit /b 1
)

set INNER_NAME=%TARBALL:.gz=%

tar -cf "%INNER_NAME%" -C repack metadata.xml "%PLUGIN_DIR%"
gzip -c "%INNER_NAME%" > "%TARBALL%"

del "%INNER_NAME%"
rmdir /s /q repack
del inner.tar

echo X64 tarball repack complete.
call :check "%XML_FILE%"
call :check "%TARBALL%"

REM ------------------------------------------------------------
REM Copy x64 DLL + PDB to x64 OpenCPN runtime
REM ------------------------------------------------------------
echo Copying x64 DLL + PDB to OpenCPN-x64 runtime...



copy /Y ".\RelWithDebInfo\%PLUGIN_NAME%.dll" "%OCPN_BUILD64%\"
copy /Y ".\RelWithDebInfo\%PLUGIN_NAME%.pdb" "%OCPN_BUILD64%\"

call :check "%OCPN_BUILD64%\%PLUGIN_NAME%.dll"
call :check "%OCPN_BUILD64%\%PLUGIN_NAME%.pdb"

echo X64 plugin deployed.

cd "%PLUGIN_ROOT%"

echo.
echo X64 BUILD COMPLETE

endlocal
exit /b

REM --- file check helper ---
:check
if exist %1 (
    echo FILE EXISTS: %1
) else (
    echo FILE MISSING: %1
    exit /b 1
)
exit /b


