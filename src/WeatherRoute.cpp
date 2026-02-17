//==============================================================
// NEW UI
//==============================================================

wxString WeatherRoute::StartTypeString() const {
  switch (start_type) {
    case START_MANUAL:
      return _("Manual");
    case START_BOAT:
      return _("Boat");
    case START_POSITION:
      return _("Position");
    default:
      return _("Unknown");
  }
}

wxString WeatherRoute::StartString() const {
  if (start_type == START_POSITION && start_position)
    return start_position->name;

  if (start_type == START_BOAT) return _("Boat Position");

  // Manual lat/lon
  return wxString::Format(_T("%.5f, %.5f"), start_lat, start_lon);
}

wxString WeatherRoute::StartTimeString() const {
  if (!start_time.IsValid()) return _("?");

  return start_time.FormatISOCombined(' ');
}

wxString WeatherRoute::EndString() const {
  if (end_position) return end_position->name;

  // Manual lat/lon
  return wxString::Format(_T("%.5f, %.5f"), end_lat, end_lon);
}

wxString WeatherRoute::EndTimeString() const {
  if (!end_time.IsValid()) return _("?");

  return end_time.FormatISOCombined(' ');
}

wxString WeatherRoute::DurationString() const {
  if (!start_time.IsValid() || !end_time.IsValid()) return _("?");

  wxTimeSpan span = end_time - start_time;

  long hours = span.GetHours();
  long minutes = span.GetMinutes() % 60;

  return wxString::Format(_T("%ldh %02ldm"), hours, minutes);
}

wxString WeatherRoute::DistanceString() const {
  if (distance_nm <= 0) return _("?");

  return wxString::Format(_T("%.1f nm"), distance_nm);
}

wxString WeatherRoute::StateString() const {
  switch (state) {
    case STATE_NOT_COMPUTED:
      return _("Not Computed");
    case STATE_READY:
      return _("Ready");
    case STATE_RUNNING:
      return _("Running");
    case STATE_COMPLETE:
      return _("Complete");
    case STATE_FAILED:
      return _("Failed");
    default:
      return _("Unknown");
  }
}
