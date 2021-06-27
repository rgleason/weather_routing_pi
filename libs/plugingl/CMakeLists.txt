##---------------------------------------------------------------------------
## Author:      Sean D'Epagnier
## Copyright:   
## License:     GPLv3
##---------------------------------------------------------------------------

if (TARGET ocpn::plugingl)
    return ()
endif ()

set(SAVE_CMLOC_PLUGINGLL ${CMLOC})
set(CMLOC "CMakeLists_PLUGINGL: ")

message(STATUS "${CMLOC}Building PluginGL")
SET(SRC_PLUGINGL
          src/pidc.cpp
          src/pi_shaders.cpp
          src/TexFont.cpp
          src/qtstylesheet.cpp
          )

if(UNIX)
    add_definitions("-fpic")
endif(UNIX)

# Needed for android builds
if(QT_ANDROID)
    message(STATUS "${CMLOC}Building PluginGL: qt_android_include:  ${qt_android_include}")

    include_directories(${qt_android_include})
endif(QT_ANDROID)


ADD_LIBRARY(${PACKAGE_NAME}_LIB_PLUGINGL STATIC ${SRC_PLUGINGL})

target_include_directories(${PACKAGE_NAME}_LIB_PLUGINGL PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(ocpn::plugingl ALIAS ${PACKAGE_NAME}_LIB_PLUGINGL)

set(CMLOC ${SAVE_CMLOC_PLUGINGL})
