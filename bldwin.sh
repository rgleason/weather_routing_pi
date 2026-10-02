#!/usr/bin/env bash
set -euo pipefail

echo "RUN THIS PLUGIN FROM INSIDE THE PLUGIN DIRECTORY"
# 1. Uses RelWithDebInfo.
# 2.  wxWidgets setup: C:\Users\fcgle\source\ocpn_wxWidgets
# 3. Create a tarball with metadata.xml injected into the tarball. (similar to cloudsmith-upload.sh) useful for importing and testing.
# 4. Copy the dll + pdb into the Visual Studio build/RelWithDebInfo/plugins ready for use.

# ---------------------------------------
# Use Git for Windows tar, gzip, bash
# (MSYS2/Git-bash already provides these)
# ---------------------------------------

# ------------------------------------------------------------
# 0. Inject wxWidgets paths for PluginConfigure.cmake
# ------------------------------------------------------------
wxROOT="/c/Users/fcgle/source/ocpn_wxWidgets"
wxLIB="$wxROOT/lib/vc_dll"

export wxWidgets_ROOT_DIR="$wxROOT"
export wxWidgets_LIB_DIR="$wxLIB"
# ------------------------------------------------------------
# 1. Define paths
# ------------------------------------------------------------
PLUGIN_ROOT="$(pwd)"
BUILD_DIR="$PLUGIN_ROOT/build"
PLUGIN_BUILD="$BUILD_DIR/RelWithDebInfo"

OCPN_ROOT="/c/Users/fcgle/source/opencpn"
OCPN_BUILD="$OCPN_ROOT/build/RelWithDebInfo"
OCPN_SOLUTION="$OCPN_ROOT/build/OpenCPN.sln"

echo "PLUGIN_ROOT: $PLUGIN_ROOT"
echo "BUILD_DIR:   $BUILD_DIR"

# ------------------------------------------------------------
# 2. Detect plugin name from directory
# ------------------------------------------------------------
PLUGIN_NAME="$(basename "$PLUGIN_ROOT")"
echo "Plugin name detected: $PLUGIN_NAME"

# ------------------------------------------------------------
# 3. Remove build directory if it exists
# ------------------------------------------------------------
if [ -d "$BUILD_DIR" ]; then
    echo "Removing existing build directory..."
    rm -rf "$BUILD_DIR"
fi

echo "Creating fresh build directory..."
mkdir -p "$BUILD_DIR"

# ------------------------------------------------------------
# 4. Run CMake configure + build
# ------------------------------------------------------------
cd "$BUILD_DIR"

echo "Configuring plugin build..."
cmake -T v143 -A Win32 -DOCPN_TARGET=MSVC ..

echo "Building plugin (RelWithDebInfo)..."
cmake --build . --config RelWithDebInfo

# ------------------------------------------------------------
# 5. Generate Tarball and XML metadata
# ------------------------------------------------------------
echo "Running CPack to generate tarball and XML metadata..."
cmake --build . --config RelWithDebInfo --target package

# ------------------------------------------------------------
# 6. Insert metadata.xml into tarball (DOUBLE‑TAR REPACK)
# ------------------------------------------------------------

XML_FILE=$(ls "$PLUGIN_NAME"-*.xml)
TARBALL=$(ls "$PLUGIN_NAME"-*.tar.gz)

echo Inserting metadata.xml into tarball (DOUBLE TAR REPACK)
echo "Using XML: $XML_FILE"
echo "Using TARBALL: $TARBALL"

# Extract outer tar.gz → inner.tar
gzip -dc "$TARBALL" > inner.tar

# Extract inner.tar into repack/
rm -rf repack
mkdir repack
tar -xf inner.tar -C repack

# Copy metadata.xml INTO repack directory
cp "$XML_FILE" repack/metadata.xml

# Identify plugin directory inside tarball
PLUGIN_DIR=""
for d in repack/*; do
    base="$(basename "$d")"
    if echo "$base" | grep -qi "$PLUGIN_NAME"; then
        PLUGIN_DIR="$base"
    fi
done

if [ -z "$PLUGIN_DIR" ]; then
    echo "ERROR: Plugin directory not found!"
    exit 1
fi

echo "Plugin directory: $PLUGIN_DIR"

# Derive correct inner tar name from tarball
INNER_NAME="${TARBALL%.gz}"

echo "Correct inner tar name: $INNER_NAME"

# Rebuild inner tar with correct name
tar -cf "$INNER_NAME" -C repack metadata.xml "$PLUGIN_DIR"

# Safety check
SIZE=$(stat -c%s "$INNER_NAME")
echo "Inner tar size: $SIZE"

if [ "$SIZE" -eq 0 ]; then
    echo "ERROR: Inner tar is empty — aborting."
    exit 1
fi

# Recompress using correct name
gzip -c "$INNER_NAME" > "$TARBALL"

# Cleanup
rm "$INNER_NAME"
rm -rf repack
rm inner.tar

echo "SUCCESS: metadata.xml inserted into tarball."

# ------------------------------------------------------------
# 7. Copy plugin DLL + PDB into OpenCPN plugin folder
# ------------------------------------------------------------
echo "Copying plugin DLL + PDB into Visual Studio OpenCPN plugin directory..."

cp "$PLUGIN_BUILD/$PLUGIN_NAME.dll" "$OCPN_BUILD/plugins/"
cp "$PLUGIN_BUILD/$PLUGIN_NAME.pdb" "$OCPN_BUILD/plugins/"

echo "Plugin deployed successfully to Visual Studio."

# ------------------------------------------------------------
# 6. Launch Visual Studio (optional)
# ------------------------------------------------------------
# echo "Launching Visual Studio for debugging..."
# Uncomment if desired:
# cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe\" \"$OCPN_SOLUTION\""
# echo "Visual Studio is now open."
# echo "Select RelWithDebInfo and press F5 to debug OpenCPN with your plugin."

