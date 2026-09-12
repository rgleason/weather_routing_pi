file(READ "${CONFIGURATION_SOURCE}" configuration_source)
file(READ "${ROUTING_SOURCE}" routing_source)

string(FIND "${configuration_source}"
       "ConfigurationDialog::ConfigurationDialog(" constructor_start)
string(FIND "${configuration_source}"
       "void ConfigurationDialog::RefreshTimeZoneControls(" refresh_start)
if(constructor_start LESS 0 OR refresh_start LESS_EQUAL constructor_start)
  message(FATAL_ERROR
    "ConfigurationDialog constructor and timezone refresh must remain identifiable")
endif()

math(EXPR constructor_length "${refresh_start} - ${constructor_start}")
string(SUBSTRING "${configuration_source}" ${constructor_start}
       ${constructor_length} constructor_body)
if(constructor_body MATCHES "m_SettingsDialog")
  message(FATAL_ERROR
    "ConfigurationDialog constructor must not access the later-constructed SettingsDialog")
endif()

string(FIND "${routing_source}" "m_SettingsDialog.LoadSettings();" load_start)
string(FIND "${routing_source}"
       "m_ConfigurationDialog.RefreshTimeZoneControls();" refresh_call)
if(load_start LESS 0 OR refresh_call LESS_EQUAL load_start)
  message(FATAL_ERROR
    "Timezone controls must synchronize only after persisted settings are loaded")
endif()
