REM!/bin/bash   NOW a Batch file

REM FE2 Testplugin
REM REKWITHDEBINFO VERSION
REM Use "./build-win.sh" to run cmake.
REM Adjust this command for your setup and Plugin.
REM Requires wxWidgets setup
REM - /home/fcgle/source/ocpn-wxWidgets
REM - /home/fcgle/source/ where all the plugins and OpenCPN repos are kept.
REM --------------------------------------
REM For Opencpn using MS Visual Studio 2022
REM --------------------------------------
REM Used for local builds and testing.
REM Create an empty "[plugin]/build" directory
REM Use Bash Prompt from the [plugin] root directory: "bash ./bldwin-rdeb.sh"
REM Find any errors in the build/output.txt file
REM Then use bash prompt to run cloudsmith-upload.sh command: "bash ./bldwin-rdeb.sh"
REM Which adds the metadata file to the tarball gz file.
REM Set local environment to find and use wxWidgets

REM Enable command tracing

set -x 

REM Confirm build exists and empty it and if no build directory create it.

REM if [ -d "build" ]; then
REM    echo "The 'build' direcREMtory exists, remove all build dir files."
REM    rm -rf build/*
	
REM else
REM    echo "The 'build' directory does not exist. Create the build directory"
REM	mkdir build
REM   fi

REM wxWidgets settings 
set "wxDIR=C:\Users\fcgle\source\opencpn\..\ocpn_wxWidgets" 
set "wxWIN=C:\Users\fcgle\source\opencpn\..\ocpn_wxWidgets" 
set "wxWidgets_ROOT_DIR=C:\Users\fcgle\source\opencpn\..\ocpn_wxWidgets" 
set "wxWidgets_LIB_DIR=C:\Users\fcgle\source\opencpn\..\ocpn_wxWidgets\lib\vc_dll" 
set "VCver=17" 
set "VCstr=Visual Studio 17" 

REM wxDIR=$WXWIN
REM wxWidgets_ROOT_DIR=$WXWIN
REM wxWidgets_LIB_DIR="$WXWIN/lib/vc14x_dll"
REM WXWIN="/home/fcgle/source/wxWidgets-3.2.2"

REM build the plugin with cmake

cd build
cmake -T v143 -A Win32 -DOCPN_TARGET=MSVC ..
cmake --build . --target package --config relwithdebinfo > output.txt
	
REM Bash script completes tarball prep adding metadata into it.

bash ./cloudsmith-upload.sh

REM Find ${bold}"build/output.txt"${normal} file if the build is not successful.
REM Other examples below.
