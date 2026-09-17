#!/usr/bin/env bash
#
# Locate and run the generated cloudsmith-upload.sh script.
# The generated script comes from cmake/in-files/cloudsmith-upload.sh.in via configure_file().
#

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

# Artifact/package directory (where xml/tar.gz/pkg_version.sh usually live for CI upload step)
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
if [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="$REPO_ROOT/$BUILD_DIR"
fi
BUILD_DIR="$(mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR" && pwd)"

# Candidate locations for the generated script (CMake binary dirs + common build dirs).
declare -a CANDIDATES=(
  "$BUILD_DIR/cloudsmith-upload.sh"
  "$REPO_ROOT/build/cloudsmith-upload.sh"
  "$REPO_ROOT/cmake-build/cloudsmith-upload.sh"
  "$REPO_ROOT/cmake_build/cloudsmith-upload.sh"
  "$REPO_ROOT/_build/cloudsmith-upload.sh"
)

SCRIPT_PATH=""
for p in "${CANDIDATES[@]}"; do
  if [[ -f "$p" ]]; then
    SCRIPT_PATH="$p"
    break
  fi
done

# If not found, search shallowly for generated script under likely roots.
if [[ -z "$SCRIPT_PATH" ]]; then
  while IFS= read -r f; do
    case "$f" in
      */artifacts/*) continue ;;  # avoid staged artifact trees unless explicitly copied there
    esac
    SCRIPT_PATH="$f"
    break
  done < <(
    find "$REPO_ROOT" -maxdepth 4 -type f -name cloudsmith-upload.sh \
      ! -path "*/ci/*" \
      ! -path "*/cmake/in-files/*" \
      2>/dev/null
  )
fi

if [[ -z "$SCRIPT_PATH" ]]; then
  echo "ERROR: generated cloudsmith-upload.sh not found." >&2
  echo "Checked BUILD_DIR=$BUILD_DIR and common build locations under $REPO_ROOT." >&2
  echo "Hint: ensure CMake configure step ran and produced cloudsmith-upload.sh in a build directory." >&2
  exit 1
fi

echo "Using generated Cloudsmith script: $SCRIPT_PATH"
echo "Using BUILD_DIR for artifacts: $BUILD_DIR"

# Ensure generated script reads/writes the intended package directory.
export BUILD_DIR

exec bash "$SCRIPT_PATH"