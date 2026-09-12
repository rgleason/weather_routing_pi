#include "supercpn/weather_routing/ArrivalPlanner.h"
#include "supercpn/weather_routing/Engine.h"
#include "supercpn/weather_routing/Providers.h"
#include "supercpn/weather_routing/ResourcePolicy.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace supercpn::weather_routing;

namespace {
RoutingRequest request() {
  RoutingRequest value;
  value.start = {50.0, -4.0};
  value.destination = {50.0, -3.5};
  value.departure = TimePoint{Duration{1'800'000'000}};
  PerformanceProfile profile;
  profile.role = ProfileRole::SailOnly;
  profile.identity = "contract-polar";
  profile.rows = {{10.0, {{0.0, 5.0}, {90.0, 5.0}, {180.0, 5.0}}},
                  {20.0, {{0.0, 5.0}, {90.0, 5.0}, {180.0, 5.0}}}};
  value.vessel.profiles = {std::move(profile)};
  value.environment.useCurrent = false;
  value.environment.useWaves = false;
  value.options.timeStep = std::chrono::minutes{30};
  value.options.minimumTimeStep = std::chrono::minutes{10};
  value.options.destinationToleranceNm = 1.0;
  return value;
}

RoutingEnvironment environment(const RoutingRequest& route) {
  UniformWeatherProvider::Configuration weather;
  weather.begins = route.departure;
  weather.ends = route.departure + std::chrono::hours{48};
  weather.area = {-10.0, 40.0, 5.0, 60.0};
  weather.windTowardKnots = Vector2{0.0, 10.0};
  weather.identity = "contract-weather";
  RoutingEnvironment result;
  result.grib = std::make_shared<UniformWeatherProvider>(weather);
  return result;
}

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main() {
  try {
    const auto baseline = selectRoutingResourcePolicy(100.0, 100);
    const auto expanded = selectRoutingResourcePolicy(100.0, 400);
    require(baseline.maximumGeneratedStates == 1'145'000,
            "incorrect baseline total budget");
    require(baseline.maximumCoastalEndpointGeneratedStates == 20'000,
            "incorrect baseline coastal endpoint budget");
    require(baseline.maximumForwardGeneratedStates == 675'000,
            "incorrect baseline forward budget");
    require(baseline.maximumFrontierRecoveryGeneratedStates == 225'000,
            "incorrect baseline frontier budget");
    require(baseline.maximumGraphGeneratedStates == 225'000,
            "incorrect baseline graph budget");
    require(expanded.maximumGeneratedStates ==
                baseline.maximumGeneratedStates * 4,
            "400% effort did not scale every stage cumulatively");
    require(expanded.maximumCoastalEndpointGeneratedStates ==
                baseline.maximumCoastalEndpointGeneratedStates * 4,
            "400% effort did not scale the coastal endpoint stage");
    require(normalizeRoutingEffortPercent(149) == 150,
            "effort tier normalization changed");

    const RoutingRequest route = request();
    const RoutingEnvironment weather = environment(route);
    RoutingEngine engine;
    const RoutingResult first = engine.route(route, weather);
    const RoutingResult second = engine.route(route, weather);
    require(first.status == second.status, "routing status is not deterministic");
    require(first.validation.passed == second.validation.passed,
            "validation result is not deterministic");
    require(first.legs.size() == second.legs.size(),
            "route leg count is not deterministic");
    require(first.metrics.elapsed == second.metrics.elapsed,
            "route elapsed time is not deterministic");
    require(first.diagnostics.generatedStates ==
                second.diagnostics.generatedStates,
            "generated-state count is not deterministic");
    require(first.validation.passed, first.message.c_str());

    RoutingRequest forecastThenClimatology = request();
    forecastThenClimatology.destination = {50.0, -0.5};
    forecastThenClimatology.options.timeStep = std::chrono::hours{12};
    forecastThenClimatology.options.minimumTimeStep = std::chrono::hours{1};
    for (auto& profile : forecastThenClimatology.vessel.profiles)
      for (auto& row : profile.rows)
        for (auto& point : row.points) point.boatSpeedKnots = 1.0;
    forecastThenClimatology.environment.climatology =
        ClimatologyFallbackPolicy::AllowWithWarning;
    UniformWeatherProvider::Configuration shortForecast;
    shortForecast.begins = forecastThenClimatology.departure;
    shortForecast.ends =
        forecastThenClimatology.departure + std::chrono::hours{2};
    shortForecast.area = {-20.0, 40.0, 20.0, 60.0};
    shortForecast.windTowardKnots = Vector2{0.0, 10.0};
    shortForecast.identity = "short-contract-forecast";
    shortForecast.source = EnvironmentalSource::GribForecast;
    UniformWeatherProvider::Configuration climatology;
    climatology.area = shortForecast.area;
    climatology.windTowardKnots = Vector2{0.0, 10.0};
    climatology.identity = "contract-climatology";
    climatology.source = EnvironmentalSource::Climatology;
    RoutingEnvironment mixedWeather;
    mixedWeather.grib = std::make_shared<UniformWeatherProvider>(shortForecast);
    mixedWeather.climatology =
        std::make_shared<UniformWeatherProvider>(climatology);
    const RoutingResult mixed =
        engine.route(forecastThenClimatology, mixedWeather);
    require(mixed.validation.passed, mixed.message.c_str());
    require(mixed.metrics.elapsed > std::chrono::hours{128},
            "route was incorrectly limited to the 128-hour cache horizon");
    require(mixed.environment.gribWindDuration > Duration::zero(),
            "route used no forecast wind before forecast expiry");
    require(mixed.environment.climatologyWindDuration > Duration::zero(),
            "route did not continue with climatology after forecast expiry");

    ArrivalPlanningOptions arrivalOptions;
    arrivalOptions.plannedArrival =
        route.departure + std::chrono::hours{24};
    arrivalOptions.safetyMargin = std::chrono::minutes{30};
    arrivalOptions.searchHorizon = std::chrono::hours{20};
    arrivalOptions.initialSearchStep = std::chrono::hours{2};
    arrivalOptions.refinementStep = std::chrono::minutes{5};
    arrivalOptions.maximumRouteEvaluations = 18;
    arrivalOptions.nominalPassageSpeedKnots = 5.0;
    const TimePoint effectiveDeadline =
        arrivalOptions.plannedArrival - arrivalOptions.safetyMargin;
    std::vector<TimePoint> evaluatedDepartures;
    ArrivalPlanner arrivalPlanner;
    const ArrivalPlanningResult arrivalPlan = arrivalPlanner.plan(
        route, arrivalOptions, [&](TimePoint departure) {
          evaluatedDepartures.push_back(departure);
          RoutingResult candidate;
          candidate.status = RoutingStatus::Complete;
          candidate.validation.passed = true;
          candidate.metrics.elapsed = std::chrono::hours{10};
          RouteLeg leg;
          leg.start = route.start;
          leg.end = route.destination;
          leg.startTime = departure;
          leg.endTime = departure + candidate.metrics.elapsed;
          candidate.legs.push_back(leg);
          return candidate;
        });
    require(arrivalPlan.status == ArrivalPlanningStatus::Complete,
            arrivalPlan.message.c_str());
    require(arrivalPlan.departure.has_value(),
            "arrival planner did not select a departure");
    require(arrivalPlan.arrival == effectiveDeadline,
            "arrival planner did not converge on the effective deadline");
    require(*arrivalPlan.departure ==
                effectiveDeadline - std::chrono::hours{10},
            "arrival planner selected the wrong latest departure");
    require(arrivalPlan.route && arrivalPlan.route->validation.passed,
            "arrival planner returned a route without forward validation");
    require(arrivalPlan.diagnostics.reverseProjections > 0,
            "arrival planner did not use reverse timing projection");
    require(evaluatedDepartures ==
                arrivalPlan.diagnostics.evaluatedDepartures,
            "arrival planner diagnostics changed evaluation order");

    ArrivalPlanningOptions impossibleOptions = arrivalOptions;
    impossibleOptions.maximumRouteEvaluations = 4;
    const ArrivalPlanningResult impossible = arrivalPlanner.plan(
        route, impossibleOptions, [](TimePoint) {
          RoutingResult candidate;
          candidate.status = RoutingStatus::NoFeasibleRoute;
          candidate.message = "synthetic route failure";
          return candidate;
        });
    require(impossible.status ==
                ArrivalPlanningStatus::NoFeasibleSchedule,
            "failed forward routes produced a false arrival schedule");

    ArrivalPlanningOptions boundedOptions = arrivalOptions;
    boundedOptions.earliestAllowedDeparture =
        effectiveDeadline - std::chrono::hours{5};
    boundedOptions.maximumRouteEvaluations = 8;
    std::vector<TimePoint> boundedDepartures;
    const ArrivalPlanningResult bounded = arrivalPlanner.plan(
        route, boundedOptions, [&](TimePoint departure) {
          boundedDepartures.push_back(departure);
          RoutingResult candidate;
          candidate.status = RoutingStatus::Complete;
          candidate.validation.passed = true;
          candidate.metrics.elapsed = std::chrono::hours{10};
          RouteLeg leg;
          leg.start = route.start;
          leg.end = route.destination;
          leg.startTime = departure;
          leg.endTime = departure + candidate.metrics.elapsed;
          candidate.legs.push_back(leg);
          return candidate;
        });
    require(bounded.status == ArrivalPlanningStatus::NoFeasibleSchedule,
            "earliest departure bound produced an impossible schedule");
    require(!boundedDepartures.empty(),
            "earliest departure test evaluated no routes");
    for (const TimePoint departure : boundedDepartures)
      require(departure >= *boundedOptions.earliestAllowedDeparture,
              "arrival planner evaluated before earliest allowed departure");
    std::cout << "deterministic route with " << first.legs.size()
              << " legs\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
