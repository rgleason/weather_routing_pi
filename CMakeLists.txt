# ---------------------------------------------------------------------------
# Author:      Sean d'Epagnier
# License: GPL v3+   License:  wxWidgets
# ---------------------------------------------------------------------------
# -   Copyright (C) 2010 by Sean d'Epagnier                              *
# -   License:     GPLv3+
# - This program is free software; you can redistribute it and/or modify  *
# - it under the terms of the GNU General Public License as published by  *
# - the Free Software Foundation; either version 2 of the License, or     *
# - (at your option) any later version.                                   *
# - *
# - This program is distributed in the hope that it will be useful,       *
# - but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# - MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# - GNU General Public License for more details.                          *
# - *
# - You should have received a copy of the GNU General Public License     *
# - along with this program; if not, write to the                         *
# - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,                                       *
# - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx                                       *
# -----------------------------------------------------------------------------
# Frontend2 Author: Jon Gough Testplugin(TP) v1.0.341.0 30/04/2025 DD/MM/YYYY
# format buildCI:yes useODAPI:no validateJSON:no
# ---------------------------------------------------------------------------
## -- When changing this file do NOT change the order of the sections.    -- ##
## -- Make changes only between the section blocks identified             -- ##
## -- The whole configuration process relies on this sequence for success.-- ##
##
## -- Modify section below to include all the details for your plugin.    -- ##

set(CMLOC "CMakeLists: ")

# Option to enable/disable testing
option(OCPN_BUILD_TEST "Build plugin tests" OFF)

# define minimum cmake version
cmake_minimum_required(VERSION 3.10)
if(COMMAND cmake_policy)
  if (POLICY CMP0043)
    cmake_policy(SET CMP0043 NEW)
  endif (POLICY CMP0043)
  cmake_policy(SET CMP0048 NEW)
  if (POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
  endif (POLICY CMP0068)
  if (POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
  endif (POLICY CMP0076)
  if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
  endif (POLICY CMP0077)
  if (POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
  endif (POLICY CMP0079)
  if (POLICY CMP0175)
    cmake_policy(SET CMP0175 NEW)
  endif (POLICY CMP0175)
  if (POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
  endif (POLICY CMP0177)
endif (COMMAND cmake_policy)

# set for vscode/vscodium
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# define plugin name, owner and versions
set(VERBOSE_NAME "xWeatherRouting")
set(COMMON_NAME "xWeatherRouting")  # Search "getcommonname"in <plugin_pi>.cpp
set(TITLE_NAME "xweatherrouting")
set(PACKAGE_CONTACT "Paul (pob220)")
set(PACKAGE "xweather_routing_pi")
set(SHORT_DESCRIPTION
    "Advanced deterministic weather routing with departure and arrival planning.")
set(LONG_DESCRIPTION
    "xWeatherRouting computes land-aware sailing routes using deterministic forward isochrones, reverse recovery, graph fallback, concurrent departure optimisation, and planned-arrival routing. Standard OpenCPN uses GSHHS land checks; optional chart-backed safety is enabled only when the host provides it.")

set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_PATCH "0")
set(VERSION_TWEAK "0")
set(VERSION_DATE  "30/07/2026") # DD/MM/YYYY format
set(OCPN_MIN_VERSION "ov511")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "21")
set(TP_COMMENT "  * Release using CI")

set(PARENT "opencpn")

#Use to set up a local webserver with git installed.
#Default is github.com. Now set in pluginconfigure.cmake
#set(GIT_REPOSITORY_SERVER "github.com")

## Specifies Cloudsmith upload repository suffix for each catalog
set(PROD "prod")   #Standard Repos
set(BETA "beta")   #Standard Repos
set(ALPHA "alpha") #Standard Repos

# Set if  Cloudsmith Base Repository name does not match your Git Repository name.
#set (CLOUDSMITH_BASE_REPOSITORY "${GIT_REPOSITORY_NAME}")
#set (CLOUDSMITH_BASE_REPOSITORY "opencpn-plugins")  # Ricks's customization
set(CLOUDSMITH_BASE_REPOSITORY "xweather-routing")
set(PLUGIN_GIT_REPOSITORY "pob220/xweather_routing_pi")

# Keep validation independent of the OpenCPN publishing repositories.  Alpha
# publication remains a separate, explicitly approved later step.
set(CLOUDSMITH_USER "pob220")

set(XML_INFO_URL
    "https://github.com/pob220/xweather_routing_pi"
)
set(XML_SUMMARY ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

## ----- Modify section above to include all the details for your plugin ------- ##
## ----- Modify section below if there are special requirements for the plugin --##

option(PLUGIN_USE_SVG "Use SVG graphics" ON)
option(XWEATHER_ROUTING_STANDALONE_API
       "Build against the vendored stock OpenCPN API even inside an OpenCPN source tree"
       OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use local version of GLU library requires libs/glu directory
# set(USE_LOCAL_GLU FALSE)
# Prefer libGL.so to libOpenGL.so, see CMP0072
set(OpenGL_GL_PREFERENCE "LEGACY")   #not operational
# For Weatherfax this must be set ON or it fails to compile
option(USE_GL "Enable OpenGL support" ON)
message(STATUS "${CMLOC}USE_GL: ${USE_GL}")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Choose the type of build" FORCE
  )
endif ("${CMAKE_BUILD_TYPE}" STREQUAL "")
set_property(
  CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "" "Debug" "Release" "RelWithDebInfo"
                                  "MinSizeRel"
)
message(STATUS "${CMLOC}Build type: ${CMAKE_BUILD_TYPE}")

# ----- Statements below used to setup standard variables required by the CMAKE  -##
# ----- process -- Do not remove -##

project(${PACKAGE})

# MSVC otherwise reports the historical 199711L value for __cplusplus even
# when /std:c++20 is active.  Frontend2's bundled JsonCpp uses __cplusplus to
# choose unique_ptr rather than the removed auto_ptr, and plugin code uses the
# same feature-test contract.
if(MSVC)
  add_compile_options(/Zc:__cplusplus)
endif()

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

##========================================================

include("PluginSetup")

##========================================================
## ----- Above sets up standard variables required by CMAKE - Do not remove -----##
## ----- Modify section below if there are special requirements for the plugin --##

set(CMAKE_VERBOSE_MAKEFILE "Activate verbose mode for make files" ON)

option(Plugin_CXX11 "Use c++11" OFF)

## ----- Modify section above if there are special requirements for the plugin --##
## ----- Do not change next section - needed to configure build process ---------##
##===========================================================

include("PluginConfigure")

##===========================================================
## ----- Do not change section above - needed to configure build process --------##
## ----- Change below to match project requirements for source, headers, etc. ---##

add_definitions(-DUSE_S57)

## ---- Section Below - Add your project SET(SRCS and SET(HDRS  etc..


SET(SRCS
            src/AppendOnlyCache.cpp
            src/ChartSafetyCache.cpp
            src/ChartHazardEvaluator.cpp
            src/ChartSafetyHost.cpp
            src/OceanPrewarmPolicy.cpp
            src/ReachabilityPrewarmPolicy.cpp
            src/ExternalPlanningProvider.cpp
            src/weather_routing_pi.cpp
            src/WeatherRouting.cpp
            src/SunCalculator.cpp
            src/TimeZoneDisplay.cpp
            src/ConfigurationDialog.cpp
            src/SettingsDialog.cpp
            src/PlotDialog.cpp
            src/BoatDialog.cpp
            src/StatisticsDialog.cpp
            src/ReportDialog.cpp
            src/FilterRoutesDialog.cpp
            src/ConfigurationBatchDialog.cpp
            src/EditPolarDialog.cpp
            src/AboutDialog.cpp
            src/ConstraintChecker.cpp
            src/ClimatologyThreadGuard.cpp
            src/Polar.cpp
            src/Boat.cpp
            src/RouteMap.cpp
            src/RouteMapOverlay.cpp
            src/RouteWaypointExtractor.cpp
            src/MultiLegRouteOutput.cpp
            src/RouteSimplifier.cpp
            src/StabilityRouteAdapter.cpp
            src/StabilityCorridorLifecycle.cpp
            src/RoutingTablePanel.cpp
            src/Utilities.cpp
            src/LineBufferOverlay.cpp
            src/PolygonRegion.cpp
            src/WeatherRoutingUI.cpp
            src/icons.cpp
            src/zuFile.cpp
            src/georef.cpp
            src/cutil.cpp
            src/GribRecord.cpp
            src/navobj_util.cpp
            src/WeatherDataProvider.cpp
            src/RoutePoint.cpp
            src/Position.cpp
            src/IsoRoute.cpp
            src/headless/HeadlessRouteRunner.cpp
            src/headless/RoutingScenarioJson.cpp
            src/engine/StabilityCorridorCalculator.cpp
            vendor/weather-routing-engine/src/ArrivalPlanner.cpp
            vendor/weather-routing-engine/src/Geometry.cpp
            vendor/weather-routing-engine/src/Providers.cpp
            vendor/weather-routing-engine/src/Polar.cpp
            vendor/weather-routing-engine/src/EnvironmentResolver.cpp
            vendor/weather-routing-engine/src/Engine.cpp
            vendor/weather-routing-engine/src/Validator.cpp
            vendor/weather-routing-engine/src/Ensemble.cpp
            src/engine/native/OpenCpnAdapter.cpp
)

SET (HDRS
            include/AppendOnlyCache.h
            include/ChartSafetyCache.h
            include/ChartHazardEvaluator.h
            include/ChartSafetyHost.h
            include/headless/HeadlessRouteMonitor.h
            include/OptionalChartSafetyApi.h
            include/OceanPrewarmPolicy.h
            include/ReachabilityPrewarmPolicy.h
            include/ExternalPlanningProvider.h
            include/ClimatologyThreadGuard.h
            include/weather_routing_pi.h
            include/WeatherRouting.h
            include/SunCalculator.h
            include/ConfigurationDialog.h
            include/SettingsDialog.h
            include/PlotDialog.h
            include/BoatDialog.h
            include/StatisticsDialog.h
            include/ReportDialog.h
            include/FilterRoutesDialog.h
            include/ConfigurationBatchDialog.h
            include/ConstraintChecker.h
            include/MultiLegRouteOutput.h
            include/RouteSimplifier.h
            include/StabilityRouteAdapter.h
            include/StabilityCorridorLifecycle.h
            include/EditPolarDialog.h
            include/AboutDialog.h
            include/Polar.h
            include/Boat.h
            include/RouteMap.h
            include/RouteMapOverlay.h
            include/DepartureScheduler.h
            include/RoutingQualityPolicy.h
            include/RoutingFootprint.h
            include/RouteWaypointExtractor.h
            include/RoutingTablePanel.h
            include/Utilities.h
            include/LineBufferOverlay.h
            include/PolygonRegion.h
            include/WeatherRoutingUI.h
            include/icons.h
            include/zuFile.h
            include/georef.h
            include/cutil.h
            include/GribRecord.h
            include/navobj_util.h
            include/WeatherDataProvider.h
            include/RoutePoint.h
            include/Position.h
            include/IsoRoute.h
            include/weather_routing_engine/RoutingDiagnostics.h
            include/weather_routing_engine/RoutingResult.h
            include/weather_routing_engine/RoutingScenario.h
            include/weather_routing_engine/StabilityCorridor.h
            vendor/weather-routing-engine/include/supercpn/weather_routing/Types.h
            vendor/weather-routing-engine/include/supercpn/weather_routing/ArrivalPlanner.h
            vendor/weather-routing-engine/include/supercpn/weather_routing/Providers.h
            vendor/weather-routing-engine/include/supercpn/weather_routing/Engine.h
            vendor/weather-routing-engine/src/RoutingInternal.h
            include/ModernNativeRoute.h
            src/headless/HeadlessRouteRunner.h
            src/headless/RoutingScenarioJson.h
)

add_definitions(-DPLUGIN_USE_SVG)

## ----- Section above - Add your project SET(SRCS and SET(HDRS  etc.-------##
## ----- If using JSON validation in plugin section below is needed --------##

# Do not need Json Schema Validation

## ----- If using JSON validation in plugin section above is needed --------##


##=============================================================
## Change as required to collect all the set( headers and SRCS
## This add_library  can be used in conditionals below.
##=============================================================

set(SRC ${SRCS} ${HDRS} )

##=============================================================
## Modify/configure 'Include' Directories below for the Plugin
##=============================================================

include_directories(BEFORE
  ${PROJECT_SOURCE_DIR}/vendor/weather-routing-engine/include
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src)

## Change target_sources to match your project needs.
##==============================================================

target_sources(${PACKAGE_NAME} PUBLIC ${SRC} )

# Installed packages resolve their assets through OpenCPN's plugin data path.
# An in-tree developer build has no installed xweather_routing_pi data
# directory, so give that build an explicit source-only fallback.
if(NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  target_compile_definitions(${PACKAGE_NAME} PRIVATE
    XWEATHER_ROUTING_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()

if(CMAKE_DL_LIBS)
  target_link_libraries(${PACKAGE_NAME} ${CMAKE_DL_LIBS})
endif()


if (NOT OCPN_FLATPAK_CONFIG)
  # Build environment not available when flatpak is being configured so
  # following statements will not work
  message(STATUS "${CMLOC}Adding target link libraries to ${PACKAGE_NAME}")

  if (WIN32)
    add_subdirectory(opencpn-libs/WindowsHeaders)
    target_link_libraries(${PACKAGE_NAME} windows::headers)
  endif (WIN32)

  if(NOT OCPN_IN_TREE_SOURCE_DIR)
    get_filename_component(OCPN_IN_TREE_SOURCE_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  endif()
  if (NOT XWEATHER_ROUTING_STANDALONE_API
      AND EXISTS "${OCPN_IN_TREE_SOURCE_DIR}/include/ocpn_plugin.h")
    add_library(OCPN_API_LOCAL INTERFACE)
    add_library(ocpn::api ALIAS OCPN_API_LOCAL)
    target_include_directories(OCPN_API_LOCAL INTERFACE
      "${OCPN_IN_TREE_SOURCE_DIR}/include")
  elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/api-${OCPN_API_VERSION_MINOR}/CMakeLists.txt")
    add_subdirectory(opencpn-libs/api-${OCPN_API_VERSION_MINOR})
    # The enhanced chart-safety service is optional and resolved dynamically.
    # Stock API headers intentionally do not declare its ABI-only structures,
    # so make the plugin-local declarations available to every target in this
    # directory, including the standalone regression tests.
    add_compile_definitions(
      XWEATHER_ROUTING_DECLARE_OPTIONAL_CHART_SAFETY_ABI=1)
  elseif (EXISTS "${CMAKE_SOURCE_DIR}/include/ocpn_plugin.h")
    add_library(OCPN_API_LOCAL INTERFACE)
    add_library(ocpn::api ALIAS OCPN_API_LOCAL)
    target_include_directories(OCPN_API_LOCAL INTERFACE "${CMAKE_SOURCE_DIR}/include")
  else ()
    message(FATAL_ERROR "Missing opencpn-libs/api-${OCPN_API_VERSION_MINOR}")
  endif ()
  target_link_libraries(${PACKAGE_NAME} ocpn::api)

  add_subdirectory(opencpn-libs/tinyxml)
  target_link_libraries(${PACKAGE_NAME} ocpn::tinyxml)

  add_subdirectory(opencpn-libs/odapi)
  target_link_libraries(${PACKAGE_NAME} ocpn::odapi)

  add_subdirectory(opencpn-libs/jsonlib)
  target_link_libraries(${PACKAGE_NAME} ocpn::jsonlib)

  add_subdirectory(opencpn-libs/plugin_dc)
  target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)

  add_subdirectory(opencpn-libs/pugixml)
  target_link_libraries(${PACKAGE_NAME} ocpn::pugixml)

  # @todo Why would this not be required on UNIX?  
  # @todo@ On macos the build fails without it, and macos is UNIX
  # @todo See https://cmake.org/cmake/help/latest/variable/UNIX.html
  
  add_subdirectory(opencpn-libs/bzip2)
  target_link_libraries(${PACKAGE_NAME} ocpn::bzip2)

  add_subdirectory(opencpn-libs/zlib)
  target_link_libraries(${PACKAGE_NAME} ocpn::zlib)
 
  add_subdirectory(opencpn-libs/libtess2)
  target_link_libraries(${PACKAGE_NAME} ocpn::libtess2)

endif(NOT OCPN_FLATPAK_CONFIG)

add_definitions(-DTIXML_USE_STL)

##
## ----- If using JSON validation section below is needed ---------------##
##

# Do not need Json Schema Validation


##------- Change below to match project requirements --------------------##

##------- Change below to match project requirements --------------

# Needed for android builds
if (QT_ANDROID)
  include_directories(BEFORE ${qt_android_include})
endif (QT_ANDROID)

## Needed for all builds Do not change - needed to build app -------------##
##===============================================================

include("PluginInstall")
include("PluginLocalization")
include("PluginPackage")

##================================================================
## ----- Do not change section above - needed to configure build process --##

message(STATUS "${CMLOC}include directories: ")
get_property(
  dirs
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  PROPERTY INCLUDE_DIRECTORIES
)
foreach (dir ${dirs})
  message(STATUS "${CMLOC}dir='${dir}'")
endforeach ()

# Add the test directory if testing is enabled
if(OCPN_BUILD_TEST)
    message(STATUS "Building with tests enabled")
    enable_testing()
    add_subdirectory(test)
endif()
