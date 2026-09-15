if(NOT DEFINED ROUTING_SOURCE)
  message(FATAL_ERROR "ROUTING_SOURCE is required")
endif()

file(READ "${ROUTING_SOURCE}" routing_source)
string(FIND "${routing_source}"
       "void WeatherRouting::RunHeadlessRouteTestFromEnv()" runner_start)
string(FIND "${routing_source}"
       "void WeatherRouting::CursorRouteChanged()" runner_end)
if(runner_start LESS 0 OR runner_end LESS_EQUAL runner_start)
  message(FATAL_ERROR "Headless route runner must remain identifiable")
endif()

math(EXPR runner_length "${runner_end} - ${runner_start}")
string(SUBSTRING "${routing_source}" ${runner_start} ${runner_length}
       runner_body)
if(runner_body MATCHES "wxYield|wxMilliSleep")
  message(FATAL_ERROR
    "Headless route runner must not drive a nested wx event loop")
endif()
if(NOT runner_body MATCHES "m_tHeadlessRouteTest.Start")
  message(FATAL_ERROR
    "Headless route runner must return control to its asynchronous monitor")
endif()
