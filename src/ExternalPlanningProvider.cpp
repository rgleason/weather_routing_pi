/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 or later.
 ***************************************************************************/

#include "ExternalPlanningProvider.h"

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <thread>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

#include <json/json.h>
#include <wx/app.h>
#include <wx/datetime.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>

#include "ChartSafetyHost.h"
#include "weather_routing_pi.h"

namespace {
using RegisterFunction = bool (*)(const char*, const PlugInPlanningProviderV1*);
using UnregisterFunction = bool (*)(const char*);

template <typename T>
T Resolve(const char* name) {
#ifdef _WIN32
  return reinterpret_cast<T>(GetProcAddress(GetModuleHandle(nullptr), name));
#else
  return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
#endif
}

bool PlainResourceId(const std::string& value) {
  return value.find('/') == std::string::npos &&
         value.find('\\') == std::string::npos &&
         value.find("..") == std::string::npos;
}

std::string TimeUtc(Json::Int64 epoch_seconds) {
  wxDateTime value(static_cast<time_t>(epoch_seconds));
  return std::string((value.ToUTC().FormatISOCombined('T') + "Z").ToUTF8());
}
}  // namespace

ExternalPlanningProvider::ExternalPlanningProvider(weather_routing_pi& plugin)
    : plugin_(plugin) {}

ExternalPlanningProvider::~ExternalPlanningProvider() { Unregister(); }

bool ExternalPlanningProvider::RegisterIfSupported() {
  if (registered_) return true;
  const auto register_provider =
      Resolve<RegisterFunction>("PlugIn_RegisterPlanningProviderV1");
  if (!register_provider) {
    wxLogMessage(
        "WeatherRouting: host has no external planning-provider service; "
        "continuing with normal plugin features");
    return false;
  }
  PlugInPlanningProviderV1 provider{};
  provider.struct_size = sizeof(provider);
  provider.capability = "route-planning.chart-weather.v1";
  provider.display_name = "WeatherRouting";
  provider.provider_context = this;
  provider.run = &ExternalPlanningProvider::Run;
  registered_ =
      register_provider(plugin_.GetCommonName().ToUTF8().data(), &provider);
  if (!registered_)
    wxLogWarning("WeatherRouting: external planning registration failed");
  return registered_;
}

bool ExternalPlanningProvider::Unregister() {
  if (!registered_) return true;
  const auto unregister_provider =
      Resolve<UnregisterFunction>("PlugIn_UnregisterPlanningProvidersV1");
  if (!unregister_provider) {
    registered_ = false;
    return true;
  }
  if (!unregister_provider(plugin_.GetCommonName().ToUTF8().data()))
    return false;
  registered_ = false;
  return true;
}

int ExternalPlanningProvider::Run(void* context, const char* request_json,
                                  PlugInPlanningCancelledV1 is_cancelled,
                                  void* cancellation_context,
                                  PlugInPlanningProgressV1 report_progress,
                                  void* progress_context,
                                  const char** result_json,
                                  const char** error_code,
                                  const char** error_message) {
  return static_cast<ExternalPlanningProvider*>(context)->RunRequest(
      request_json, is_cancelled, cancellation_context, report_progress,
      progress_context, result_json, error_code, error_message);
}

int ExternalPlanningProvider::Fail(const std::string& code,
                                   const std::string& message,
                                   const char** error_code,
                                   const char** error_message) {
  error_code_ = code;
  error_message_ = message;
  if (error_code) *error_code = error_code_.c_str();
  if (error_message) *error_message = error_message_.c_str();
  return 0;
}

int ExternalPlanningProvider::RunRequest(
    const char* request_json, PlugInPlanningCancelledV1 is_cancelled,
    void* cancellation_context, PlugInPlanningProgressV1 report_progress,
    void* progress_context, const char** result_json, const char** error_code,
    const char** error_message) {
  std::unique_lock<std::mutex> single_run(run_mutex_, std::try_to_lock);
  if (!single_run.owns_lock())
    return Fail("provider_busy",
                "WeatherRouting accepts one resident job at a time",
                error_code, error_message);

  cancellation_requested_.store(false, std::memory_order_relaxed);
  weather_routing::chart_safety_host::SetPrewarmCancellationFlag(
      &cancellation_requested_);
  struct CancellationFlagGuard {
    ~CancellationFlagGuard() {
      weather_routing::chart_safety_host::SetPrewarmCancellationFlag(nullptr);
    }
  } cancellation_flag_guard;

  Json::Value request;
  Json::Reader reader;
  if (!request_json || !reader.parse(request_json, request, false) ||
      !request.isObject())
    return Fail("invalid_request", "Invalid provider request JSON", error_code,
                error_message);
  const Json::Value& start = request["start"];
  const Json::Value& destination = request["destination"];
  if (!start.isObject() || !destination.isObject() ||
      !start["latitudeDegrees"].isNumeric() ||
      !start["longitudeDegrees"].isNumeric() ||
      !destination["latitudeDegrees"].isNumeric() ||
      !destination["longitudeDegrees"].isNumeric())
    return Fail("invalid_request", "Start and destination are required",
                error_code, error_message);

  const std::string weather =
      request.get("weatherDatasetIdentity", "active").asString();
  const std::string currents =
      request.get("currentDatasetIdentity", "active").asString();
  if (!weather.empty() && weather != "active")
    return Fail("unknown_weather_dataset",
                "Preview B currently supports the active xGRIB dataset only",
                error_code, error_message);
  if (!currents.empty() && currents != "active")
    return Fail("unknown_current_dataset",
                "Preview B currently supports the active current dataset only",
                error_code, error_message);

  const std::string polar = request.get("polarIdentity", "").asString();
  if (!polar.empty() && !PlainResourceId(polar))
    return Fail("invalid_polar_identity",
                "polarIdentity must be a discovered file name, not a path",
                error_code, error_message);

  Json::Value scenario;
  scenario["schemaVersion"] = 1;
  scenario["name"] = "OpenCPN external-control WeatherRouting job";
  scenario["start"]["name"] = "Start";
  scenario["start"]["lat"] = start["latitudeDegrees"];
  scenario["start"]["lon"] = start["longitudeDegrees"];
  scenario["end"]["name"] = "Destination";
  scenario["end"]["lat"] = destination["latitudeDegrees"];
  scenario["end"]["lon"] = destination["longitudeDegrees"];
  if (request["departureEpochSeconds"].isInt64() ||
      request["departureEpochSeconds"].isUInt64())
    scenario["startTime"] = TimeUtc(request["departureEpochSeconds"].asInt64());

  const int before = request.get("departureWindowBeforeMinutes", 0).asInt();
  const int after = request.get("departureWindowAfterMinutes", 0).asInt();
  scenario["departureOptimization"]["enabled"] = before > 0 || after > 0;
  scenario["departureOptimization"]["beforeMinutes"] = before;
  scenario["departureOptimization"]["afterMinutes"] = after;
  scenario["departureOptimization"]["stepMinutes"] =
      request.get("departureStepMinutes", 60).asInt();
  scenario["departureOptimization"]["concurrentRoutes"] =
      request.get("concurrentRoutes", 1).asInt();
  scenario["environment"]["useGrib"] = true;
  scenario["environment"]["useCurrents"] = !currents.empty();
  scenario["environment"]["allowClimatologyFallback"] =
      request.get("allowClimatologyFallback", false).asBool();
  if (!polar.empty()) {
    const wxString boat = weather_routing_pi::StandardPath() + "boats" +
                          wxFileName::GetPathSeparator() +
                          wxString::FromUTF8(polar);
    if (!wxFileExists(boat))
      return Fail("polar_not_found", "Requested polar is not installed",
                  error_code, error_message);
    scenario["route"]["boatFile"] = std::string(boat.ToUTF8());
  }
  scenario["route"]["routingEffortPercent"] =
      request.get("routingEffortPercent", 100).asInt();
  scenario["safety"]["mode"] = "chart";
  scenario["safety"]["enforce"] = true;
  scenario["safety"]["minimumDepthM"] =
      request.get("minimumDepthMeters", 0.0).asDouble();
  scenario["safety"]["landMarginNm"] =
      request.get("landMarginNauticalMiles", 0.0).asDouble();
  scenario["safety"]["persistentCertifiedCacheEnabled"] = true;
  scenario["reverseReachability"]["enabled"] = true;
  scenario["reverseReachability"]["searchBackIsochrones"] = 6;
  scenario["reverseReachability"]["horizonHours"] = 0;

  const wxString scenario_path =
      wxFileName::CreateTempFileName("xwr-provider-scenario-");
  const wxString output_path =
      wxFileName::CreateTempFileName("xwr-provider-result-");
  {
    std::ofstream output(scenario_path.ToStdString(),
                         std::ios::out | std::ios::trunc);
    output << Json::FastWriter().write(scenario);
    if (!output.good()) {
      wxRemoveFile(scenario_path);
      wxRemoveFile(output_path);
      return Fail("scenario_write_failed", "Could not create scenario file",
                  error_code, error_message);
    }
  }

  struct StartState {
    std::mutex mutex;
    std::condition_variable changed;
    bool complete{false};
    bool entered{false};
    bool started{false};
    bool abandoned{false};
  };
  auto start_state = std::make_shared<StartState>();
  if (!wxTheApp) {
    wxRemoveFile(scenario_path);
    wxRemoveFile(output_path);
    return Fail("not_ready", "OpenCPN application executor is unavailable",
                error_code, error_message);
  }
  const auto clear_scenario = [this] {
    struct ClearState {
      std::mutex mutex;
      std::condition_variable changed;
      bool complete{false};
    };
    auto state = std::make_shared<ClearState>();
    wxTheApp->CallAfter([this, state] {
      plugin_.ClearExternalPlanningScenario();
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->complete = true;
      }
      state->changed.notify_all();
    });
    std::unique_lock<std::mutex> lock(state->mutex);
    state->changed.wait(lock, [&] { return state->complete; });
  };
  wxTheApp->CallAfter([this, start_state, scenario_path, output_path] {
    {
      std::lock_guard<std::mutex> lock(start_state->mutex);
      if (start_state->abandoned) {
        start_state->complete = true;
        start_state->changed.notify_all();
        return;
      }
      start_state->entered = true;
      start_state->changed.notify_all();
    }
    const bool started = plugin_.StartExternalPlanningScenario(
        scenario_path, output_path, 24L * 60L * 60L * 1000L);
    if (cancellation_requested_.load(std::memory_order_relaxed))
      plugin_.CancelExternalPlanningScenario();
    {
      std::lock_guard<std::mutex> lock(start_state->mutex);
      start_state->started = started;
      start_state->complete = true;
    }
    start_state->changed.notify_all();
  });
  {
    std::unique_lock<std::mutex> lock(start_state->mutex);
    const auto admission_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!start_state->complete) {
      if (is_cancelled && is_cancelled(cancellation_context))
        cancellation_requested_.store(true, std::memory_order_relaxed);
      if (!start_state->entered &&
          std::chrono::steady_clock::now() >= admission_deadline) {
        start_state->abandoned = true;
        break;
      }
      start_state->changed.wait_for(lock, std::chrono::milliseconds(100));
    }
    if (start_state->abandoned) {
      wxRemoveFile(scenario_path);
      wxRemoveFile(output_path);
      return Fail("owner_thread_timeout",
                  "OpenCPN did not admit the resident route job", error_code,
                  error_message);
    }
    if (cancellation_requested_.load(std::memory_order_relaxed)) {
      lock.unlock();
      clear_scenario();
      wxRemoveFile(scenario_path);
      wxRemoveFile(output_path);
      return Fail("cancelled", "WeatherRouting job was cancelled",
                  error_code, error_message);
    }
    if (!start_state->started) {
      wxRemoveFile(scenario_path);
      wxRemoveFile(output_path);
      return Fail("provider_busy", "WeatherRouting is already calculating",
                  error_code, error_message);
    }
  }

  if (report_progress) report_progress(progress_context, 0.02);
  Json::Value result;
  bool cancellation_sent = false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::hours(24);
  while (std::chrono::steady_clock::now() < deadline) {
    if (is_cancelled && is_cancelled(cancellation_context) &&
        !cancellation_sent) {
      cancellation_sent = true;
      wxTheApp->CallAfter([this] { plugin_.CancelExternalPlanningScenario(); });
    }
    std::ifstream input(output_path.ToStdString());
    Json::Value candidate;
    if (input.good() && reader.parse(input, candidate, false) &&
        candidate.isObject()) {
      const std::string status = candidate.get("status", "").asString();
      if (!status.empty() && status != "running") {
        result = std::move(candidate);
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  clear_scenario();
  wxRemoveFile(scenario_path);
  wxRemoveFile(output_path);
  if (cancellation_sent)
    return Fail("cancelled", "WeatherRouting job was cancelled", error_code,
                error_message);
  if (!result.isObject())
    return Fail("provider_timeout", "WeatherRouting job exceeded 24 hours",
                error_code, error_message);
  result_ = Json::FastWriter().write(result);
  if (result_json) *result_json = result_.c_str();
  if (report_progress) report_progress(progress_context, 1.0);
  return 1;
}
