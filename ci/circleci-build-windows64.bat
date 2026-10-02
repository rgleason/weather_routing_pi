@echo off
setlocal EnableExtensions
choco install gettext --version 1.0.0.20260310 -y --no-progress
if errorlevel 1 exit /b %errorlevel%
call refreshenv
if errorlevel 1 exit /b %errorlevel%
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
git submodule update --init opencpn-libs
if errorlevel 1 exit /b %errorlevel%
python ci\circleci-build-windows64.py
exit /b %errorlevel%
