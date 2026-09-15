#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "supercpn/weather_routing/ArrivalPlanner.h"

namespace wr = supercpn::weather_routing;

namespace {

template <typename Clock, typename Duration>
auto ChronoTicks(const std::chrono::time_point<Clock, Duration>& value) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             value.time_since_epoch())
      .count();
}

wr::RoutingRequest Request() {
  wr::RoutingRequest request;
  request.start = {53.30, -4.63};
  request.destination = {55.22, -6.68};
  request.departure = wr::TimePoint{wr::Duration{1'800'000'000}};
  return request;
}

wr::RoutingResult CompleteRoute(const wr::RoutingRequest& request,
                                wr::TimePoint departure, wr::Duration elapsed,
                                bool validated = true) {
  wr::RoutingResult route;
  route.status = wr::RoutingStatus::Complete;
  route.validation.passed = validated;
  route.metrics.elapsed = elapsed;
  wr::RouteLeg leg;
  leg.start = request.start;
  leg.end = request.destination;
  leg.startTime = departure;
  leg.endTime = departure + elapsed;
  route.legs.push_back(leg);
  return route;
}

wr::ArrivalPlanningOptions Options(const wr::RoutingRequest& request) {
  wr::ArrivalPlanningOptions options;
  options.plannedArrival = request.departure + std::chrono::hours{24};
  options.safetyMargin = std::chrono::minutes{30};
  options.searchHorizon = std::chrono::hours{20};
  options.initialSearchStep = std::chrono::hours{2};
  options.refinementStep = std::chrono::minutes{5};
  options.arrivalTolerance = std::chrono::minutes{1};
  options.maximumRouteEvaluations = 18;
  options.nominalPassageSpeedKnots = 5.0;
  return options;
}

}  // namespace

TEST(ArrivalPlanner, SelectsLatestForwardValidatedDepartureBeforeDeadline) {
  const wr::RoutingRequest request = Request();
  const wr::ArrivalPlanningOptions options = Options(request);
  const wr::TimePoint deadline = options.plannedArrival - options.safetyMargin;
  std::vector<wr::TimePoint> evaluated;

  const wr::ArrivalPlanningResult result =
      wr::ArrivalPlanner().plan(request, options, [&](wr::TimePoint departure) {
        evaluated.push_back(departure);
        return CompleteRoute(request, departure, std::chrono::hours{10});
      });

  ASSERT_EQ(result.status, wr::ArrivalPlanningStatus::Complete);
  ASSERT_TRUE(result.departure);
  ASSERT_TRUE(result.arrival);
  EXPECT_EQ(ChronoTicks(*result.departure),
            ChronoTicks(deadline - std::chrono::hours{10}));
  EXPECT_EQ(ChronoTicks(*result.arrival), ChronoTicks(deadline));
  ASSERT_TRUE(result.route);
  EXPECT_TRUE(result.route->validation.passed);
  EXPECT_GT(result.diagnostics.reverseProjections, 0U);
  ASSERT_EQ(evaluated.size(),
            result.diagnostics.evaluatedDepartures.size());
  for (std::size_t index = 0; index < evaluated.size(); ++index) {
    EXPECT_EQ(ChronoTicks(evaluated[index]),
              ChronoTicks(result.diagnostics.evaluatedDepartures[index]));
  }
}

TEST(ArrivalPlanner, NeverEvaluatesBeforeEarliestAllowedDeparture) {
  const wr::RoutingRequest request = Request();
  wr::ArrivalPlanningOptions options = Options(request);
  const wr::TimePoint deadline = options.plannedArrival - options.safetyMargin;
  options.earliestAllowedDeparture = deadline - std::chrono::hours{5};
  std::vector<wr::TimePoint> evaluated;

  const wr::ArrivalPlanningResult result =
      wr::ArrivalPlanner().plan(request, options, [&](wr::TimePoint departure) {
        evaluated.push_back(departure);
        return CompleteRoute(request, departure, std::chrono::hours{10});
      });

  EXPECT_EQ(result.status, wr::ArrivalPlanningStatus::NoFeasibleSchedule);
  ASSERT_FALSE(evaluated.empty());
  for (const wr::TimePoint departure : evaluated) {
    EXPECT_GE(ChronoTicks(departure),
              ChronoTicks(*options.earliestAllowedDeparture));
  }
}

TEST(ArrivalPlanner, RejectsRoutesWhichFailForwardValidation) {
  const wr::RoutingRequest request = Request();
  wr::ArrivalPlanningOptions options = Options(request);
  options.maximumRouteEvaluations = 5;

  const wr::ArrivalPlanningResult result =
      wr::ArrivalPlanner().plan(request, options, [&](wr::TimePoint departure) {
        return CompleteRoute(request, departure, std::chrono::hours{10}, false);
      });

  EXPECT_EQ(result.status, wr::ArrivalPlanningStatus::NoFeasibleSchedule);
  EXPECT_EQ(result.diagnostics.completeRoutes, 0U);
  EXPECT_EQ(result.diagnostics.feasibleRoutes, 0U);
}
