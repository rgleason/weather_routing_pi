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
#  There is probably a much simpler way to do this...
    IF(_wx_selected_config MATCHES "androideabi-qt-arm64")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtCore")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtWidgets")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtGui")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtOpenGL")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm64_O3/qtbase/include/QtTest")

        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/wxWidgets/libarm64/wx/include/arm-linux-androideabi-qt-unicode-static-3.1")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/wxWidgets/include")

    ELSE(_wx_selected_config MATCHES "androideabi-qt-arm64")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtCore")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtWidgets")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtGui")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtOpenGL")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/qt5/build_arm32_19_O3/qtbase/include/QtTest")

        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/wxWidgets/libarmhf/wx/include/arm-linux-androideabi-qt-unicode-static-3.1")
        include_directories( "${CMAKE_SOURCE_DIR}/${OCPN_Android_Common}/wxWidgets/include")

        ADD_DEFINITIONS( -DOCPN_ARMHF )

    ENDIF(_wx_selected_config MATCHES "androideabi-qt-arm64")

endif(QT_ANDROID)


ADD_LIBRARY(${PACKAGE_NAME}_LIB_PLUGINGL STATIC ${SRC_PLUGINGL})

target_include_directories(${PACKAGE_NAME}_LIB_PLUGINGL PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(ocpn::plugingl ALIAS ${PACKAGE_NAME}_LIB_PLUGINGL)

set(CMLOC ${SAVE_CMLOC_PLUGINGL})
