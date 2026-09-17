#!/usr/bin/env bash

#
# Upload the .tar.gz and .xml artifacts to Cloudsmith
#
# Repository selection:
#   ALPHA: Non-master branch, no tag
#   BETA:  Non-master branch with tag OR master branch without tag
#   PROD:  Master branch with tag
#
# Local builds generate artifacts but do not upload.
#

set -xe

PROD_REPO=${CLOUDSMITH_PROD_REPO:-'opencpn/weather-routing-prod'}
BETA_REPO=${CLOUDSMITH_BETA_REPO:-'opencpn/weather-routing-beta'}
ALPHA_REPO=${CLOUDSMITH_ALPHA_REPO:-'opencpn/weather-routing-alpha'}

LOCAL_BUILD=false

# Detect top-level repo
REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

# -----------------------------
# CI environment detection
# -----------------------------
if [ "$CIRCLECI" ]; then
    BUILD_ID=${CIRCLE_BUILD_NUM:-1}
    BUILD_DIR=${BUILD_DIR:-"$REPO_ROOT/build"}
    BUILD_BRANCH=$CIRCLE_BRANCH
    BUILD_TAG=$CIRCLE_TAG
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'deb'}

elif [ "$TRAVIS" ]; then
    BUILD_ID=${TRAVIS_BUILD_NUM:-1}
    BUILD_DIR=$TRAVIS_BUILD_DIR/build
    BUILD_BRANCH=$TRAVIS_BRANCH
    BUILD_TAG=$TRAVIS_TAG
    [ "$BUILD_BRANCH" = "$BUILD_TAG" ] && BUILD_BRANCH=""
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'deb'}

elif [ "$APPVEYOR" ]; then
    BUILD_ID=${APPVEYOR_BUILD_NUMBER:-1}
    BUILD_DIR=$(pwd)/build
    BUILD_BRANCH=$APPVEYOR_REPO_BRANCH
    BUILD_TAG=$APPVEYOR_REPO_TAG_NAME
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'exe'}

else
    BUILD_ID=${CIRCLE_BUILD_NUM:-1}
    BUILD_DIR=${BUILD_DIR:-"$REPO_ROOT/build"}
    BUILD_BRANCH=$CIRCLE_BRANCH
    BUILD_TAG=$CIRCLE_TAG
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'deb'}
    LOCAL_BUILD=true
fi

# Normalize Windows paths to POSIX (Git Bash / MSYS)
if [ "$OS" = "Windows_NT" ] || uname | grep -qi "mingw"; then
    if command -v cygpath >/dev/null 2>&1; then
        BUILD_DIR="$(cygpath -u "$BUILD_DIR")"
    fi
fi

# -----------------------------
# API key check
# -----------------------------
set +x
if [ -z "$CLOUDSMITH_API_KEY" ] && [ "$LOCAL_BUILD" = "false" ]; then
    echo 'Cannot deploy to Cloudsmith: missing $CLOUDSMITH_API_KEY'
    exit 0
fi
set -x

# -----------------------------
# Install cloudsmith-cli (PEP-668 compliant)
# -----------------------------
install_cloudsmith_cli() {
    if command -v pipx >/dev/null 2>&1; then
        pipx install cloudsmith-cli || pipx upgrade cloudsmith-cli || true
        pipx ensurepath || true
        return
    fi

    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update -q
        sudo apt-get install -y pipx python3-venv
        pipx ensurepath
        pipx install cloudsmith-cli
        return
    fi

    if command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y pipx python3-virtualenv
        pipx ensurepath
        pipx install cloudsmith-cli
        return
    fi

    if command -v brew >/dev/null 2>&1; then
        brew install pipx
        pipx ensurepath
        pipx install cloudsmith-cli
        return
    fi

    echo "ERROR: pipx not available and no supported package manager found."
    exit 1
}

if { [ "$CIRCLECI" ] || [ "$TRAVIS" ]; } && [ "$DEPLOY_USE_ORB" != "true" ]; then
    install_cloudsmith_cli
fi

# Ensure cloudsmith is on PATH after pipx install
export PATH="$HOME/.local/bin:$PATH"

# -----------------------------
# Build metadata
# -----------------------------
commit=$(git rev-parse --short=7 HEAD || echo "unknown")
tag=$(git tag --contains HEAD || true)

ls -la "$BUILD_DIR"

# Robust file discovery
xml=$(find "$BUILD_DIR" -maxdepth 1 -type f -name "*.xml" | head -n 1)
tarball=$(find "$BUILD_DIR" -maxdepth 1 -type f -name "*.tar.gz" | head -n 1)

if [ -z "$xml" ] || [ -z "$tarball" ]; then
    echo "ERROR: Could not find XML or tarball in $BUILD_DIR"
    echo "xml='$xml'"
    echo "tarball='$tarball'"
    exit 1
fi

tarball_basename=$(basename "$tarball")
# Derive the Cloudsmith artifact name from the tarball filename (strip .tar.gz)
tarball_name="${tarball_basename%.tar.gz}"

if [ -f "$BUILD_DIR/pkg_version.sh" ]; then
    # shellcheck disable=SC1090
    source "$BUILD_DIR/pkg_version.sh"
fi

if ls "$BUILD_DIR"/*."${PKG_EXT}" >/dev/null 2>&1; then
    pkg=$(ls "$BUILD_DIR"/*."${PKG_EXT}" | head -n 1)
else
    pkg=""
fi

# -----------------------------
# Branch/tag → repo selection
# -----------------------------
echo "$BUILD_BRANCH"
echo "$BUILD_TAG"

if [ -z "$BUILD_TAG" ] && [ -n "$tag" ]; then
    BUILD_TAG="$tag"
    echo "Build tag: $BUILD_TAG"
fi

if [ -z "$BUILD_BRANCH" ]; then
    build_commit=$(git show -s --format=%d "$BUILD_TAG" || echo "")
    is_master=$(echo "$build_commit" | awk '/\/master/ {print}')
    if [ -n "$is_master" ] || [ "$TRAVIS" ]; then
        BUILD_BRANCH="master"
    fi
fi

# Use VERSION from pkg_version.sh if available, else project version
PKG_VERSION="${VERSION:-1.17.11}"

BUILD_BRANCH_LOWER=$(echo "$BUILD_BRANCH" | tr '[:upper:]' '[:lower:]')
if [ "$BUILD_BRANCH_LOWER" = "master" ]; then
    echo "In master branch"
    if [ -n "$BUILD_TAG" ]; then
        UPLOAD_VERSION="$BUILD_TAG"
        REPO="$PROD_REPO"
    else
        UPLOAD_VERSION="${PKG_VERSION}+${BUILD_ID}.${commit}"
        REPO="$BETA_REPO"
    fi
else
    echo "In non-master branch $BUILD_BRANCH"
    if [ -n "$BUILD_TAG" ]; then
        UPLOAD_VERSION="$BUILD_TAG"
        REPO="$BETA_REPO"
    else
        UPLOAD_VERSION="${PKG_VERSION}+${BUILD_ID}.${commit}"
        REPO="$ALPHA_REPO"
    fi
fi
echo "$UPLOAD_VERSION"
echo "$REPO"

# -----------------------------
# Substitute metadata in XML
# -----------------------------
if [ "$APPVEYOR" ] || [ "$OS" = "Windows_NT" ]; then
    tmp_xml="$BUILD_DIR/xml.tmp"
    : >"$tmp_xml"
    while IFS= read -r line; do
        line=${line//--pkg_repo--/$REPO}
        line=${line//--name--/$tarball_name}
        line=${line//--version--/$UPLOAD_VERSION}
        line=${line//--filename--/$tarball_basename}
        printf '%s\n' "$line" >>"$tmp_xml"
    done < "$xml"
    cp "$tmp_xml" "$xml"
    rm -f "$tmp_xml"
else
    sed -i -e "s|--pkg_repo--|$REPO|"  "$xml"
    sed -i -e "s|--name--|$tarball_name|" "$xml"
    sed -i -e "s|--version--|$UPLOAD_VERSION|" "$xml"
    sed -i -e "s|--filename--|$tarball_basename|" "$xml"
fi

cat "$xml"
ls -l "$BUILD_DIR"

cur_dir=$(pwd)

# -----------------------------
# Repack tarball with metadata.xml inside
# -----------------------------
gunzip -f "$tarball"
tarball_tar="${tarball%.gz}"

cp -f "$xml" "$BUILD_DIR/metadata.xml"

cd "$BUILD_DIR"
if [ "$TRAVIS" ] || [ "$CIRCLECI" ] || [ -n "${LOCAL_DEPLOY+x}" ]; then
    mkdir -p build_tar
    cp "$tarball_tar" build_tar/
    cd build_tar
    tar -xf "$(basename "$tarball_tar")"
    rm -f ./*.tar
    rm -rf root
    cp ../metadata.xml .
    tar -cf build_tarfile.tar *
    tar -tf build_tarfile.tar
    mv build_tarfile.tar "$tarball_tar"
    cd ..
    rm -rf build_tar
else
    tar -rf "$tarball_tar" metadata.xml
fi
tar -tf "$tarball_tar"
gzip -f "$tarball_tar"
cd "$cur_dir"

# -----------------------------
# Upload to Cloudsmith
# -----------------------------
if [ "$LOCAL_BUILD" = "false" ]; then
    cloudsmith push raw --republish --no-wait-for-sync \
        --name "${tarball_name}-metadata" \
        --version "$UPLOAD_VERSION" \
        --summary "weather_routing_pi opencpn plugin metadata for automatic installation" \
        "$REPO" "$xml"

    cloudsmith push raw --republish --no-wait-for-sync \
        --name "$tarball_name" \
        --version "$UPLOAD_VERSION" \
        --summary "weather_routing_pi opencpn plugin tarball for automatic installation" \
        "$REPO" "$tarball"

    if [ "${PKG_EXT}" != "gz" ] && [ -n "$pkg" ]; then
        cloudsmith push raw --republish --no-wait-for-sync \
            --name "$(basename "$pkg")" \
            --version "$UPLOAD_VERSION" \
            --summary "weather_routing_pi .${PKG_EXT} installation package" \
            "$REPO" "$pkg"
    fi
fi
