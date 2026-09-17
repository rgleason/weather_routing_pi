#!/usr/bin/env bash

#
# Run generated Cloudsmith upload script
#

set -eu

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"

# Resolve BUILD_DIR to an absolute path before changing directories.
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$REPO_ROOT/$BUILD_DIR"
fi
if [ -d "$BUILD_DIR" ]; then
    BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
fi

cd "$REPO_ROOT"

SCRIPT_PATH="${BUILD_DIR}/cloudsmith-upload.sh"
if [ ! -f "$SCRIPT_PATH" ]; then
    FALLBACK_SCRIPT="$REPO_ROOT/build/cloudsmith-upload.sh"
    if [ -f "$FALLBACK_SCRIPT" ]; then
        SCRIPT_PATH="$FALLBACK_SCRIPT"
    else
        echo "ERROR: Cloudsmith upload script not found in BUILD_DIR ($BUILD_DIR) or repo build dir ($REPO_ROOT/build)." >&2
        exit 1
    fi
fi

echo "Using Cloudsmith upload script: $SCRIPT_PATH"
exec bash "$SCRIPT_PATH"