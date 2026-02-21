
//==============================================================
// WeatherRoute Constructor
//==============================================================

WeatherRoute::WeatherRoute() { ... }



//==============================================================
// Structured Error Helpers
//==============================================================

void WeatherRoute::ClearError() { lastError = RouteError(); }

void WeatherRoute::SetError(int code, const wxString& user,
                            const wxString& detail, const wxString& stage) {
  lastError.hasError = true;
  lastError.errorCode = code;
  lastError.userMessage = user;
  lastError.detailMessage = detail;
  lastError.failureStage = stage;
}


//==============================================================
//ComputeStateString()
//==============================================================

wxString WeatherRoute::ComputeStateString() const {
  if (lastError.hasError && !lastError.userMessage.IsEmpty())
    return lastError.userMessage;

  switch (state) {
    case RouteState::Idle:
      return _("Idle");
    case RouteState::Computing:
      return _("Computing route...");
    case RouteState::Completed:
      return _("Route completed");
    case RouteState::Failed:
      return _("Route failed");
    case RouteState::Aborted:
      return _("Routing aborted");
    case RouteState::InvalidInput:
      return _("Invalid input");
    default:
      return _("Unknown state");
  }
}

//==============================================================
// Update()
//==============================================================

RouteState WeatherRoute::Update(bool stateOnly) {
  ClearError();

  // 1. INPUT VALIDATION
  if (!HasValidEndpoints()) {
    SetError(ERR_INVALID_START, "Invalid start or destination point.",
             "Routing cannot begin because the start or end point is missing, "
             "invalid, or outside valid chart bounds.",
             "Input validation");
    state = RouteState::InvalidInput;
    return state;
  }

  if (!m_Grib) {
    SetError(ERR_NO_GRIB, "No GRIB data available.",
             "WeatherRouting requires GRIB data covering the route region.",
             "GRIB");
    state = RouteState::Failed;
    return state;
  }

  if (!m_Grib->Covers(start_lat, start_lon) ||
      !m_Grib->Covers(end_lat, end_lon)) {
    SetError(ERR_GRIB_COVERAGE, "GRIB does not cover the route.",
             "The GRIB file does not include the region between the start and "
             "destination.",
             "GRIB");
    state = RouteState::Failed;
    return state;
  }

  if (!m_Polar) {
    SetError(ERR_POLAR_NONE, "No boat polar selected.",
             "A valid polar file is required to compute boat speeds.", "Polar");
    state = RouteState::Failed;
    return state;
  }

  if (!m_Polar->IsValid()) {
    SetError(ERR_POLAR_INVALID, "Boat polar file is invalid.",
             "The polar file could not be parsed or contains invalid values.",
             "Polar");
    state = RouteState::Failed;
    return state;
  }

  
  // 2. BEGIN COMPUTATION
  state = RouteState::Computing;

  if (stateOnly) return state;

    // 3. COMPUTE ISOCHRONES
    if (!ComputeIsochrones()) {
    switch (lastError.errorCode) {
      case ERR_ABORTED:
        state = RouteState::Aborted;
        return state;

      case ERR_INVALID_START:
      case ERR_INVALID_END:
        state = RouteState::InvalidInput;
        return state;

      default:
        state = RouteState::Failed;
        return state;
    }
  }

  // 4. BUILD ROUTE
  if (!BuildRoute()) {
    if (lastError.errorCode == ERR_ABORTED) {
      state = RouteState::Aborted;
      return state;
    }

    state = RouteState::Failed;
    return state;
  }

  // 5. SUCCESS
  state = RouteState::Completed;
  return state;
}
//==============================================================
// PRIVATE COMPUTATION METHODS
//==============================================================



bool WeatherRoute::HasValidEndpoints() const { ... }
bool WeatherRoute::ComputeIsochrones() { ... }
bool WeatherRoute::PropagateNode(const IsoRoute& src, double speed,
                                 IsoRoute& dst) {
  ...
}
bool WeatherRoute::BuildRoute(){...}

//==============================================================
// NEW UI (UI helper methods)
//==============================================================

wxString WeatherRoute::StartTypeString() const {
  switch (start_type) {
    case START_MANUAL: return _("Manual");
    case START_BOAT:   return _("Boat");
    case START_POSITION: return _("Position");
    default:  return _("Unknown");
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
