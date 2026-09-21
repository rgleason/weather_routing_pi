#!/usr/bin/env bash

#
# Build for Raspbian and debian in a docker container
#

cd ~/project

git submodule update --init opencpn-libs

ls -la ~/project

# bailout on errors and echo commands.
set -x
sudo apt-get -y --allow-unauthenticated update

DOCKER_SOCK="unix:///var/run/docker.sock"

echo "DOCKER_OPTS=\"-H tcp://127.0.0.1:2375 -H $DOCKER_SOCK -s devicemapper\"" | sudo tee /etc/default/docker > /dev/null
sudo service docker restart
sleep 5;

if [ "$BUILD_ENV" = "raspbian" ]; then
    docker run --rm --privileged multiarch/qemu-user-static:register --reset
else
    docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
fi

docker run --privileged -d -ti -e "container=docker"  \
    -e "CIRCLECI=$CIRCLECI" \
    -e "CIRCLE_BRANCH=$CIRCLE_BRANCH" \
    -e "CIRCLE_TAG=$CIRCLE_TAG" \
    -e "CIRCLE_PROJECT_USERNAME=$CIRCLE_PROJECT_USERNAME" \
    -e "CIRCLE_PROJECT_REPONAME=$CIRCLE_PROJECT_REPONAME" \
    -e "GIT_REPOSITORY_SERVER=$GIT_REPOSITORY_SERVER" \
    -e "OCPN_TARGET=$OCPN_TARGET" \
    -e "BUILD_GTK3=$BUILD_GTK3" \
    -e "WX_VER=$WX_VER" \
    -e "BUILD_ENV=$BUILD_ENV" \
    -e "TZ=$TZ" \
    -e "DEBIAN_FRONTEND=$DEBIAN_FRONTEND" \
    -v $(pwd):/ci-source:rw -v ~/source_top:/source_top $DOCKER_IMAGE /bin/bash

DOCKER_CONTAINER_ID=$(docker ps | grep $DOCKER_IMAGE | awk '{print $1}')

echo "Target build: $OCPN_TARGET"
# Construct and run build script

rm -f build.sh

cat > build.sh << 'EOF'
#!/usr/bin/env bash
set -xe

install_packages() {
    apt-get -y --no-install-recommends --allow-change-held-packages --allow-unauthenticated install "$@"
}

echo "BUILD_ENV: $BUILD_ENV"
echo "OCPN_TARGET: $OCPN_TARGET"
echo "WX_VER: $WX_VER"
echo "BUILD_GTK3: $BUILD_GTK3"

install_base_deps() {
    apt-get -qq --allow-unauthenticated update
    apt-get -y --no-install-recommends --allow-change-held-packages --allow-unauthenticated install \
        git cmake build-essential gettext wx-common libgtk2.0-dev \
        libwxbase3.0-dev libwxgtk3.0-gtk3-dev \
        libbz2-dev libcurl4-openssl-dev libexpat1-dev libcairo2-dev \
        libarchive-dev liblzma-dev libexif-dev lsb-release
}

install_raspbian_cmake() {
    if [ "$OCPN_TARGET" = "buster-armhf" ]; then
        install_packages cmake=3.13.4-1 cmake-data=3.13.4-1
    else
        install_packages cmake cmake-data
    fi
}

install_raspbian_deps() {
    if [ "$OCPN_TARGET" = "bullseye-armhf" ]; then
        curl http://mirrordirector.raspbian.org/raspbian.public.key  | apt-key add -
        curl http://archive.raspbian.org/raspbian.public.key  | apt-key add -
        apt -q --allow-unauthenticated update
        apt --allow-unauthenticated install devscripts equivs wget git lsb-release
        mk-build-deps -ir ci-source/ci/control
        apt-get --allow-unauthenticated install -f
    else
        install_packages git build-essential devscripts equivs gettext wx-common \
            libgtk2.0-dev libwxbase3.0-dev libwxgtk3.0-dev libbz2-dev \
            libcurl4-openssl-dev libexpat1-dev libcairo2-dev libarchive-dev \
            liblzma-dev libexif-dev lsb-release
    fi
}

install_debian_base() {
    echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections
    apt-get -qq --allow-unauthenticated update && DEBIAN_FRONTEND='noninteractive' TZ='America/New_York' \
        apt-get -y --no-install-recommends --allow-change-held-packages install tzdata
    apt-get -y --fix-missing install --allow-change-held-packages --allow-unauthenticated \
        devscripts equivs wget git build-essential gettext wx-common libgtk2.0-dev \
        libbz2-dev libcurl4-openssl-dev libexpat1-dev libcairo2-dev libarchive-dev \
        liblzma-dev libexif-dev lsb-release openssl libssl-dev
    if [ "$OCPN_TARGET" = "bullseye-armhf" ] ||
       [ "$OCPN_TARGET" = "bullseye-arm64" ] ||
       [ "$OCPN_TARGET" = "bookworm-armhf" ] ||
       [ "$OCPN_TARGET" = "bookworm-arm64" ] ||
       [ "$OCPN_TARGET" = "bookworm" ] ||
       [ "$OCPN_TARGET" = "buster-armhf" ]; then
        apt-get -y --fix-missing --allow-change-held-packages --allow-unauthenticated install software-properties-common
    fi
}

install_wx_deps() {
    if [ "$OCPN_TARGET" = "buster-armhf" ] || [ "$OCPN_TARGET" = "bullseye-arm64" ]; then
        if [ -z "$BUILD_GTK3" ] || [ "$BUILD_GTK3" = "false" ]; then
            apt-get -y --no-install-recommends --fix-missing --allow-change-held-packages --allow-unauthenticated install \
                libwxgtk3.2-dev libwxbase3.2-dev
        else
            apt-get -y --no-install-recommends --fix-missing --allow-change-held-packages --allow-unauthenticated install \
                libwxgtk3.2-dev libwxbase3.2-dev
        fi
    fi

    if [ -z "$WX_VER" ] || [ "$WX_VER" = "30" ]; then
        apt-get -y --no-install-recommends --fix-missing --allow-change-held-packages --allow-unauthenticated install \
            libwxbase3.0-dev
    elif [ "$WX_VER" = "32" ]; then
        if [ "$OCPN_TARGET" = "bullseye-armhf" ] || [ "$OCPN_TARGET" = "bullseye-arm64" ]; then
            echo "deb [trusted=yes] https://ppa.launchpadcontent.net/opencpn/opencpn/ubuntu jammy main" | tee -a /etc/apt/sources.list
            echo "deb-src [trusted=yes] https://ppa.launchpadcontent.net/opencpn/opencpn/ubuntu jammy main" | tee -a /etc/apt/sources.list
            apt-get -y --allow-unauthenticated update
        fi
        apt-get -y --fix-missing --allow-change-held-packages --allow-unauthenticated install libwxgtk3.2-dev
    fi
}

install_cmake_debian() {
    if [ "$OCPN_TARGET" = "focal-armhf" ]; then
        CMAKE_VERSION=3.20.5-0kitware1ubuntu20.04.1
        wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc --no-check-certificate 2>/dev/null | apt-key add -
        apt-add-repository 'deb https://apt.kitware.com/ubuntu/ focal main'
        apt-get --allow-unauthenticated update
        apt --allow-unauthenticated install cmake="$CMAKE_VERSION" cmake-data="$CMAKE_VERSION"
    else
        apt install -y --allow-unauthenticated cmake
    fi
}

main() {
    if [ "$BUILD_ENV" = "raspbian" ]; then
        install_raspbian_cmake
        install_raspbian_deps
    else
        case "$OCPN_TARGET" in
            bullseye-armhf|bullseye-arm64|bookworm-armhf|bookworm-arm64|bookworm|trixie-armhf|trixie-arm64|trixie|buster-armhf)
                install_debian_base
                install_wx_deps
                install_cmake_debian
                ;;
            *)
                install_base_deps
                ;;
        esac
    fi
}

main "$@"
EOF

chmod +x build.sh



# Install extra build libs
ME=$(echo ${0##*/} | sed 's/\.sh//g')
EXTRA_LIBS=./ci/extras/extra_libs.txt
if test -f "$EXTRA_LIBS"; then
    while read line; do
        sudo apt-get install $line
    done < $EXTRA_LIBS
fi
EXTRA_LIBS=./ci/extras/${ME}_extra_libs.txt
if test -f "$EXTRA_LIBS"; then
    while read line; do
        sudo apt-get install $line
    done < $EXTRA_LIBS
fi

cat build.sh

if type nproc &> /dev/null
then
    BUILD_FLAGS="-j"$(nproc)
fi

docker exec -ti \
    $DOCKER_CONTAINER_ID /bin/bash -xec "bash -xe ci-source/build.sh; rm -rf ci-source/build; mkdir ci-source/build; cd ci-source/build; cmake ..; make $BUILD_FLAGS; make package; chmod -R a+rw ../build;"

echo "Stopping"
docker ps -a
docker stop $DOCKER_CONTAINER_ID
docker rm -v $DOCKER_CONTAINER_ID

