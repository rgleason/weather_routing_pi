#!/usr/bin/env bash
set -Eeuo pipefail

#
# Run the CMake-generated cloudsmith upload script.
# Honors BUILD_DIR when set; falls back to ./build/cloudsmith-upload.sh.
#

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

SCRIPT_PATH="${BUILD_DIR:-$REPO_ROOT/build}/cloudsmith-upload.sh"

if [ ! -f "$SCRIPT_PATH" ]; then
    echo "ERROR: Cloudsmith script not found at $SCRIPT_PATH"
    echo "BUILD_DIR=${BUILD_DIR:-unset}"
    exit 1
fi

echo "Using Cloudsmith script: $SCRIPT_PATH"
chmod +x "$SCRIPT_PATH"
exec bash "$SCRIPT_PATH"
