cmake_minimum_required(VERSION 3.15.0)

project(weather_routing_pi_tests)
set(CMAKE_CXX_STANDARD 17)
message(STATUS "Building weather routing plugin tests")
# make sure to use a Debug build, for coverage
set(CMAKE_BUILD_TYPE "Debug")

include(CTest)
include(GoogleTest)

message(STATUS "Downloading and building gtest from source (if required)")
# As per https://google.github.io/googletest/quickstart-cmake.html
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0 # Released 2025-02-07
    FIND_PACKAGE_ARGS NAMES googletest VERSION 1.16.0  # Only download and build if it's not already available via find_package
  )
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Find required packages
find_package(wxWidgets COMPONENTS 
core 
base 
net
xml
html
adv
aui
REQUIRED)

set(SRC
    # Test source files, in alphabetical order
    IsoRoute_tests.cpp
    Polar_tests.cpp
    PolygonRegion_tests.cpp
    Utilities_tests.cpp

    #Mock source files, in alphabetical order
    mock_plugin_api.cpp
    mock_plugin_impl.c # For functions with "C" linkage
    mock_plugin_impl.cpp # For functions with C++ linkage.

    # Plugin files, in alphabetical order
    ${CMAKE_SOURCE_DIR}/src/AboutDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/Boat.cpp
    ${CMAKE_SOURCE_DIR}/src/BoatDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ConfigurationBatchDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ConfigurationDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ConstraintChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/EditPolarDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/FilterRoutesDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/GribRecord.cpp
    ${CMAKE_SOURCE_DIR}/src/georef.cpp
    ${CMAKE_SOURCE_DIR}/src/icons.cpp
    ${CMAKE_SOURCE_DIR}/src/LineBufferOverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/IsoRoute.cpp
    ${CMAKE_SOURCE_DIR}/src/navobj_util.cpp
    ${CMAKE_SOURCE_DIR}/src/PlotDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/PolygonRegion.cpp
    ${CMAKE_SOURCE_DIR}/src/Polar.cpp
    ${CMAKE_SOURCE_DIR}/src/Position.cpp
    ${CMAKE_SOURCE_DIR}/src/ReportDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/RoutingTablePanel.cpp
    ${CMAKE_SOURCE_DIR}/src/RouteMap.cpp
    ${CMAKE_SOURCE_DIR}/src/RouteMapOverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/RoutePoint.cpp
    ${CMAKE_SOURCE_DIR}/src/RouteSimplifier.cpp
    ${CMAKE_SOURCE_DIR}/src/SettingsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/StatisticsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/SunCalculator.cpp
    ${CMAKE_SOURCE_DIR}/src/Utilities.cpp
    ${CMAKE_SOURCE_DIR}/src/WeatherDataProvider.cpp
    ${CMAKE_SOURCE_DIR}/src/WeatherRouting.cpp
    ${CMAKE_SOURCE_DIR}/src/WeatherRoutingUI.cpp
    ${CMAKE_SOURCE_DIR}/src/weather_routing_pi.cpp
    ${CMAKE_SOURCE_DIR}/src/zuFile.cpp
)
add_executable(${PROJECT_NAME} ${SRC})

# Compile and link with code coverage (only for GCC or Clang)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -coverage)
    target_link_options(${PROJECT_NAME} PRIVATE -coverage)
endif()

# Uncomment include directories as needed - @todo tests should inherit this from main plugin
target_include_directories(${PROJECT_NAME} 
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/opencpn-libs/${PKG_API_LIB}/include
        ${CMAKE_SOURCE_DIR}/opencpn-libs/libtess2/include
        ${CMAKE_SOURCE_DIR}/opencpn-libs/tinyxml/include
        ${CMAKE_SOURCE_DIR}/opencpn-libs/plugin_dc/dc_utils/include
        ${CMAKE_SOURCE_DIR}/opencpn-libs/jsonlib/include
        ${CMAKE_SOURCE_DIR}/opencpn-libs/odapi
        ${CMAKE_SOURCE_DIR}/opencpn-libs/pugixml
        ${GTEST_INCLUDE_DIRS}
        ${GMOCK_INCLUDE_DIRS}
        ${wxWidgets_INCLUDE_DIRS}
)

# Uncomment required libraries as needed - @todo tests should inherit this from main plugin
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
        GTest::gtest_main
        GTest::gmock_main
        ${wxWidgets_LIBRARIES}
        ocpn::api
        ocpn::libtess2
        ocpn::pugixml
        ocpn::tinyxml
        ocpn::dc-utils
        ocpn::jsonlib
        # ocpn::api_wx32
        # ocpn::iso8211
        # ocpn::json-schema-validator
        # ocpn::wxsvg
        ocpn::bzip2
        ${ZLIB_LIBRARIES}
)

# Workaround as per https://discourse.cmake.org/t/how-to-get-an-lc-rpath-and-rpath-prefix-on-a-dylib-on-macos/5540/5
# TODO: Quinton: Make this robust on non-Mac platforms.
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "../lib"
    INSTALL_RPATH "../lib"    
    )

    target_compile_definitions(weather_routing_pi_tests
    PUBLIC
        USE_MOCK_DEFS CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
        TESTDATADIR="${CMAKE_CURRENT_LIST_DIR}/testdata"
        UNIT_TESTS
)

gtest_discover_tests(${PROJECT_NAME})

# Create UNIT_UNDER_TEST_OBJECT_DIR variable
set(OBJECT_DIR ${CMAKE_BINARY_DIR}/test/CMakeFiles/${PROJECT_NAME}.dir)
set(UNIT_UNDER_TEST_OBJECT_DIR ${OBJECT_DIR}/__/src)
message("-- Object files under test are assumed to be under: ${UNIT_UNDER_TEST_OBJECT_DIR}")

# Create the coverage target. Run coverage tests with 'make coverage'
add_custom_target(coverage
    COMMAND ${CMAKE_MAKE_PROGRAM} test
)

# Add a custom command to the coverage target that runs gcov, only for src files
add_custom_command(TARGET coverage
    COMMAND gcov -n -s ${CMAKE_SOURCE_DIR}/src -r ${UNIT_UNDER_TEST_OBJECT_DIR}/*.o 
)