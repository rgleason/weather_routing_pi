@echo on
setlocal enabledelayedexpansion

echo RUN THIS PLUGIN FROM INSIDE THE PLUGIN DIRECTORY
REM -----------------------------------------------------------
REM 1. Build both Win32 and x64 using separate build directories.
REM 2. wxWidgets root: C:\Users\fcgle\source\ocpn_wxWidgets
REM 3. Create tarballs with metadata.xml injected (DOUBLE‑TAR REPACK).
REM 4. Copy Win32 DLL/PDB → opencpn\build\RelWithDebInfo\plugins
REM 5. Copy x64  DLL/PDB → opencpn-x64 (adjust later)
REM -----------------------------------------------------------

REM -----------------------------------------------------------
REM Use Git for Windows tar, gzip, bash - lives in usr/bin
REM -----------------------------------------------------------
set PATH=C:\Program Files\Git\usr\bin;%PATH%

REM ------------------------------------------------------------
REM 0. wxWidgets root
REM ------------------------------------------------------------
set WX_ROOT=C:\Users\fcgle\source\ocpn_wxWidgets

REM ------------------------------------------------------------
REM 1. Define plugin paths
REM ------------------------------------------------------------
set PLUGIN_ROOT=%cd%
set PLUGIN_NAME=
for %%D in ("%PLUGIN_ROOT%") do set PLUGIN_NAME=%%~nxD

echo Plugin detected: %PLUGIN_NAME%
echo PLUGIN_ROOT: %PLUGIN_ROOT%

REM ------------------------------------------------------------
REM 2. Define OpenCPN destinations
REM ------------------------------------------------------------
set OCPN_ROOT=C:\Users\fcgle\source\opencpn
set OCPN_BUILD=%OCPN_ROOT%\build\RelWithDebInfo\plugins

set OCPN_ROOT64=C:\Users\fcgle\source\opencpn-64
set OCPN_BUILD64=%OCPN_ROOT64%\build\RelWithDebInfo\plugins

echo Win32 destination: %OCPN_BUILD%
echo Win64 destination: %OCPN_BUILD64%

REM ============================================================
REM                     WIN32 BUILD 
REM ============================================================

set BUILD=%PLUGIN_ROOT%\build
if exist "%BUILD%" (
    echo Removing existing Win32 build directory...
    rmdir /S /Q "%BUILD%"
)
echo Creating fresh build directory...
mkdir "%BUILD%"
cd "%BUILD%"

REM ------------------------------------------------------------
REM Configure Win32 build
REM ------------------------------------------------------------
echo Configuring Win32 build...
cmake -T v143 -A Win32 ^
  -DwxWidgets_ROOT_DIR=%WX_ROOT% ^
  -DwxWidgets_LIB_DIR=%WX_ROOT%\lib\vc_dll ^
  -DwxWidgets_INCLUDE_DIRS=%WX_ROOT%\lib\vc_dll\mswud ^
  -DOCPN_TARGET=MSVC ^
  ..

echo Building Win32 plugin (RelWithDebInfo)...
cmake --build . --config RelWithDebInfo

REM ------------------------------------------------------------
REM Run CPack to generate Win32 tarball + XML
REM ------------------------------------------------------------
echo Running CPack for Win32  Target package ...
cmake --build . --config RelWithDebInfo --target package

REM ------------------------------------------------------------
REM Win32 Tarball + metadata.xml injection
REM ------------------------------------------------------------
echo Creating Win32 tarball...

for %%f in (%PLUGIN_NAME%-*.xml) do set XML_FILE=%%f
for %%f in (%PLUGIN_NAME%-*.tar.gz) do set TARBALL=%%f

echo Using XML: %XML_FILE%
echo Using TARBALL: %TARBALL%

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
    echo ERROR: Win32 plugin directory not found!
    exit /b 1
)

set INNER_NAME=%TARBALL:.gz=%

tar -cf "%INNER_NAME%" -C repack metadata.xml "%PLUGIN_DIR%"
gzip -c "%INNER_NAME%" > "%TARBALL%"

del "%INNER_NAME%"
rmdir /s /q repack
del inner.tar

echo Win32 tarball repack complete.

REM ------------------------------------------------------------
REM Copy Win32 DLL + PDB to OpenCPN runtime
REM ------------------------------------------------------------
echo Copying Win32 DLL + PDB to OpenCPN runtime...

copy /Y ".\RelWithDebInfo\%PLUGIN_NAME%.dll" "%OCPN_BUILD%\"
copy /Y ".\RelWithDebInfo\%PLUGIN_NAME%.pdb" "%OCPN_BUILD%\"

echo Win32 plugin deployed.

cd "%PLUGIN_ROOT%"

REM ============================================================
REM                       X64 BUILD
REM ============================================================

set BUILD64=%PLUGIN_ROOT%\build64
if exist "%BUILD64%" (
    echo Removing existing x64 build directory...
    rmdir /S /Q "%BUILD64%"
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
  ..

echo Building x64 plugin (RelWithDebInfo)...
cmake --build . --config RelWithDebInfo

REM ------------------------------------------------------------
REM Run CPack to generate X64 tarball + XML
REM ------------------------------------------------------------

echo Running CPack for x64 for Target Package...
cmake --build . --config RelWithDebInfo --target package


REM ------------------------------------------------------------
REM X64 Tarball + metadata.xml injection
REM ------------------------------------------------------------
echo Creating x64 tarball...

for %%f in (%PLUGIN_NAME%-*.xml) do set XML_FILE=%%f
for %%f in (%PLUGIN_NAME%-*.tar.gz) do set TARBALL=%%f

echo Using XML: %XML_FILE%
echo Using TARBALL: %TARBALL%

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

REM ------------------------------------------------------------
REM Copy x64 DLL + PDB to x64 OpenCPN runtime
REM ------------------------------------------------------------
echo Copying x64 DLL + PDB to OpenCPN-x64 runtime...

copy /Y ".\RelWithDebInfo\%PLUGIN_NAME%.dll" "%OCPN_BUILD64%\"
copy /Y ".\RelWithDebInfo\%PLUGIN_NAME%.pdb" "%OCPN_BUILD64%\"

echo X64 plugin deployed.


cd "%PLUGIN_ROOT%"

echo.
echo ============================================================
echo DUAL-ARCH BUILD COMPLETE
echo ============================================================

endlocal
