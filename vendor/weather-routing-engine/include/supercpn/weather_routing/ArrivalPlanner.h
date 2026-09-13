#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing {

struct ArrivalPlanningOptions {
  TimePoint plannedArrival{};
  /** Optional lower bound, used for example to prevent a boat departure
   * being scheduled before the current time. */
  std::optional<TimePoint> earliestAllowedDeparture;
  Duration safetyMargin{};
  Duration searchHorizon{std::chrono::hours{24 * 30}};
  Duration initialSearchStep{std::chrono::hours{1}};
  Duration refinementStep{std::chrono::minutes{5}};
  Duration arrivalTolerance{std::chrono::minutes{1}};
  unsigned maximumRouteEvaluations{24};
  double nominalPassageSpeedKnots{5.0};
};

struct ArrivalCandidate {
  TimePoint departure{};
  std::optional<TimePoint> arrival;
  bool complete{};
  bool forwardValidated{};
  Duration deadlineError{};
  RoutingStatus routingStatus{RoutingStatus::InternalError};
  std::string message;
};

struct ArrivalPlanningDiagnostics {
  TimePoint effectiveDeadline{};
  TimePoint earliestDeparture{};
  TimePoint reverseProjectedDeparture{};
  std::vector<TimePoint> evaluatedDepartures;
  unsigned forwardRoutesEvaluated{};
  unsigned completeRoutes{};
  unsigned feasibleRoutes{};
  unsigned reverseProjections{};
  unsigned bracketRefinements{};
  bool evaluationBudgetExhausted{};
};

enum class ArrivalPlanningStatus {
  Complete,
  NoFeasibleSchedule,
  InvalidRequest,
  Cancelled
};

struct ArrivalPlanningResult {
  ArrivalPlanningStatus status{ArrivalPlanningStatus::InvalidRequest};
  std::optional<RoutingResult> route;
  std::optional<TimePoint> departure;
  std::optional<TimePoint> arrival;
  Duration scheduleMargin{};
  std::vector<ArrivalCandidate> candidates;
  ArrivalPlanningDiagnostics diagnostics;
  std::string message;
};

using ArrivalRouteEvaluator = std::function<RoutingResult(TimePoint departure)>;

class ArrivalPlanner {
public:
  ArrivalPlanningResult plan(const RoutingRequest& request,
                             const ArrivalPlanningOptions& options,
                             const ArrivalRouteEvaluator& evaluate) const;
};

std::string toString(ArrivalPlanningStatus status);

}  // namespace supercpn::weather_routing
