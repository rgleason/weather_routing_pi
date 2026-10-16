// SPDX-License-Identifier: GPL-3.0-or-later
#include "Fixtures.h"
#include "Instrumentation.h"
#include "PolarFile.h"
#ifdef ATLAS_HAS_ECCODES
#include "GribWeather.h"
#endif
#include "original_routing/Engine.h"
#include "original_routing/GshhgProvider.h"
#include "quick2_routing/Engine.h"
#include "supercpn/weather_routing/QuickEngine.h"
#include "supercpn/weather_routing/AlternativeEngine.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <sys/resource.h>

// Linux getrusage can retain the parent's pre-exec RSS high-water mark. The
// current mm's VmHWM measures this executable, excluding the Python launcher.
static std::int64_t peakRssKib() {
#ifdef __linux__
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmHWM:", 0) == 0) {
      std::istringstream value(line.substr(6));
      std::int64_t kib;
      if (value >> kib) return kib;
    }
  }
#endif
  struct rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return usage.ru_maxrss;
}

int main(int argc, char** argv) {
  namespace wr = fixture::wr;
  try {
    if (argc < 3)
      throw std::runtime_error(
          "Usage: atlas_benchmark atlas|original|quick2|quick|alternative|main "
          "scenario.json [timeout_seconds] [repeat_count]");
    const std::string engine = argv[1];
    Json::Value j;
    std::ifstream input(argv[2]);
    if (!input) throw std::runtime_error("Cannot open scenario");
    input >> j;
    const double timeout = argc > 3 ? std::stod(argv[3]) : 10.;
    const unsigned repeats = argc > 4 ? std::stoul(argv[4]) : 1;
    const auto prepared = std::chrono::steady_clock::now();
    auto r = fixture::configure(j);
    if (j.isMember("polar")) fixture::loadPolar(j["polar"].asString(), r);
    auto raw = j;
    raw["cache"] = false;
    std::shared_ptr<const wr::WeatherProvider> source =
        std::make_shared<fixture::Weather>(raw, r.start);
    bool relative = false;
    if (j.isMember("grib")) {
#ifdef ATLAS_HAS_ECCODES
      source = std::make_shared<fixture::GribWeather>(j["grib"].asString());
      relative = r.environment.useCurrent;
      r.departure = wr::TimePoint{wr::Duration{
          j.get("epoch", Json::Int64(fixture::epoch.time_since_epoch().count()))
              .asInt64()}};
#else
      throw std::runtime_error("This benchmark was built without ecCodes");
#endif
    }
    const bool profileProviders = j.get("profile_providers", false).asBool();
    auto weather = std::make_shared<fixture::InstrumentedWeather>(
        source, relative, j.get("cache", false).asBool(), profileProviders);
    std::shared_ptr<const wr::LandAndBoundaryProvider> geographic;
    if (j.isMember("shoreline"))
      geographic = std::make_shared<original_routing::GshhgProvider>(
          j["shoreline"].asString());
    auto land = std::make_shared<fixture::Land>(j, r.start, geographic);
    wr::RoutingEnvironment env;
    env.grib = weather;
    env.landAndBoundaries = land;
    auto performance = std::make_shared<fixture::InstrumentedPerformance>(
        r.vessel, profileProviders);
    env.performance = performance;
    const double preparation = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - prepared)
                                   .count();
    const auto preparationRss = peakRssKib();
    for (unsigned repeat = 0; repeat < repeats; ++repeat) {
      auto request = r;
      auto stop = std::make_shared<std::atomic_bool>(false);
      const auto start = std::chrono::steady_clock::now();
      const auto cpu = std::clock();
      request.cancellation = r.cancellation.bounded(
          start + std::chrono::milliseconds(static_cast<long>(timeout * 1000)),
          stop);
      const auto weatherBefore = weather->calls, hitsBefore = weather->hits,
                 landBefore = land->calls,
                 validationBefore = land->validationCalls,
                 polarBefore = performance->calls;
      const auto weatherNsBefore = weather->queryNanoseconds,
                 landNsBefore = land->queryNanoseconds,
                 polarNsBefore = performance->queryNanoseconds;
      wr::RoutingResult route;
      atlas_routing::Diagnostics d;
      if (engine == "atlas") {
        auto result =
            atlas_routing::Engine{}.route(request, env, fixture::options(j));
        route = std::move(result.route);
        d = std::move(result.atlas);
      } else if (engine == "original") {
        original_routing::Options o;
        o.allowCoastalEndpointLeeway = true;
        route = original_routing::Engine{}.route(request, env, o);
      } else if (engine == "quick2")
        route = quick2_routing::Engine{}.route(request, env).route;
      else if (engine == "quick")
        route = wr::QuickRoutingEngine{}.route(request, env).route;
      else if (engine == "alternative")
        route = wr::AlternativeRoutingEngine{}.route(request, env).route;
      else if (engine == "main") {
        original_routing::Options o;
        o.allowCoastalEndpointLeeway = true;
        o.allowProfessionalInterimBridge = true;
        auto incumbent = original_routing::Engine{}.route(request, env, o);
        route = wr::ProfessionalEngine{}.route(request, env, &incumbent);
      } else
        throw std::runtime_error("Unknown engine");
      const auto seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - start)
                               .count();
      const auto cpuSeconds = double(std::clock() - cpu) / CLOCKS_PER_SEC;
      const auto solverWeather = weather->calls - weatherBefore,
                 solverLand = land->calls - landBefore,
                 solverPolar = performance->calls - polarBefore;
      const auto weatherNs = weather->queryNanoseconds - weatherNsBefore,
                 landNs = land->queryNanoseconds - landNsBefore,
                 polarNs = performance->queryNanoseconds - polarNsBefore;
      // Fresh request/token and original providers: this audit cannot inherit
      // a solver's deadline, approximate geography, or acceptance flags.
      wr::RouteValidationResult independent;
      if (!route.legs.empty())
        independent =
            wr::RouteValidator{}.validate(r, env, *performance, route.legs);
      const bool complete =
          (route.status == wr::RoutingStatus::Complete ||
           route.status == wr::RoutingStatus::CompleteUsingReverseRecovery ||
           route.status == wr::RoutingStatus::CompleteUsingFrontierRecovery ||
           route.status == wr::RoutingStatus::CompleteUsingGraphFallback);
      Json::Value v;
      v["case"] = j["name"];
      v["engine"] = engine;
      v["repeat"] = repeat;
      v["cache_state"] = repeat ? "warm" : "cold";
      v["status"] = wr::toString(route.status);
      v["message"] = route.message;
      v["complete"] = complete;
      v["valid"] = complete && route.validation.passed && independent.passed;
      v["independent_failure"] = independent.failureReason;
      v["seconds"] = seconds;
      v["cpu_seconds"] = cpuSeconds;
      v["preparation_seconds"] = preparation;
      v["arrival_seconds"] =
          route.validation.passed
              ? Json::Value(Json::Int64(route.metrics.elapsed.count()))
              : Json::Value();
      v["distance_nm"] = route.metrics.distanceNm;
      v["waiting_seconds"] = Json::Int64(route.metrics.waitingTime.count());
      v["weather_queries_including_audit"] =
          Json::UInt64(weather->calls - weatherBefore);
      v["weather_cache_hits_including_audit"] =
          Json::UInt64(weather->hits - hitsBefore);
      v["weather_queries"] = Json::UInt64(solverWeather);
      v["land_queries"] = Json::UInt64(solverLand);
      v["polar_queries"] = Json::UInt64(solverPolar);
      v["land_queries_including_audit"] =
          Json::UInt64(land->calls - landBefore);
      v["validation_queries"] =
          Json::UInt64(land->validationCalls - validationBefore);
      v["generated"] = Json::UInt64(route.diagnostics.generatedStates);
      v["retained"] = Json::UInt64(route.diagnostics.retainedStates);
      v["validation_samples"] = Json::UInt64(independent.samples);
      v["solver_weather_samples"] =
          Json::UInt64(route.diagnostics.weatherSamples);
      struct rusage usage{};
      getrusage(RUSAGE_SELF, &usage);
      v["rusage_peak_rss_kib"] = Json::Int64(usage.ru_maxrss);
      v["peak_rss_kib"] = Json::Int64(peakRssKib());
      v["preparation_peak_rss_kib"] = Json::Int64(preparationRss);
      v["profile"]["preprocessing_ms"] = d.preprocessingMs;
      v["profile"]["provider_timing_enabled"] = profileProviders;
      v["profile"]["weather_provider_ms"] = weatherNs / 1e6;
      v["profile"]["land_provider_ms"] = landNs / 1e6;
      v["profile"]["polar_provider_ms"] = polarNs / 1e6;
      v["profile"]["search_ms"] = d.searchMs;
      v["profile"]["value_field_ms"] = d.valueFieldMs;
      v["profile"]["value_field_cells"] = Json::UInt64(d.valueFieldCells);
      v["profile"]["pilot_legs"] = Json::UInt64(d.pilotLegs);
      v["profile"]["lane_probes"] = Json::UInt64(d.laneProbes);
      v["profile"]["reconstruction_ms"] = d.reconstructionMs;
      v["profile"]["validation_ms"] = d.validationMs;
      v["profile"]["first_valid_ms"] = d.firstValidMs;
      v["profile"]["waypoints"] = Json::UInt64(d.waypoints);
      v["profile"]["arcs"] = Json::UInt64(d.graphEdges);
      v["profile"]["edge_attempts"] = Json::UInt64(d.edgeAttempts);
      v["profile"]["integrations"] = Json::UInt64(d.shootingIntegrations);
      v["profile"]["queue_peak"] = Json::UInt64(d.queuePeak);
      v["profile"]["managed_bytes"] = Json::UInt64(d.memoryPeakBytes);
      v["profile"]["polar_queries"] = Json::UInt64(d.polarQueries);
      v["profile"]["weather_queries"] = Json::UInt64(d.weatherQueries);
      v["profile"]["geometry_queries"] = Json::UInt64(d.geometryQueries);
      v["profile"]["guidance_geometry_queries"] =
          Json::UInt64(d.guidanceGeometryQueries);
      v["profile"]["validation_failures"] = Json::UInt64(d.validationFailures);
      for (auto t : d.incumbentSeconds) v["incumbents"].append(Json::Int64(t));
      for (const auto& s : route.diagnostics.stageStopReasons)
        v["stop_reasons"].append(s);
      v["track"] = Json::arrayValue;
      for (const auto& leg : route.legs) {
        Json::Value a;
        a["lat"] = leg.end.latitude;
        a["lon"] = leg.end.longitude;
        a["seconds"] = Json::Int64((leg.endTime - r.departure).count());
        a["heading"] = leg.courseThroughWaterDegrees;
        a["wait"] = leg.stationaryWait;
        v["track"].append(a);
      }
      Json::StreamWriterBuilder b;
      b["indentation"] = "";
      std::cout << Json::writeString(b, v) << '\n';
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
}
