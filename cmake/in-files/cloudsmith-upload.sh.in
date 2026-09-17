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

PROD_REPO=${CLOUDSMITH_PROD_REPO:-'@CLOUDSMITH_USER@/@CLOUDSMITH_BASE_REPOSITORY@-@PROD@'}
BETA_REPO=${CLOUDSMITH_BETA_REPO:-'@CLOUDSMITH_USER@/@CLOUDSMITH_BASE_REPOSITORY@-@BETA@'}
ALPHA_REPO=${CLOUDSMITH_ALPHA_REPO:-'@CLOUDSMITH_USER@/@CLOUDSMITH_BASE_REPOSITORY@-@ALPHA@'}

LOCAL_BUILD=false

# -----------------------------
# CI environment detection
# -----------------------------
if [ "$CIRCLECI" ]; then
    BUILD_ID=${CIRCLE_BUILD_NUM:-1}
    BUILD_DIR=${BUILD_DIR:-"$HOME/project/build"}
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
    BUILD_DIR=$(pwd)
    BUILD_BRANCH=$APPVEYOR_REPO_BRANCH
    BUILD_TAG=$APPVEYOR_REPO_TAG_NAME
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'exe'}

else
    BUILD_ID=${CIRCLE_BUILD_NUM:-1}
    BUILD_DIR=.
    BUILD_BRANCH=$CIRCLE_BRANCH
    BUILD_TAG=$CIRCLE_TAG
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'deb'}
    LOCAL_BUILD=true
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
        pipx install cloudsmith-cli
        return
    fi

    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
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

if [ "$CIRCLECI" ] || [ "$TRAVIS" ]; then
    if [ "$DEPLOY_USE_ORB" != "true" ]; then
        install_cloudsmith_cli
    fi
fi

# -----------------------------
# Build metadata
# -----------------------------
commit=$(git rev-parse --short=7 HEAD || echo "unknown")
tag=$(git tag --contains HEAD || true)

xml=$(ls "$BUILD_DIR"/*.xml)
tarball=$(ls "$BUILD_DIR"/*.tar.gz)
tarball_basename=${tarball##*/}

source "$BUILD_DIR/pkg_version.sh"

if [ -n "${OCPN_TARGET}" ]; then
    tarball_name="@PACKAGE_NAME@-@PACKAGE_VERSION@-${PKG_TARGET}-@COMPOUND_ARCH_DASH@@PKG_TARGET_WX_VER@@PKG_BUILD_GTK@-${PKG_TARGET_VERSION}-${OCPN_TARGET}-tarball"
else
    tarball_name="@PACKAGE_NAME@-@PACKAGE_VERSION@-${PKG_TARGET}-@COMPOUND_ARCH_DASH@@PKG_TARGET_WX_VER@@PKG_BUILD_GTK@-${PKG_TARGET_VERSION}-tarball"
fi

pkg=$(ls "$BUILD_DIR"/*.${PKG_EXT} 2>/dev/null || echo "")

# -----------------------------
# Branch/tag → repo selection
# -----------------------------
echo "$BUILD_BRANCH"
echo "$BUILD_TAG"

if [ -z "$BUILD_TAG" ] && [ -n "$tag" ]; then
    BUILD_TAG=$tag
fi

if [ -z "$BUILD_BRANCH" ]; then
    build_commit=$(git show -s --format=%d "$BUILD_TAG")
    is_master=$(echo "$build_commit" | awk '/\/master/ {print}')
    [ -n "$is_master" ] && BUILD_BRANCH="master"
fi

BUILD_BRANCH_LOWER=$(echo "$BUILD_BRANCH" | tr 'A-Z' 'a-z')

if [ "$BUILD_BRANCH_LOWER" = "master" ]; then
    if [ -n "$BUILD_TAG" ]; then
        VERSION=$BUILD_TAG
        REPO="$PROD_REPO"
    else
        VERSION="@PROJECT_VERSION@+${BUILD_ID}.${commit}"
        REPO="$BETA_REPO"
    fi
else
    if [ -n "$BUILD_TAG" ]; then
        VERSION=$BUILD_TAG
        REPO="$BETA_REPO"
    else
        VERSION="@PROJECT_VERSION@+${BUILD_ID}.${commit}"
        REPO="$ALPHA_REPO"
    fi
fi

echo "$VERSION"
echo "$REPO"

# -----------------------------
# Substitute metadata variables
# -----------------------------
if [ "$APPVEYOR" ] || [ "$LOCAL_BUILD" = true ]; then
    while read -r line; do
        line=${line//--pkg_repo--/$REPO}
        line=${line//--name--/$tarball_name}
        line=${line//--version--/$VERSION}
        line=${line//--filename--/$tarball_basename}
        echo "$line"
    done < "$xml" > xml.tmp
    cp xml.tmp "$xml"
    rm xml.tmp
else
    sudo sed -i -e "s|--pkg_repo--|$REPO|" "$xml"
    sudo sed -i -e "s|--name--|$tarball_name|" "$xml"
    sudo sed -i -e "s|--version--|$VERSION|" "$xml"
    sudo sed -i -e "s|--filename--|$tarball_basename|" "$xml"
fi

# -----------------------------
# Rebuild tarball with metadata
# -----------------------------
gunzip -f "$tarball"
cd "$BUILD_DIR"
rm -f metadata.xml
tarball_tar=$(ls *.tar)
xml_here=$(ls *.xml)
cp -f "$xml_here" metadata.xml

mkdir build_tar
cp "$tarball_tar" build_tar/
cd build_tar
tar -xf "$tarball_tar"
rm *.tar
rm -rf root
cp ../metadata.xml .
tar -cf build_tarfile.tar *
cp build_tarfile.tar ../"$tarball_tar"
cd ..
rm -rf build_tar

gzip -f "$tarball_tar"

cd "$cur_dir"

# -----------------------------
# Upload to Cloudsmith
# -----------------------------
have_any() { [ $# -gt 0 ]; }

if [ "$LOCAL_BUILD" = false ]; then
    if [ "$CIRCLE_PROJECT_USERNAME" = "$CIRCLE_USERNAME" ] || \
       [[ -n "${collab_users+1}" && -n "$CIRCLE_USERNAME" && "$collab_users" =~ "$git_user" ]]; then

        cloudsmith push raw --republish --no-wait-for-sync \
            --name @PACKAGE_NAME@-@PACKAGE_VERSION@-@PKG_TARGET@-@COMPOUND_ARCH_DASH@@PKG_TARGET_WX_VER@@PKG_BUILD_GTK@-@PKG_TARGET_VERSION@-${OCPN_TARGET}-metadata \
            --version "$VERSION" \
            --summary "@PACKAGE@ opencpn plugin metadata for automatic installation" \
            "$REPO" "$xml"

        cloudsmith push raw --republish --no-wait-for-sync \
            --name "$tarball_name" \
            --version "$VERSION" \
            --summary "@PACKAGE@ opencpn plugin tarball for automatic installation" \
            "$REPO" "$tarball"

        if [ "$PKG_EXT" != "gz" ] && [ -n "$pkg" ]; then
            cloudsmith push raw --republish --no-wait-for-sync \
                --name opencpn-package-@PACKAGE@-@PACKAGE_VERSION@-@PKG_TARGET@-@COMPOUND_ARCH_DASH@@PKG_TARGET_WX_VER@@PKG_BUILD_GTK@-@PKG_TARGET_VERSION@-${OCPN_TARGET}.${PKG_EXT} \
                --version "$VERSION" \
                --summary "@PACKAGE@ .${PKG_EXT} installation package" \
                "$REPO" "$pkg"
        fi
    fi
fi
