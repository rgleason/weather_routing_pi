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
