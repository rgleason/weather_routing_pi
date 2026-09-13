cmake_minimum_required(VERSION 3.20)

project(weather_routing_engine
  VERSION 1.1.0
  DESCRIPTION "Deterministic, chart-aware weather-routing engine"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(WEATHER_ROUTING_ENGINE_BUILD_TESTS
  "Build the standalone engine contract tests" ON)
option(WEATHER_ROUTING_ENGINE_ENABLE_INSTALL
  "Generate install and CMake package rules" ON)

add_library(weather_routing_engine STATIC
  include/supercpn/weather_routing/ArrivalPlanner.h
  include/supercpn/weather_routing/Engine.h
  include/supercpn/weather_routing/Providers.h
  include/supercpn/weather_routing/ResourcePolicy.h
  include/supercpn/weather_routing/Types.h
  src/ArrivalPlanner.cpp
  src/Engine.cpp
  src/Ensemble.cpp
  src/EnvironmentResolver.cpp
  src/Geometry.cpp
  src/Polar.cpp
  src/Providers.cpp
  src/RoutingInternal.h
  src/Validator.cpp)
add_library(weather_routing_engine::weather_routing_engine
  ALIAS weather_routing_engine)

target_compile_features(weather_routing_engine PUBLIC cxx_std_20)
target_include_directories(weather_routing_engine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(weather_routing_engine PRIVATE /W4 /permissive-)
else()
  target_compile_options(weather_routing_engine PRIVATE
    -Wall -Wextra -Wpedantic)
endif()

set_target_properties(weather_routing_engine PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME weather_routing_engine)

if(WEATHER_ROUTING_ENGINE_ENABLE_INSTALL)
  install(TARGETS weather_routing_engine
    EXPORT weather_routing_engineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT weather_routing_engineTargets
    FILE weather_routing_engineTargets.cmake
    NAMESPACE weather_routing_engine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/weather_routing_engine)

  configure_package_config_file(
    cmake/weather_routing_engineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/weather_routing_engineConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/weather_routing_engine)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/weather_routing_engineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/weather_routing_engineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/weather_routing_engineConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/weather_routing_engine)
endif()

if(WEATHER_ROUTING_ENGINE_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
