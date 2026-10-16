// SPDX-License-Identifier: GPL-3.0-or-later
// Audit previously recorded controls, without running any search algorithm.
#include "../../vendor/atlas-routing-engine/tests/Fixtures.h"
#include "MotionKernel.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace wr = fixture::wr;
namespace in = wr::internal;

int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("scenario.json record.json");
    Json::Value scenario, record;
    std::ifstream a(argv[1]), b(argv[2]);
    a >> scenario;
    b >> record;
    // This replay client intentionally supports only the synthetic provider.
    if (scenario.isMember("grib") || scenario.isMember("shoreline") ||
        scenario.isMember("polar"))
      throw std::runtime_error("not a synthetic replay");
    auto r = fixture::configure(scenario);
    wr::RoutingEnvironment env;
    env.grib = std::make_shared<fixture::Weather>(scenario, r.start);
    env.landAndBoundaries = std::make_shared<fixture::Land>(scenario, r.start);
    wr::PolarPerformanceModel perf(r.vessel);
    in::MotionState state;
    state.position = r.start;
    state.time = r.departure;
    if (r.constraints.landSafetyMarginNm > 0)
      state.departureEgressActive =
          env.landAndBoundaries->distanceToForbiddenNm(r.start) <
          r.constraints.landSafetyMarginNm;
    std::vector<wr::RouteLeg> legs;
    wr::RoutingDiagnostics diagnostics;
    for (const auto& action : record["track"]) {
      auto endTime = r.departure + wr::Duration{action["seconds"].asInt64()};
      const auto duration = endTime - state.time;
      const wr::GeoPoint end{action["lat"].asDouble(), action["lon"].asDouble()};
      std::optional<in::MotionCandidate> chosen;
      if (action["wait"].asBool()) {
        chosen = in::checkedWait(r, env, state, duration, diagnostics);
        if (chosen) chosen->state.modeDuration = {};
      } else {
        // Legacy records omit the original numerical partition. Recover it
        // only by exact endpoint agreement, never by snapping onto a track.
        for (long slice : {300L, 30L, 60L, 900L, 1800L, 3600L, 21600L}) {
          auto failure = wr::RoutingStatus::Complete;
          auto moves = in::checkedMotion(r, env, perf, state,
              action["heading"].asDouble(), duration, wr::Duration{slice}, diagnostics, &failure);
          for (auto& move : moves)
            if (move.state.time == endTime &&
                wr::distanceNm(move.state.position, end) < 1e-7) {
              chosen = std::move(move);
              break;
            }
          if (chosen) break;
        }
      }
      if (!chosen || chosen->state.time != endTime ||
          wr::distanceNm(chosen->state.position, end) >= 1e-7)
        throw std::runtime_error("recorded controls do not reproduce endpoint");
      state = chosen->state;
      legs.push_back(std::move(chosen->leg));
    }
    auto validation = wr::RouteValidator{}.validate(r, env, perf, legs);
    Json::Value out;
    out["valid"] = validation.passed;
    out["reason"] = validation.failureReason;
    out["arrival_seconds"] = Json::Int64((state.time - r.departure).count());
    out["samples"] = Json::UInt64(validation.samples);
    std::cout << out << '\n';
    return 0;
  } catch (const std::exception& e) {
    Json::Value out;
    out["valid"] = false;
    out["reason"] = e.what();
    std::cout << out << '\n';
    return 0;
  }
}
