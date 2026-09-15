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
foreach(case short coastal variable tidal island margin dateline high-latitude depth shallow missing-depth missing-wind wind-limit cancel invalid climatology tiny-budget low-memory land-endpoint ocean ocean6h prediction prediction-limit wave-limit missing-wave motor duration-limit)
  add_test(NAME quick-${case} COMMAND weather_routing_head_to_head quick ${case})
  set_tests_properties(quick-${case} PROPERTIES TIMEOUT 120)
endforeach()
