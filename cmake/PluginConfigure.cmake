# ---------------------------------------------------------------------------
# Author:      Jon Gough (Based on the work of Sean D'Epagnier and Pavel Kalian)
# Copyright:   2019  License: GPLv3+
# ---------------------------------------------------------------------------

set(SAVE_CMLOC ${CMLOC})
set(CMLOC "PluginConfigure: ")

message(STATUS "${CMLOC}*** Staging to build ${PACKAGE_NAME} ***")
message(STATUS "${CMLOC}CIRCLECI: ${CIRCLECLI}, Env CIRCLECI: $ENV{CIRCLECI}")
message(STATUS "${CMLOC}TRAVIS: ${TRAVIS}, Env TRAVIS: $ENV{TRAVIS}")

# -----------------------------------------------------------------------------
# Git / repository metadata
# -----------------------------------------------------------------------------
set(GIT_REPOSITORY "")

if ($ENV{CIRCLECI})
  set(GIT_REPOSITORY
      "$ENV{CIRCLE_PROJECT_USERNAME}/$ENV{CIRCLE_PROJECT_REPONAME}")
  set(GIT_REPOSITORY_BRANCH "$ENV{CIRCLE_BRANCH}")
  set(GIT_REPOSITORY_TAG "$ENV{CIRCLE_TAG}")
elseif ($ENV{TRAVIS})
  set(GIT_REPOSITORY "$ENV{TRAVIS_REPO_SLUG}")
  set(GIT_REPOSITORY_BRANCH "$ENV{TRAVIS_BRANCH}")
  set(GIT_REPOSITORY_TAG "$ENV{TRAVIS_TAG}")
  if ("${GIT_REPOSITORY_BRANCH}" STREQUAL "${GIT_REPOSITORY_TAG}")
    set(GIT_REPOSITORY_BRANCH "")
  endif ()
elseif ($ENV{APPVEYOR})
  set(GIT_REPOSITORY "$ENV{APPVEYOR_REPO_NAME}")
  set(GIT_REPOSITORY_BRANCH "$ENV{APPVEYOR_REPO_BRANCH}")
  set(GIT_REPOSITORY_TAG "$ENV{APPVEYOR_REPO_TAG_NAME}")
else ()
  if ("${GIT_REPOSITORY_EXISTS}" STREQUAL "0")
    execute_process(
      COMMAND git rev-parse --abbrev-ref HEAD
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_REPOSITORY_BRANCH
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if ("${GIT_REPOSITORY_BRANCH}" STREQUAL "")
      message(STATUS "${CMLOC}Setting default GIT repository branch - master")
      set(GIT_REPOSITORY_BRANCH "master")
    endif ()

    execute_process(
      COMMAND git tag --contains
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_REPOSITORY_TAG
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    execute_process(
      COMMAND git status --porcelain -b
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_STATUS
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(FIND "${GIT_STATUS}" "..." START_TRACKED)
    if (NOT START_TRACKED EQUAL -1)
      math(EXPR START_TRACKED "${START_TRACKED}+3")
      string(SUBSTRING "${GIT_STATUS}" ${START_TRACKED} -1 TRACKED_STATUS)
      string(FIND "${TRACKED_STATUS}" "/" END_TRACKED)
      string(SUBSTRING "${TRACKED_STATUS}" 0 ${END_TRACKED}
                       GIT_REPOSITORY_REMOTE
      )
      message(STATUS "${CMLOC}GIT_REPOSITORY_REMOTE: ${GIT_REPOSITORY_REMOTE}")

      execute_process(
        COMMAND git remote get-url ${GIT_REPOSITORY_REMOTE}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_REPOSITORY_URL
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE GIT_REMOTE_ERROR
      )
      if (GIT_REMOTE_ERROR STREQUAL "")
        string(FIND ${GIT_REPOSITORY_URL} ${GIT_REPOSITORY_SERVER} START_URL
               REVERSE)
        string(LENGTH ${GIT_REPOSITORY_SERVER} STRING_LENGTH)
        math(EXPR START_URL "${START_URL}+1+${STRING_LENGTH}")
        string(LENGTH ${GIT_REPOSITORY_URL} STRING_LENGTH)
        string(SUBSTRING ${GIT_REPOSITORY_URL} ${START_URL} ${STRING_LENGTH}
                         GIT_REPOSITORY)
      else ()
        message(STATUS "${CMLOC}Command error: ${GIT_REMOTE_ERROR}")
        message(STATUS "${CMLOC}Using default repository")
      endif ()
    else ()
      message(STATUS "${CMLOC}Branch is not tracking a remote branch")
    endif ()
  else ()
    message(
      STATUS
        "${CMLOC}This directory does not contain git or git is not available")
    set(GIT_REPOSITORY "")
    set(GIT_REPOSITORY_BRANCH "")
    set(GIT_REPOSITORY_TAG "")
  endif ()
endif ()
if (DEFINED PLUGIN_GIT_REPOSITORY
    AND NOT "${PLUGIN_GIT_REPOSITORY}" STREQUAL "")
  set(GIT_REPOSITORY "${PLUGIN_GIT_REPOSITORY}")
  message(STATUS "${CMLOC}Using configured canonical plugin repository")
endif ()
message(STATUS "${CMLOC}GIT_REPOSITORY: ${GIT_REPOSITORY}")
message(STATUS "${CMLOC}Git Branch: \"${GIT_REPOSITORY_BRANCH}\"")
message(STATUS "${CMLOC}Git Tag: \"${GIT_REPOSITORY_TAG}\"")

if ("${GIT_REPOSITORY_BRANCH}" STREQUAL "")
  set(GIT_BRANCH_OR_TAG "tag")
  set(GIT_REPOSITORY_ITEM ${GIT_REPOSITORY_TAG})
else ()
  set(GIT_BRANCH_OR_TAG "branch")
  set(GIT_REPOSITORY_ITEM ${GIT_REPOSITORY_BRANCH})
endif ()
message(STATUS "${CMLOC}GIT_BRANCH_OR_TAG: ${GIT_BRANCH_OR_TAG}")
message(STATUS "${CMLOC}GIT_REPOSITORY_ITEM: ${GIT_REPOSITORY_ITEM}")

if (NOT DEFINED CLOUDSMITH_BASE_REPOSITORY AND NOT ${GIT_REPOSITORY} STREQUAL "")
  string(FIND ${GIT_REPOSITORY} "/" START_NAME REVERSE)
  math(EXPR START_NAME "${START_NAME}+1")
  string(LENGTH ${GIT_REPOSITORY} STRING_LENGTH)
  string(SUBSTRING ${GIT_REPOSITORY} ${START_NAME} ${STRING_LENGTH}
                   CLOUDSMITH_BASE_REPOSITORY)
endif ()
message(
  STATUS "${CMLOC}CLOUDSMITH_BASE_REPOSITORY: ${CLOUDSMITH_BASE_REPOSITORY}")

# -----------------------------------------------------------------------------
# Version / extra headers from in-files
# -----------------------------------------------------------------------------
if (NOT SKIP_VERSION_CONFIG)
  if (MINGW)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE_SAVE
        ${CMAKE_FIND_ROOT_PATH_MODE_INCLUDE})
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
  endif ()

  set(BUILD_INCLUDE_PATH ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY})
  unset(PLUGIN_EXTRA_VERSION_VARS CACHE)

  find_file(
    PLUGIN_EXTRA_VERSION_VARS
    NAMES version.h.extra
    PATHS ${CMAKE_CURRENT_LIST_DIR}/in-files
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
  )
  if (PLUGIN_EXTRA_VERSION_VARS STREQUAL "PLUGIN_EXTRA_VERSION_VARS-NOTFOUND")
    set(EXTRA_VERSION_INFO "")
  else ()
    configure_file(
      ${PLUGIN_EXTRA_VERSION_VARS}
      ${BUILD_INCLUDE_PATH}/include/version_extra.h
    )
    set(EXTRA_VERSION_INFO "#include \"version_extra.h\"")
  endif ()

  find_file(
    PLUGIN_EXTRA_FORMBUILDER_HEADERS
    NAMES extra_formbuilder_headers.h.in
    PATHS ${CMAKE_CURRENT_LIST_DIR}/in-files
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
  )
  if (PLUGIN_EXTRA_FORMBUILDER_HEADERS STREQUAL
      "PLUGIN_EXTRA_FORMBUILDER_HEADERS-NOTFOUND")
    # optional, ignore if missing
  else ()
    configure_file(
      ${PLUGIN_EXTRA_FORMBUILDER_HEADERS}
      ${BUILD_INCLUDE_PATH}/include/extra_formbuilder_headers.h
    )
  endif ()

  configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/in-files/version.h.in
    ${BUILD_INCLUDE_PATH}/include/version.h
  )
  configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/in-files/wxWTranslateCatalog.h.in
    ${BUILD_INCLUDE_PATH}/include/wxWTranslateCatalog.h
  )
  include_directories(${BUILD_INCLUDE_PATH}/include)

  if (MINGW)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE
        ${CMAKE_FIND_ROOT_PATH_MODE_INCLUDE_SAVE})
  endif ()
endif ()

# -----------------------------------------------------------------------------
# GTK / build env flags
# -----------------------------------------------------------------------------
message(STATUS "${CMLOC}ENV BUILD_GTK2: $ENV{BUILD_GTK2}")
string(TOUPPER "$ENV{BUILD_GTK2}" BUILD_GTK2_TEMP)
set(ENV{BUILD_GTK2} ${BUILD_GTK2_TEMP})

message(STATUS "${CMLOC}ENV BUILD_GTK3: $ENV{BUILD_GTK3}")
string(TOUPPER "$ENV{BUILD_GTK3}" BUILD_GTK3_TEMP)
set(ENV{BUILD_GTK3} ${BUILD_GTK3_TEMP})

string(TOUPPER "$ENV{BUILD_ENV}" BUILD_ENV_TEMP)
message(
  STATUS
    "${CMLOC}BUILD_ENV: $ENV{BUILD_ENV}, BUILD_ENV_TEMP ${BUILD_ENV_TEMP}, OCPN_TARGET: $ENV{OCPN_TARGET}")
message(STATUS "${CMLOC}Doing build_gtk3: $ENV{BUILD_GTK3}")

if ("$ENV{BUILD_GTK3}" STREQUAL "TRUE"
    AND (("${BUILD_ENV_TEMP}" STREQUAL "UBUNTU"
          AND NOT "$ENV{OCPN_TARGET}" STREQUAL "jammy")
         OR "$ENV{OCPN_TARGET}" STREQUAL "buster-armhf"))
  set(PKG_TARGET_GTK "gtk3")
else ()
  unset(PKG_TARGET_GTK)
endif ()
message(STATUS "${CMLOC}PKG_TARGET_GTK: ${PKG_TARGET_GTK}")

if (UNIX AND NOT APPLE)
  string(STRIP "${PKG_TARGET}" PKG_TARGET)
  string(TOLOWER "${PKG_TARGET}" PKG_TARGET)

  if (ARCH MATCHES "aarch64")
    set(PKG_TARGET_ARCH "-aarch64")
  elseif (ARCH MATCHES "arm64")
    set(PKG_TARGET_ARCH "-arm64")
  elseif (ARCH MATCHES "armhf")
    set(PKG_TARGET_ARCH "-armhf")
  elseif (ARCH MATCHES "i386")
    set(PKG_TARGET_ARCH "-i386")
  elseif (ARCH MATCHES "amd64")
    set(PKG_TARGET_ARCH "-amd64")
  else ()
    set(PKG_TARGET_ARCH "-x86_64")
  endif ()
else ()
  if (MINGW)
    set(PKG_TARGET_ARCH "-x86_64")
  else ()
    set(PKG_TARGET_ARCH "")
  endif ()
endif ()

message(STATUS "${CMLOC}ARCH: ${ARCH}")

if ("${PKG_BUILD_TARGET}" STREQUAL "")
  set(PKG_BUILD_TARGET "${PKG_TARGET}")
  set(PKG_TARGET_BUILD "-${PKG_BUILD_TARGET}")
endif ()

# -----------------------------------------------------------------------------
# GTK2/GTK3 detection (non-Windows, non-Apple, non-Android)
# -----------------------------------------------------------------------------
if (NOT WIN32 AND NOT APPLE AND NOT QT_ANDROID)
  if (BUILD_GTK2)
    find_package(GTK2)
  endif ()

  if (GTK2_FOUND AND NOT "$ENV{BUILD_GTK3}" STREQUAL "TRUE")
    set(wxWidgets_CONFIG_OPTIONS ${wxWidgets_CONFIG_OPTIONS} --toolkit=gtk2)
    include_directories(${GTK2_INCLUDE_DIRS})
    set(GTK_LIBRARIES ${GTK2_LIBRARIES})
    set(PKG_BUILT_WITH_GTK "gtk2")
  else ()
    find_package(GTK3)
    if (GTK3_FOUND)
      include_directories(${GTK3_INCLUDE_DIRS})
      set(GTK_LIBRARIES ${GTK3_LIBRARIES})
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__WXGTK3__")
      set(wxWidgets_CONFIG_OPTIONS ${wxWidgets_CONFIG_OPTIONS} --toolkit=gtk3)
      set(PKG_BUILT_WITH_GTK "gtk3")
    else ()
      set(PKG_BUILT_WITH_GTK "")
    endif ()
  endif ()
  set(EXTRA_LIBS ${EXTRA_LIBS} ${GTK_LIBRARIES})
else ()
  set(PKG_BUILT_WITH_GTK "${PKG_TARGET_GTK}")
endif ()

if (NOT "${PKG_TARGET_GTK}" STREQUAL "")
  set(PKG_BUILD_GTK "-${PKG_TARGET_GTK}")
  set(PKG_TARGET_GTK "-${PKG_TARGET_GTK}")
endif ()

# -----------------------------------------------------------------------------
# Packaging name / Flatpak extras
# -----------------------------------------------------------------------------
message(
  STATUS
    "${CMLOC}PKG_NVR: ${PKG_NVR}, PKG_TARGET: ${PKG_TARGET}, ARCH: ${ARCH}, PKG_TARGET_WX_VER: ${PKG_TARGET_WX_VER}, PKG_BUILD_GTK: ${PKG_BUILD_GTK}, PKG_TARGET_VERSION: ${PKG_TARGET_VERSION}, OCPN_TARGET: $ENV{OCPN_TARGET}"
)

if (DEFINED ENV{OCPN_TARGET})
  if (OCPN_FLATPAK_CONFIG
      OR OCPN_FLATPAK_BUILD
      OR MINGW
      OR MSVC
      OR (("$ENV{OCPN_TARGET}" STREQUAL "bookworm-armhf"
           OR "$ENV{OCPN_TARGET}" STREQUAL "bookworm-arm64")
          AND "$ENV{BUILD_ENV}" STREQUAL "debian")
      OR (("$ENV{OCPN_TARGET}" STREQUAL "bullseye-armhf"
           OR "$ENV{OCPN_TARGET}" STREQUAL "bullseye-arm64")
          AND "$ENV{WX_VER}" STREQUAL "32"
          AND "$ENV{BUILD_ENV}" STREQUAL "debian"))
    set(PACKAGING_NAME
        "${PKG_NVR}-${PKG_TARGET}-${ARCH}${PKG_TARGET_WX_VER}${PKG_BUILD_GTK}-${PKG_TARGET_VERSION}-$ENV{OCPN_TARGET}"
    )
    set(PACKAGING_NAME_XML
        "${PKG_NVR}-${PKG_TARGET}-${ARCH}${PKG_TARGET_WX_VER}${PKG_BUILD_GTK}-${PKG_TARGET_VERSION}-$ENV{OCPN_TARGET}"
    )
  else ()
    if (APPLE AND CMAKE_OSX_ARCHITECTURES)
      set(PACKAGING_NAME
          "${PKG_NVR}-${PKG_TARGET}-${PKG_TARGET_VERSION}${PKG_BUILD_GTK}-$ENV{OCPN_TARGET}"
      )
      set(PACKAGING_NAME_XML
          "${PKG_NVR}-${PKG_TARGET}-${COMPOUND_ARCH_DASH}-${PKG_TARGET_VERSION}${PKG_BUILD_GTK}-$ENV{OCPN_TARGET}"
      )
    else ()
      set(PACKAGING_NAME
          "${PKG_NVR}-${PKG_TARGET}-${PKG_TARGET_VERSION}${PKG_BUILD_GTK}-$ENV{OCPN_TARGET}"
      )
      set(PACKAGING_NAME_XML
          "${PKG_NVR}-${PKG_TARGET}-${ARCH}-${PKG_TARGET_VERSION}${PKG_BUILD_GTK}-$ENV{OCPN_TARGET}"
      )
    endif ()
  endif ()
else ()
  if (OCPN_FLATPAK_CONFIG OR OCPN_FLATPAK_BUILD OR MINGW OR MSVC)
    set(PACKAGING_NAME
        "${PKG_NVR}-${PKG_TARGET}-${ARCH}-${PKG_TARGET_VERSION}${PKG_BUILD_GTK}"
    )
    set(PACKAGING_NAME_XML
        "${PKG_NVR}-${PKG_TARGET}-${ARCH}-${PKG_TARGET_VERSION}${PKG_BUILD_GTK}"
    )
  else ()
    set(PACKAGING_NAME
        "${PKG_NVR}-${PKG_TARGET}-${PKG_TARGET_VERSION}${PKG_BUILD_GTK}"
    )
    set(PACKAGING_NAME_XML
        "${PKG_NVR}-${PKG_TARGET}-${ARCH}-${PKG_TARGET_VERSION}${PKG_BUILD_GTK}"
    )
  endif ()
endif ()

if (OCPN_FLATPAK_CONFIG)
  find_file(
    PLUGIN_FLATPAK_ARGS
    NAMES flatpak_args.in
    PATHS ${CMAKE_CURRENT_LIST_DIR}/in-files
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
  )
  if (PLUGIN_FLATPAK_ARGS STREQUAL "PLUGIN_FLATPAK_ARGS-NOTFOUND")
    set(finish_args "")
  else ()
    file(READ ${PLUGIN_FLATPAK_ARGS} finish_args)
  endif ()

  find_file(
    PLUGIN_FLATPAK_OPTIONS
    NAMES flatpak_options.in
    PATHS ${CMAKE_CURRENT_LIST_DIR}/in-files
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
  )
  if (PLUGIN_FLATPAK_OPTIONS STREQUAL "PLUGIN_FLATPAK_OPTIONS-NOTFOUND")
    set(build_options "")
  else ()
    file(READ ${PLUGIN_FLATPAK_OPTIONS} build_options)
  endif ()

  find_file(
    PLUGIN_FLATPAK_EXTRA_MODULES
    NAMES flatpak_extra_modules.in
    PATHS ${CMAKE_CURRENT_LIST_DIR}/in-files
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
  )
  if (PLUGIN_FLATPAK_EXTRA_MODULES STREQUAL
      "PLUGIN_FLATPAK_EXTRA_MODULES-NOTFOUND")
    set(flatpak_extra_modules "")
  else ()
    file(READ ${PLUGIN_FLATPAK_EXTRA_MODULES} flatpak_extra_modules)
  endif ()
endif ()

message(STATUS "${CMLOC}PACKAGING_NAME: ${PACKAGING_NAME}")
message(STATUS "${CMLOC}PACKAGING_NAME_XML: ${PACKAGING_NAME_XML}")

set(PKG_TARGET_FULL
    "${PKG_TARGET}${PKG_TARGET_GTK}${PKG_TARGET_WX_VER}${PKG_TARGET_ARCH}")
message(STATUS "${CMLOC}PKG_TARGET_FULL: ${PKG_TARGET_FULL}")
message(STATUS "${CMLOC}PKG_BUILD_TARGET: ${PKG_BUILD_TARGET}")
message(STATUS "${CMLOC}PKG_BUILD_GTK: ${PKG_TARGET_GTK}")
message(STATUS "${CMLOC}PKG_BUILT_WITH_GTK: ${PKG_BUILT_WITH_GTK}")

configure_file(
  ${PROJECT_SOURCE_DIR}/cmake/in-files/plugin.xml.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGING_NAME_XML}.xml
)
configure_file(
  ${PROJECT_SOURCE_DIR}/cmake/in-files/pkg_version.sh.in
  ${CMAKE_CURRENT_BINARY_DIR}/pkg_version.sh
)
configure_file(
  ${PROJECT_SOURCE_DIR}/cmake/in-files/cloudsmith-upload.sh.in
  ${CMAKE_CURRENT_BINARY_DIR}/cloudsmith-upload.sh @ONLY
)
configure_file(
  ${PROJECT_SOURCE_DIR}/cmake/in-files/PluginCPackOptions.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/PluginCPackOptions.cmake @ONLY
)

if (OCPN_FLATPAK_CONFIG)
  set(RUNTIME_VERSION ${FLATPAK_BRANCH})
  configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/in-files/org.opencpn.OpenCPN.Plugin.yaml.in
    ${CMAKE_CURRENT_BINARY_DIR}/flatpak/org.opencpn.OpenCPN.Plugin.${PACKAGE}.yaml
  )
  set(CMLOC ${SAVE_CMLOC})
  return()
endif ()

# -----------------------------------------------------------------------------
# Compiler / linker flags
# -----------------------------------------------------------------------------
set(CMAKE_VERBOSE_MAKEFILE ON)

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

set(CMAKE_SHARED_LINKER_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "")

if (CMAKE_BUILD_TYPE STREQUAL "Debug"
    OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  add_definitions("-DDEBUG_BUILD")
  set(CMAKE_INSTALL_DO_STRIP FALSE)
  set(CPACK_DEBIAN_DEBUGINFO_PACKAGE YES)
endif ()

if (NOT WIN32 AND NOT APPLE)
  add_definitions("-Wall -Wno-unused -fexceptions -rdynamic -fvisibility=hidden")
  add_definitions("-fno-strict-aliasing")
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions("-O0 -g")
  elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions("-O2 -s")
  elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_definitions("-O2 -g")
  else ()
    add_definitions("-O2 -s")
  endif ()
  add_definitions("-DPREFIX=\\\"${CMAKE_INSTALL_PREFIX}\\\"")
endif ()

if (MINGW)
  add_definitions("-Wall -Wno-unused -Wno-cpp -fexceptions")
  add_definitions("-g -fno-strict-aliasing")
  if (CMAKE_BUILD_TYPE STREQUAL "Release"
      OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_link_options(-Wl,--strip-all)
  endif ()
endif ()

if (APPLE)
  string(APPEND CMAKE_CXX_FLAGS
         " -Wall -Wno-unused -fexceptions -Wno-overloaded-virtual")
  string(APPEND CMAKE_CXX_FLAGS " -g -fno-strict-aliasing")
  string(APPEND CMAKE_CXX_FLAGS
         " -Wno-deprecated -Wno-deprecated-declarations -Wno-unknown-pragmas")
  string(APPEND CMAKE_CXX_FLAGS " -D_WCHAR_H_CPLUSPLUS_98_CONFORMANCE_")
  string(APPEND CMAKE_CXX_FLAGS " -DAPPLE")
endif ()

if (MSVC)
  add_definitions(-D__MSVC__)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE)
  add_definitions(-DHAVE_SNPRINTF)
else ()
  if (NOT APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-Bsymbolic")
  else ()
    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -Wl -undefined dynamic_lookup")
  endif ()
endif ()

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
set(BUILD_SHARED_LIBS TRUE)

if (MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif ()

if (WIN32 AND MSVC)
  set(CMAKE_C_FLAGS_DEBUG "/MP /MDd /Ob0 /Od /D_DEBUG /Zi /RTC1")
  set(CMAKE_C_FLAGS_MINSIZEREL "/MP /MD /O1 /Ob1 /D NDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "/MP /MD /O2 /Ob2 /D NDEBUG /Zi")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "/MP /MD /O2 /Ob1 /D NDEBUG /Zi")
  set(CMAKE_CXX_FLAGS_DEBUG "/MP /MDd /Ob0 /Od /D_DEBUG /Zi /RTC1 /EHa")
  set(CMAKE_CXX_FLAGS_MINSIZEREL "/MP /MD /O1 /Ob1 /D NDEBUG /EHa")
  set(CMAKE_CXX_FLAGS_RELEASE "/MP /MD /O2 /Ob2 /D NDEBUG /Zi /EHa")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MP /MD /O2 /Ob1 /D NDEBUG /Zi /EHa")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
endif ()

set(wxWidgets_USE_LIBS base core net xml html adv aui)

# -----------------------------------------------------------------------------
# OpenGL / GLES
# -----------------------------------------------------------------------------
if (ARCH MATCHES "arm*" AND (NOT QT_ANDROID) AND USE_GL MATCHES "ON")
  find_path(OPENGLESv1_INCLUDE_DIR GLES/gl.h)
  if (OPENGLESv1_INCLUDE_DIR)
    add_definitions(-DocpnUSE_GLES -DocpnUSE_GL)
    set(OPENGLES_FOUND "YES")
    set(OPENGL_FOUND "YES")
    set(wxWidgets_USE_LIBS ${wxWidgets_USE_LIBS} gl)
    set(OPENGL_LIBRARIES "GL_static" "EGL" "X11" "drm")
  endif ()
endif ()

if (DEFINED _wx_selected_config)
  if (_wx_selected_config MATCHES "androideabi-qt")
    add_definitions(-DocpnUSE_GLES -DocpnUSE_GL -DARMHF)
    set(OPENGLES_FOUND "YES")
    set(OPENGL_FOUND "YES")
    add_definitions(-DUSE_GLU_TESS -DUSE_ANDROID_GLES2 -DUSE_GLSL)
  endif ()
endif ()

if (QT_ANDROID)
  add_definitions(-D__WXQT__ -D__OCPN__ANDROID__ -DOCPN_USE_WRAPPER -DANDROID)
  set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-soname,libgorp.so ")
  set(CMAKE_CXX_FLAGS "-pthread -fPIC ")
  add_compile_options(
    "-Wno-inconsistent-missing-override"
    "-Wno-potentially-evaluated-expression"
    "-Wno-overloaded-virtual"
    "-Wno-unused-command-line-argument"
    "-Wno-unknown-pragmas"
  )
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")
  set(QT_LINUX "OFF")
  set(QT "ON")
  set(CMAKE_SKIP_BUILD_RPATH TRUE)
  add_definitions(-DQT_WIDGETS_LIB)
  if ("$ENV{OCPN_TARGET}" STREQUAL "android-arm64")
    add_definitions(-DARM64)
  else ()
    add_definitions(-DARMHF)
  endif ()
endif ()

if ((NOT OPENGLES_FOUND) AND (NOT QT_ANDROID))
  if (USE_GL MATCHES "ON")
    find_package(OpenGL)
  endif ()
  if (OPENGL_FOUND)
    set(wxWidgets_USE_LIBS ${wxWidgets_USE_LIBS} gl)
    include_directories(${OPENGL_INCLUDE_DIR})
    add_definitions(-DocpnUSE_GL)
    unset(REVISED_OPENGL_LIBRARIES)
    foreach (_currentLibFile ${OPENGL_LIBRARIES})
      string(TOUPPER ${_currentLibFile} UCNAME)
      if (NOT ${UCNAME} MATCHES "(.*)GLU(.*)")
        list(APPEND REVISED_OPENGL_LIBRARIES ${_currentLibFile})
      endif ()
    endforeach ()
    set(OPENGL_LIBRARIES ${REVISED_OPENGL_LIBRARIES})
  endif ()
endif ()

# -----------------------------------------------------------------------------
# wxWidgets dual‑mode: CONFIG preferred, legacy fallback (MSVC forced legacy)
# -----------------------------------------------------------------------------
if (NOT QT_ANDROID)
  set(CMLOC "[PluginConfigure]")
  message(STATUS "${CMLOC}wxWidgets components: ${wxWidgets_USE_LIBS}")

  set(USE_WX_CONFIG_MODE OFF)

  # The Flatpak SDK's wxWidgets CONFIG export can reference OpenGL::GLU even
  # when that target is unavailable. The module finder uses wx-config and
  # avoids importing the broken wx::wxgl link interface.
  if (MSVC OR OCPN_FLATPAK_BUILD)
    message(STATUS "${CMLOC}Using legacy FindwxWidgets for MSVC/Flatpak")
  else ()
    # 1) Try CONFIG-mode with explicit root, if provided
    if (wxWidgets_ROOT_DIR)
      set(_wx_config_paths
        "${wxWidgets_ROOT_DIR}"
        "${wxWidgets_ROOT_DIR}/lib/cmake/wxWidgets"
        "${wxWidgets_ROOT_DIR}/lib/vc_dll/cmake/wxWidgets"
      )
      find_package(wxWidgets CONFIG QUIET
        PATHS ${_wx_config_paths}
        NO_DEFAULT_PATH
      )
      if (wxWidgets_FOUND)
        message(STATUS "${CMLOC}Using wxWidgets CONFIG-mode from: ${wxWidgets_ROOT_DIR}")
        set(USE_WX_CONFIG_MODE ON)
      endif ()
    endif ()

    # 2) Fallback: generic CONFIG-mode search (vcpkg/system)
    if (NOT USE_WX_CONFIG_MODE)
      find_package(wxWidgets CONFIG QUIET)
      if (wxWidgets_FOUND)
        message(STATUS "${CMLOC}Using wxWidgets CONFIG-mode from default locations")
        set(USE_WX_CONFIG_MODE ON)
      endif ()
    endif ()
  endif ()

  # ---------------------------------------------------------------------------
  # Legacy FindwxWidgets (Linux, MinGW, macOS, MSVC fallback)
  # ---------------------------------------------------------------------------
  if (NOT USE_WX_CONFIG_MODE)
    message(STATUS "${CMLOC}Using legacy FindwxWidgets")

	if (MSVC AND wxWidgets_ROOT_DIR)
		set(wxWidgets_LIB_DIR "${wxWidgets_ROOT_DIR}/lib/vc_dll")
		set(wxWidgets_INCLUDE_DIRS "${wxWidgets_ROOT_DIR}/lib/vc_dll/mswud")
		set(wxWidgets_EXCLUDE_COMMON_LIBRARIES TRUE)
	endif ()

    find_package(wxWidgets MODULE REQUIRED COMPONENTS ${wxWidgets_USE_LIBS})
    include(${wxWidgets_USE_FILE})

    message(STATUS "${CMLOC} wxWidgets Include: ${wxWidgets_INCLUDE_DIRS}")
    message(STATUS "${CMLOC} wxWidgets Libraries: ${wxWidgets_LIBRARIES}")

    # Remove GLU from wxWidgets_LIBRARIES (MSW quirk)
    unset(REVISED_wxWidgets_LIBRARIES)
    foreach (_currentLibFile ${wxWidgets_LIBRARIES})
      string(TOUPPER ${_currentLibFile} UCNAME)
      if (NOT ${UCNAME} MATCHES "(.*)GLU(.*)")
        list(APPEND REVISED_wxWidgets_LIBRARIES ${_currentLibFile})
      endif ()
    endforeach ()
    set(wxWidgets_LIBRARIES ${REVISED_wxWidgets_LIBRARIES})

    # Synthesize wx:: imported targets for legacy builds
    set(_wx_components base core net xml html adv aui gl)
    foreach (_comp IN LISTS _wx_components)
      if (NOT TARGET wx::${_comp})
        add_library(wx::${_comp} INTERFACE IMPORTED)
        target_link_libraries(wx::${_comp} INTERFACE ${wxWidgets_LIBRARIES})
      endif ()
    endforeach ()
  endif ()

  # ---------------------------------------------------------------------------
  # Link plugin against wxWidgets imported targets (CONFIG or synthesized)
  # ---------------------------------------------------------------------------
  target_link_libraries(${PACKAGE_NAME} PRIVATE
    wx::base
    wx::core
    wx::net
    wx::xml
    wx::html
    wx::adv
    wx::aui
  )
  if (USE_GL MATCHES "ON")
    target_link_libraries(${PACKAGE_NAME} PRIVATE wx::gl)
  endif ()

  message(STATUS "${CMLOC}Final wxWidgets mode: "
                  "$<IF:${USE_WX_CONFIG_MODE},CONFIG,LEGACY>")
endif ()  # NOT QT_ANDROID

# -----------------------------------------------------------------------------
# Android-specific wx/Qt wiring
# -----------------------------------------------------------------------------
if (QT_ANDROID)
  if (_wx_selected_config MATCHES "androideabi-qt-arm64")
    set(qt_android_include
        ${qt_android_include}
        "${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include"
        "${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtCore"
        "${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtWidgets"
        "${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtGui"
        "${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtOpenGL"
        "${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtTest"
        "${OCPN_Android_Common}/wxWidgets/libarm64/wx/include/arm-linux-androideabi-qt-unicode-static-3.1"
        "${OCPN_Android_Common}/wxWidgets/include"
    )

    set(wxWidgets_LIBRARIES
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/lib/libQt5Core.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/lib/libQt5OpenGL.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/lib/libQt5Widgets.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/lib/libQt5Gui.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/lib/libQt5AndroidExtras.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/opencpn/API-117/libarm64/libgorp.so
        -lc++_shared
        -lz
        libGLESv2.so
        libEGL.so
    )
  else ()
    set(qt_android_include
        ${qt_android_include}
        "${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include"
        "${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtCore"
        "${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtWidgets"
        "${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtGui"
        "${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtOpenGL"
        "${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtTest"
        "${OCPN_Android_Common}/wxWidgets/libarmhf/wx/include/arm-linux-androideabi-qt-unicode-static-3.1"
        "${OCPN_Android_Common}/wxWidgets/include"
    )

    add_definitions(-DOCPN_ARMHF)

    set(wxWidgets_LIBRARIES
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/lib/libQt5Core.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/lib/libQt5OpenGL.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/lib/libQt5Widgets.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/lib/libQt5Gui.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/lib/libQt5AndroidExtras.so
        ${CMAKE_CURRENT_SOURCE_DIR}/${OCPN_Android_Common}/opencpn/API-117/libarmhf/libgorp.so
        -lc++_shared
        -lz
        libGLESv2.so
        libEGL.so
    )
  endif ()

  include_directories(BEFORE ${qt_android_include})
endif ()  # QT_ANDROID

# -----------------------------------------------------------------------------
# Gettext
# -----------------------------------------------------------------------------
find_package(Gettext REQUIRED)

set(CMLOC ${SAVE_CMLOC})
