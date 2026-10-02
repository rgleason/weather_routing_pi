add_executable(weather_routing_engine_contract_tests
  ContractTests.cpp)
target_link_libraries(weather_routing_engine_contract_tests PRIVATE
  weather_routing_engine::weather_routing_engine)
target_compile_features(weather_routing_engine_contract_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(weather_routing_engine_contract_tests PRIVATE /W4)
else()
  target_compile_options(weather_routing_engine_contract_tests PRIVATE
    -Wall -Wextra -Wpedantic)
endif()

add_test(NAME weather-routing-engine-contract-tests
  COMMAND weather_routing_engine_contract_tests)

add_executable(weather_routing_head_to_head QuickHeadToHead.cpp)
target_link_libraries(weather_routing_head_to_head PRIVATE weather_routing_engine::quick_weather_routing_engine)
target_link_libraries(weather_routing_head_to_head PRIVATE
  weather_routing_engine::alternative_weather_routing_engine)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../original-routing-engine/CMakeLists.txt")
  if(NOT TARGET hardened_original_engine)
    set(ORIGINAL_ROUTING_BUILD_TESTS OFF)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../original-routing-engine"
                     "${CMAKE_CURRENT_BINARY_DIR}/original-routing-engine")
  endif()
  target_link_libraries(weather_routing_head_to_head PRIVATE
    hardened_original_engine)
  target_compile_definitions(weather_routing_head_to_head PRIVATE
    HAS_ORIGINAL_ENGINE=1)
endif()
foreach(case short coastal variable tidal island margin dateline high-latitude depth shallow missing-depth missing-wind wind-limit cancel invalid climatology tiny-budget low-memory land-endpoint ocean ocean6h prediction prediction-limit wave-limit missing-wave motor duration-limit)
  add_test(NAME quick-${case} COMMAND weather_routing_head_to_head quick ${case})
  set_tests_properties(quick-${case} PROPERTIES TIMEOUT 120)
endforeach()
foreach(case short coastal variable tidal island dateline high-latitude depth shallow missing-depth missing-wind wind-limit cancel invalid ocean6h prediction prediction-limit motor duration-limit tiny-budget weather-budget)
  add_test(NAME alternative-${case} COMMAND weather_routing_head_to_head alternative ${case})
  set_tests_properties(alternative-${case} PROPERTIES TIMEOUT 120)
endforeach()
