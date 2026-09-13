if(NOT DEFINED ROUTING_SOURCE OR NOT DEFINED TABLE_SOURCE OR
   NOT DEFINED TABLE_HEADER)
  message(FATAL_ERROR
    "ROUTING_SOURCE, TABLE_SOURCE, and TABLE_HEADER are required")
endif()

file(READ "${ROUTING_SOURCE}" routing_source)
file(READ "${TABLE_SOURCE}" table_source)
file(READ "${TABLE_HEADER}" table_header)

if(NOT table_header MATCHES
   "void SetRouteMap\\(RouteMapOverlay\\* routemap\\)")
  message(FATAL_ERROR
    "RoutingTablePanel must expose an explicit route lifetime setter")
endif()

string(FIND "${routing_source}"
       "void WeatherRouting::DeleteRouteMaps(" delete_start)
string(FIND "${routing_source}"
       "void WeatherRouting::SaveLastUsedConfigurationDefaults(" delete_end)
if(delete_start LESS 0 OR delete_end LESS_EQUAL delete_start)
  message(FATAL_ERROR "DeleteRouteMaps must remain identifiable")
endif()
math(EXPR delete_length "${delete_end} - ${delete_start}")
string(SUBSTRING "${routing_source}" ${delete_start} ${delete_length}
       delete_body)
string(FIND "${delete_body}"
       "m_RoutingTablePanel->SetRouteMap(nullptr)" detach_position)
string(FIND "${delete_body}"
       "m_RoutingTablePanel->GetRouteMap() == *it" per_route_position)
string(FIND "${delete_body}" "delete *writ" delete_position)
if(detach_position LESS 0 OR delete_position LESS 0 OR
   detach_position GREATER delete_position)
  message(FATAL_ERROR
    "The routing table must detach from a route before the route is deleted")
endif()
if(per_route_position LESS 0 OR per_route_position GREATER delete_position)
  message(FATAL_ERROR
    "Each route must be detached after synchronous selection changes")
endif()

string(FIND "${table_source}"
       "void RoutingTablePanel::PopulateTable()" populate_start)
string(FIND "${table_source}"
       "void RoutingTablePanel::UpdateTimeHighlight(" populate_end)
if(populate_start LESS 0 OR populate_end LESS_EQUAL populate_start)
  message(FATAL_ERROR "PopulateTable must remain identifiable")
endif()
math(EXPR populate_length "${populate_end} - ${populate_start}")
string(SUBSTRING "${table_source}" ${populate_start} ${populate_length}
       populate_body)
string(FIND "${populate_body}" "RouteMapIsManaged(m_RouteMap)"
       managed_position)
string(FIND "${populate_body}" "m_RouteMap->GetPlotData(false)"
       plot_position)
if(managed_position LESS 0 OR plot_position LESS 0 OR
   managed_position GREATER plot_position)
  message(FATAL_ERROR
    "PopulateTable must validate route ownership before dereferencing it")
endif()

string(FIND "${routing_source}" "void WeatherRouting::Render(" render_start)
string(FIND "${routing_source}"
       "void WeatherRouting::UpdateDisplaySettings(" render_end)
if(render_start LESS 0 OR render_end LESS_EQUAL render_start)
  message(FATAL_ERROR "WeatherRouting::Render must remain identifiable")
endif()
math(EXPR render_length "${render_end} - ${render_start}")
string(SUBSTRING "${routing_source}" ${render_start} ${render_length}
       render_body)
if(NOT render_body MATCHES "pane\\.IsOk\\(\\) && pane\\.IsShown\\(\\)")
  message(FATAL_ERROR
    "Chart rendering must not update a hidden routing table")
endif()
