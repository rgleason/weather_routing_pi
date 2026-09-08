cmake_minimum_required(VERSION 3.14)

project(weather_routing_pi_tests)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(WEATHER_ROUTING_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(WEATHER_ROUTING_ENGINE_DIR
    "${WEATHER_ROUTING_SOURCE_DIR}/vendor/weather-routing-engine")
message(STATUS "Building weather routing plugin tests")

enable_testing ()

message(STATUS "Downloading and building gtest from source (if required)")
# As per https://google.github.io/googletest/quickstart-cmake.html
include(FetchContent)
find_package(GTest 1.16 QUIET)
if(NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.16.0 # Released 2025-02-07
        GIT_SHALLOW TRUE
    )
    # For Windows: Prevent overriding the parent project's compiler/linker
    # settings.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

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

set(TEST_SRC
    AppendOnlyCache_tests.cpp
    ChartSafetyCache_tests.cpp
    ChartSafetyHost_tests.cpp
    ChartSafetyAtlas_tests.cpp
    ChartHazardEvaluator_tests.cpp
    ChartSafetyPolicy_tests.cpp
    ChartSafetyDefaults_tests.cpp
    HeadlessRouteMonitor_tests.cpp
    KeyedRequestCache_tests.cpp
    OceanPrewarmPolicy_tests.cpp
    ReachabilityPrewarmPolicy_tests.cpp
    RouteDisplayPolicy_tests.cpp
    Polar_tests.cpp
    PolygonRegion_tests.cpp
    Utilities_tests.cpp
    MultiLegRouteOutput_tests.cpp
    RouteSimplifier_tests.cpp
    StabilityCorridor_tests.cpp
    StabilityCorridorLifecycle_tests.cpp
    RoutingScenarioJson_tests.cpp
    DepartureScheduler_tests.cpp
    RoutingQualityPolicy_tests.cpp
    RoutingResourcePolicy_tests.cpp
    RoutingFootprint_tests.cpp
    ClimatologyThreadGuard_tests.cpp
    ConstraintTime_tests.cpp
    SunCalculator_tests.cpp
    TimeZoneDisplay_tests.cpp
    ModernNativeEngine_tests.cpp
    ArrivalPlanner_tests.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/AppendOnlyCache.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/ChartSafetyCache.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/ChartSafetyHost.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/ChartSafetyAtlas.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/ChartHazardEvaluator.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/georef.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/cutil.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/OceanPrewarmPolicy.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/ReachabilityPrewarmPolicy.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/Utilities.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/PolygonRegion.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/MultiLegRouteOutput.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/Polar.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/RouteSimplifier.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/engine/StabilityCorridorCalculator.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/StabilityCorridorLifecycle.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/headless/RoutingScenarioJson.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/ClimatologyThreadGuard.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/SunCalculator.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/TimeZoneDisplay.cpp
    ${WEATHER_ROUTING_ENGINE_DIR}/src/Geometry.cpp
    ${WEATHER_ROUTING_ENGINE_DIR}/src/Providers.cpp
    ${WEATHER_ROUTING_ENGINE_DIR}/src/Polar.cpp
    ${WEATHER_ROUTING_ENGINE_DIR}/src/EnvironmentResolver.cpp
    ${WEATHER_ROUTING_ENGINE_DIR}/src/ArrivalPlanner.cpp
    ${WEATHER_ROUTING_ENGINE_DIR}/src/Engine.cpp
    ${WEATHER_ROUTING_ENGINE_DIR}/src/Validator.cpp
    ${WEATHER_ROUTING_ENGINE_DIR}/src/Ensemble.cpp
    ${WEATHER_ROUTING_SOURCE_DIR}/src/zuFile.cpp
   )

   add_executable(weather_routing_pi_tests ${TEST_SRC})

   set(WEATHER_ROUTING_TEST_SANITIZER "" CACHE STRING
       "Optional sanitizer for Weather Routing tests: address, thread, or undefined")
   set_property(CACHE WEATHER_ROUTING_TEST_SANITIZER PROPERTY STRINGS
                "" address thread undefined)
   if (WEATHER_ROUTING_TEST_SANITIZER)
       target_compile_options(weather_routing_pi_tests PRIVATE
           -fno-omit-frame-pointer
           -fsanitize=${WEATHER_ROUTING_TEST_SANITIZER})
       target_link_options(weather_routing_pi_tests PRIVATE
           -fno-omit-frame-pointer
           -fsanitize=${WEATHER_ROUTING_TEST_SANITIZER})
   endif()

   # Uncomment include directories as needed - @todo tests should inherit this from main plugin
   target_include_directories(weather_routing_pi_tests
       PRIVATE
            ${WEATHER_ROUTING_SOURCE_DIR}/src
            ${WEATHER_ROUTING_SOURCE_DIR}/include
            ${WEATHER_ROUTING_ENGINE_DIR}/include
            ${WEATHER_ROUTING_ENGINE_DIR}/src
            ${CMAKE_SOURCE_DIR}/include
   #         ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/api-18/include
            ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/libtess2/include
            ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/tinyxml/include
            ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/odapi
            ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/jsonlib/include
   #         ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/plugin_dc/dc_utils/include
   #         ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/jsonlib/include
   #         ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/odapi
   #         ${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/pugixml
            ${GTEST_INCLUDE_DIRS}
            ${wxWidgets_INCLUDE_DIRS}
    )

   # Uncomment required libraries as needed - @todo tests should inherit this from main plugin
   target_link_libraries(weather_routing_pi_tests
       PRIVATE
           GTest::gtest_main
           ${wxWidgets_LIBRARIES}
           ocpn::api
           ocpn::libtess2
           ocpn::tinyxml
           # ocpn::dc-utils
           # ocpn::jsonlib
           # ocpn::api_wx32
           # ocpn::iso8211
           # ocpn::json-schema-validator
           # ocpn::wxsvg
           ocpn::bzip2
           ocpn::zlib
           ocpn::jsonlib
   )

   if(WIN32)
       # zlib1.lib is an import library. Keep its matching x86 DLL beside the
       # x86 test executable so GoogleTest discovery cannot select an
       # unrelated zlib1.dll from the executor PATH.
       add_custom_command(TARGET weather_routing_pi_tests POST_BUILD
           COMMAND ${CMAKE_COMMAND} -E copy_if_different
               "${WEATHER_ROUTING_SOURCE_DIR}/opencpn-libs/zlib/win/zlib1.dll"
               "$<TARGET_FILE_DIR:weather_routing_pi_tests>"
           COMMAND ${CMAKE_COMMAND} -E copy_if_different
               "$<TARGET_FILE:GTest::gtest>"
               "$<TARGET_FILE:GTest::gtest_main>"
               "$<TARGET_FILE_DIR:weather_routing_pi_tests>"
       )
   endif()

   # Workaround as per https://discourse.cmake.org/t/how-to-get-an-lc-rpath-and-rpath-prefix-on-a-dylib-on-macos/5540/5
   # TODO: Quinton: Make this robust on non-Mac platforms.
   set_target_properties(weather_routing_pi_tests PROPERTIES
       # The test executable lives three directories below the host build
       # library directory.  Use an origin-relative build rpath so CTest and
       # gtest discovery work without a caller-specific LD_LIBRARY_PATH.
       BUILD_WITH_INSTALL_RPATH FALSE
       SKIP_BUILD_RPATH FALSE
       BUILD_RPATH "${CMAKE_BINARY_DIR}/lib"
       INSTALL_RPATH "../lib"
       )

# gtest_discover_tests below registers every case with CTest.  Do not also
# register the executable as one aggregate test: that reruns the complete
# suite, and the duplicate process can exceed CTest's default timeout on the
# supported 32-bit Windows executor even though every discovered case passes.
add_test(
    NAME weather_routing_time_zone_lifecycle_contract
    COMMAND ${CMAKE_COMMAND}
        -DCONFIGURATION_SOURCE=${WEATHER_ROUTING_SOURCE_DIR}/src/ConfigurationDialog.cpp
        -DROUTING_SOURCE=${WEATHER_ROUTING_SOURCE_DIR}/src/WeatherRouting.cpp
        -P ${CMAKE_CURRENT_LIST_DIR}/ConfigurationTimeZoneLifecycleContract.cmake
)
add_test(
    NAME weather_routing_headless_route_lifecycle_contract
    COMMAND ${CMAKE_COMMAND}
        -DROUTING_SOURCE=${WEATHER_ROUTING_SOURCE_DIR}/src/WeatherRouting.cpp
        -P ${CMAKE_CURRENT_LIST_DIR}/HeadlessRouteLifecycleContract.cmake
)
add_test(
    NAME weather_routing_table_lifecycle_contract
    COMMAND ${CMAKE_COMMAND}
        -DROUTING_SOURCE=${WEATHER_ROUTING_SOURCE_DIR}/src/WeatherRouting.cpp
        -DTABLE_SOURCE=${WEATHER_ROUTING_SOURCE_DIR}/src/RoutingTablePanel.cpp
        -DTABLE_HEADER=${WEATHER_ROUTING_SOURCE_DIR}/include/RoutingTablePanel.h
        -P ${CMAKE_CURRENT_LIST_DIR}/RoutingTableLifecycleContract.cmake
)
add_test(
    NAME weather_routing_msvc_cplusplus_contract
    COMMAND ${CMAKE_COMMAND}
        -DROOT_CMAKE=${WEATHER_ROUTING_SOURCE_DIR}/CMakeLists.txt
        -P ${CMAKE_CURRENT_LIST_DIR}/MsvcCplusplusContract.cmake
)

target_compile_definitions(weather_routing_pi_tests
    PUBLIC
        USE_MOCK_DEFS CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
        TESTDATADIR="${CMAKE_CURRENT_LIST_DIR}/testdata"
        WEATHER_ROUTING_SOURCE_DIR="${WEATHER_ROUTING_SOURCE_DIR}"
)

include(GoogleTest)
gtest_discover_tests(weather_routing_pi_tests DISCOVERY_MODE PRE_TEST)
