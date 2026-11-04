// SPDX-License-Identifier: GPL-3.0-or-later
// Production regression cases for the Professional search policy. Synthetic
// physics are intentional; licensed-chart/GRIB replays live in qualification.
#include "ProfessionalPolicy.h"
#include "ProfessionalSearchFixtures.h"
#include <gtest/gtest.h>
#include <stdexcept>
namespace {
namespace p2=supercpn::weather_routing::professional_policy;
namespace wr=supercpn::weather_routing;
// Compare scalar ticks so GoogleTest does not instantiate C++20 chrono
// formatters unavailable at the supported macOS 11 deployment target.
template <typename Clock, typename Duration>
auto PolicyTicks(const std::chrono::time_point<Clock, Duration>& value) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(value.time_since_epoch()).count();
}
template <typename Rep, typename Period>
auto PolicyTicks(const std::chrono::duration<Rep, Period>& value) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();
}
struct TestResult {wr::RoutingResult route;p2::Diagnostics diagnostics;};
struct TestSolve {
  TestResult route(const wr::RoutingRequest& r,const wr::RoutingEnvironment& e,
      const p2::Options& o={})const {
    TestResult out;
    out.route=p2::detail::solveProfessional(r,e,nullptr,o,out.diagnostics);
    return out;
  }
};
static fixtures::Fixture corridorBottleneck(double islandHalfHeight=.12) {
  auto f=fixtures::make("island");
  f.environment.landAndBoundaries=std::make_shared<wr::PolygonBoundaryProvider>(
      std::vector<std::vector<wr::GeoPoint>>{{{-islandHalfHeight,-.05},
          {-islandHalfHeight,.05},{islandHalfHeight,.05},{islandHalfHeight,-.05}}});
  f.request.options.graphCorridorWidthNm=1;
  f.request.options.maximumGraphCorridorWidthNm=120;
  f.request.options.retryStages=1;
  f.request.options.useReverseRecovery=false;
  f.request.options.useFrontierRecovery=false;
  f.request.limits.maximumGeneratedStates=200000;
  f.request.limits.maximumRouteDuration=std::chrono::hours{24};
  return f;
}
static void sameRoute(const wr::RoutingResult& a, const wr::RoutingResult& b) {
  ASSERT_EQ(a.status,b.status) << a.message << " / " << b.message;
  ASSERT_EQ(a.validation.passed,b.validation.passed);
  ASSERT_EQ(PolicyTicks(a.metrics.elapsed),PolicyTicks(b.metrics.elapsed));
  ASSERT_EQ(a.legs.size(),b.legs.size());
  for (std::size_t i=0;i<a.legs.size();++i) {
    EXPECT_EQ(a.legs[i].start,b.legs[i].start);
    EXPECT_EQ(a.legs[i].end,b.legs[i].end);
    EXPECT_EQ(PolicyTicks(a.legs[i].startTime),PolicyTicks(b.legs[i].startTime));
    EXPECT_EQ(PolicyTicks(a.legs[i].endTime),PolicyTicks(b.legs[i].endTime));
    EXPECT_DOUBLE_EQ(a.legs[i].speedThroughWaterKnots,b.legs[i].speedThroughWaterKnots);
  }
}
TEST(ProfessionalSearchPolicy, ReopensExcludedWaterFromOriginalDeparture) {
  auto f=corridorBottleneck();
  p2::Options options;options.unrestrictedFirst=false;options.unrestrictedRecovery=false;options.runInternalQuick=false;options.adaptiveCorridor=false;
  options.deriveMotionEnvelope=false;
  const auto narrow=TestSolve{}.route(f.request,f.environment,options);
  ASSERT_FALSE(narrow.route.validation.passed);
  options.adaptiveCorridor=true;
  const auto expanded=TestSolve{}.route(f.request,f.environment,options);
  ASSERT_TRUE(expanded.route.validation.passed) << expanded.route.message;
  ASSERT_FALSE(expanded.diagnostics.corridorWidthsNm.empty());
  EXPECT_GT(expanded.diagnostics.corridorWidthsNm.front(),f.request.options.graphCorridorWidthNm);
  EXPECT_LE(expanded.diagnostics.corridorWidthsNm.size(),options.maximumCorridorPasses);
  EXPECT_LE(expanded.diagnostics.corridorGenerated,expanded.diagnostics.corridorGeneratedBudget+1);
  EXPECT_EQ(PolicyTicks(expanded.route.legs.front().startTime),PolicyTicks(f.request.departure));
  EXPECT_TRUE(wr::RouteValidator{}.validate(f.request,f.environment,
      *f.environment.performance,expanded.route.legs).passed);
}

TEST(ProfessionalSearchPolicy, ComparisonEnvelopeScalesWithMotionCurrentMarginAndGrid) {
  auto f=fixtures::make("short");
  f.request.start={0,0};f.request.destination={.1,0};
  f.request.options.refinedHeadingStepDegrees=5;
  f.request.options.spatialCellNm=.1;
  f.request.limits.maximumGeneratedStates=1;
  class Flat final:public wr::VesselPerformanceModel {
   public:
    bool valid(std::string* =nullptr)const override{return true;}
    wr::PerformanceCandidate evaluate(wr::PropulsionMode,wr::ProfileRole,
        const std::string&,double,double,const wr::WaveSample&)const override {
      return {true,wr::PropulsionMode::Sail,wr::ProfileRole::SailOnly,"flat",0,6,0};
    }
    std::vector<wr::PerformanceCandidate> candidates(double,double,
        const wr::WaveSample& w,wr::PropulsionMode,wr::Duration)const override {
      return {evaluate(wr::PropulsionMode::Sail,wr::ProfileRole::SailOnly,"",0,0,w)};
    }
  };
  class Current final:public wr::WeatherProvider {
   public:
    std::shared_ptr<const wr::WeatherProvider> base;
    wr::ParameterCoverage windCoverage()const override{return base->windCoverage();}
    wr::ParameterCoverage currentCoverage()const override{return base->currentCoverage();}
    wr::ParameterCoverage waveCoverage()const override{return base->waveCoverage();}
    wr::WindSample wind(wr::GeoPoint p,wr::TimePoint t)const override{return base->wind(p,t);}
    wr::CurrentSample current(wr::GeoPoint,wr::TimePoint)const override{return {true,{2,0},{}};}
    wr::WaveSample waves(wr::GeoPoint p,wr::TimePoint t)const override{return base->waves(p,t);}
    std::string identity()const override{return "constant-cross-current";}
  };
  f.environment.performance=std::make_shared<Flat>();
  p2::Options o;o.unrestrictedFirst=false;o.runInternalQuick=false;o.adaptiveCorridor=false;
  const auto width=[&]() {
    const auto r=TestSolve{}.route(f.request,f.environment,o);
    EXPECT_GT(r.diagnostics.motionEnvelopeNm,0);
    return r.diagnostics.motionEnvelopeNm;
  };
  EXPECT_NEAR(width(),6,1e-8);
  f.request.options.timeStep=wr::Duration{7200};
  EXPECT_NEAR(width(),12,1e-8);
  auto current=std::make_shared<Current>();current->base=f.environment.grib;
  f.environment.grib=current;f.request.environment.useCurrent=true;
  EXPECT_NEAR(width(),16,1e-8);
  f.request.constraints.landSafetyMarginNm=.4;
  EXPECT_NEAR(width(),16.4,1e-8);
  f.request.options.spatialCellNm=20;
  EXPECT_NEAR(width(),std::sqrt(2.)*20,1e-8);
}

TEST(ProfessionalSearchPolicy, FailedExpandedSearchRetainsValidatedIncumbent) {
  auto f=corridorBottleneck();
  f.request.options.maximumGraphCorridorWidthNm=1.01;
  auto broad=f.request;broad.options.graphCorridorWidthNm=30;
  broad.options.maximumGraphCorridorWidthNm=120;
  broad.limits.maximumGeneratedStates=2000000;
  const auto incumbent=wr::ProfessionalEngine{}.route(broad,f.environment);
  ASSERT_TRUE(incumbent.validation.passed) << incumbent.message;
  p2::Options options;options.unrestrictedFirst=false;options.unrestrictedRecovery=false;options.runInternalQuick=false;
  options.deriveMotionEnvelope=false;
  options.corridorQualityDistanceRatio=1;options.maximumCorridorPasses=1;
  p2::Diagnostics diagnostics;
  const auto selected=p2::detail::solveProfessional(f.request,f.environment,&incumbent,options,diagnostics);
  ASSERT_EQ(diagnostics.corridorWidthsNm.size(),1U);
  EXPECT_EQ(diagnostics.corridorAccepted,0U);
  sameRoute(incumbent,selected);
}

TEST(ProfessionalSearchPolicy, WholeCancellationDuringExpandedSearchDiscardsIncumbent) {
  auto f=corridorBottleneck();
  f.request.options.maximumGraphCorridorWidthNm=1.01;
  auto broad=f.request;broad.options.graphCorridorWidthNm=30;
  broad.options.maximumGraphCorridorWidthNm=120;
  broad.limits.maximumGeneratedStates=2000000;
  const auto incumbent=wr::ProfessionalEngine{}.route(broad,f.environment);
  ASSERT_TRUE(incumbent.validation.passed) << incumbent.message;
  p2::Options options;options.unrestrictedFirst=false;options.unrestrictedRecovery=false;options.runInternalQuick=false;
  options.deriveMotionEnvelope=false;
  options.corridorQualityDistanceRatio=1;
  options.trace=[&](const auto& e) {
    if(e.stage=="corridor" && e.action=="begin")f.request.cancellation.cancel();
  };
  p2::Diagnostics diagnostics;
  const auto selected=p2::detail::solveProfessional(f.request,f.environment,&incumbent,options,diagnostics);
  ASSERT_FALSE(diagnostics.corridorWidthsNm.empty());
  EXPECT_EQ(selected.status,wr::RoutingStatus::Cancelled);
  EXPECT_TRUE(selected.legs.empty());
  EXPECT_FALSE(selected.validation.passed);
}

TEST(ProfessionalSearchPolicy, FastestCorridorQualityPassDoesNotChangeComfortObjective) {
  auto f=corridorBottleneck();
  f.request.objective.kind=wr::ObjectiveKind::WeightedTimeRiskComfort;
  p2::Options options;options.unrestrictedFirst=false;options.runInternalQuick=false;
  const auto result=TestSolve{}.route(f.request,f.environment,options);
  EXPECT_TRUE(result.diagnostics.corridorWidthsNm.empty());
}

TEST(ProfessionalSearchPolicy, OptionalChildDeadlineCannotCancelOrDiscardIncumbent) {
  auto f=corridorBottleneck();
  auto broad=f.request;broad.options.graphCorridorWidthNm=30;
  broad.limits.maximumGeneratedStates=2000000;
  const auto incumbent=wr::ProfessionalEngine{}.route(broad,f.environment);
  ASSERT_TRUE(incumbent.validation.passed) << incumbent.message;
  p2::Options options;options.unrestrictedFirst=false;options.unrestrictedRecovery=false;options.runInternalQuick=false;
  options.deriveMotionEnvelope=false;
  options.corridorQualityDistanceRatio=1;
  options.maximumCorridorMilliseconds=1;
  p2::Diagnostics diagnostics;
  const auto result=p2::detail::solveProfessional(f.request,f.environment,&incumbent,options,diagnostics);
  ASSERT_FALSE(diagnostics.corridorWidthsNm.empty());
  EXPECT_TRUE(diagnostics.corridorBudgetExhausted);
  EXPECT_FALSE(f.request.cancellation.cancelled());
  sameRoute(incumbent,result);
}

TEST(ProfessionalSearchPolicy, GraphDoesNotStopAtFirstSlowDirectConnection) {
  auto f=fixtures::make("short");
  auto& r=f.request;
  r.start={0,0};r.destination={0,1.0/60.0};
  r.vessel.tackPenalty=wr::Duration{0};r.vessel.gybePenalty=wr::Duration{0};
  r.vessel.profiles[0].rows={
    {5,{{0,0},{40,12},{45,12},{90,1},{135,12},{180,12}}},
    {30,{{0,0},{40,12},{45,12},{90,1},{135,12},{180,12}}}};
  class NorthWind final:public wr::WeatherProvider {
   public:
    wr::ParameterCoverage windCoverage()const override {
      wr::ParameterCoverage c;c.available=true;c.area={-180,-90,180,90};
      c.begins=wr::TimePoint{wr::Duration{0}};
      c.ends=wr::TimePoint{wr::Duration{2000000000}};return c;
    }
    wr::WindSample wind(wr::GeoPoint,wr::TimePoint)const override {
      return {true,wr::speedDirectionToVector(14,180),{}};
    }
    wr::ParameterCoverage currentCoverage()const override{return windCoverage();}
    wr::ParameterCoverage waveCoverage()const override{return windCoverage();}
    wr::CurrentSample current(wr::GeoPoint,wr::TimePoint)const override{return {true,{},{}};}
    wr::WaveSample waves(wr::GeoPoint,wr::TimePoint)const override{return {};}
    std::string identity()const override{return "constant-north-wind";}
  };
  f.environment.grib=std::make_shared<NorthWind>();
  f.environment.performance=std::make_shared<wr::PolarPerformanceModel>(r.vessel);
  r.options.forceForwardFailureForTesting=true;
  r.options.useReverseRecovery=false;r.options.useFrontierRecovery=false;
  r.options.useGraphFallback=true;r.options.retryStages=7;
  r.options.minimumTimeStep=wr::Duration{120};r.options.timeStep=wr::Duration{600};
  r.options.spatialCellNm=.1;r.options.destinationToleranceNm=.02;
  r.options.graphHeadingStepDegrees=15;r.options.heuristicMaximumSpeedKnots=0;
  r.limits.maximumGeneratedStates=200000;r.limits.maximumGraphLabels=100000;
  p2::Options options;options.unrestrictedFirst=false;options.runInternalQuick=false;
  options.policy=p2::Policy::Baseline;
  const auto original=TestSolve{}.route(r,f.environment,options);
  options.policy=p2::Policy::Sanity;
  const auto improved=TestSolve{}.route(r,f.environment,options);
  ASSERT_TRUE(original.route.validation.passed) << original.route.message;
  ASSERT_TRUE(improved.route.validation.passed) << improved.route.message;
  EXPECT_EQ(original.route.solverPath,wr::SolverPath::GraphFallback);
  EXPECT_EQ(improved.route.solverPath,wr::SolverPath::GraphFallback);
  EXPECT_LT(improved.route.metrics.elapsed.count(),original.route.metrics.elapsed.count()/2);
  EXPECT_GT(improved.diagnostics.graphGoalUpdates,1U);
  EXPECT_GT(improved.diagnostics.graphBoundStops,0U);
  RecordProperty("professional_passage_s",original.route.metrics.elapsed.count());
  RecordProperty("professional2_passage_s",improved.route.metrics.elapsed.count());
  // Budget exhaustion after finding a goal must retain the validated route.
  r.limits.maximumGeneratedStates=1100;
  const auto bounded=TestSolve{}.route(r,f.environment,options);
  EXPECT_TRUE(bounded.route.validation.passed) << bounded.route.message;
  EXPECT_FALSE(bounded.route.legs.empty());
}
TEST(ProfessionalSearchPolicy, ComparisonUsesObservedHigherEffortWithinSharedAllowance) {
  auto f=corridorBottleneck();
  f.request.options.routingEffortPercent=400;
  p2::Options options;options.unrestrictedFirst=false;options.unrestrictedRecovery=false;options.runInternalQuick=false;
  options.deriveMotionEnvelope=false;options.maximumCorridorPasses=1;
  bool comparing=false;unsigned childBegins=0;
  options.trace=[&](const auto& event) {
    if(event.stage=="corridor" && event.action=="begin")comparing=true;
    if(comparing && event.stage=="effort" && event.action=="begin") {
      ++childBegins;
      EXPECT_EQ(event.reason,"400");
    }
  };
  const auto result=TestSolve{}.route(f.request,f.environment,options);
  ASSERT_FALSE(result.diagnostics.corridorWidthsNm.empty());
  EXPECT_GT(childBegins,0U);
  EXPECT_LE(result.diagnostics.corridorGenerated,result.diagnostics.corridorGeneratedBudget+1);
}
} // namespace

TEST(ProfessionalSearchPolicy, CorridorFreeForwardClearsBottleneckWithOriginalSafety) {
  auto f=corridorBottleneck();
  p2::Options options;options.unrestrictedFirst=false;options.runInternalQuick=false;options.adaptiveCorridor=false;
  options.deriveMotionEnvelope=false;options.focusForwardCorridor=false;
  const auto result=TestSolve{}.route(f.request,f.environment,options);
  ASSERT_TRUE(result.route.validation.passed) << result.route.message;
  EXPECT_EQ(result.route.solverPath,wr::SolverPath::AdaptiveIsochrone);
  EXPECT_TRUE(wr::RouteValidator{}.validate(f.request,f.environment,
      *f.environment.performance,result.route.legs).passed);
  EXPECT_LE(result.route.diagnostics.cumulativeGeneratedStates,
      f.request.limits.maximumGeneratedStates+1);
}

TEST(ProfessionalSearchPolicy, PruningPreservesSailPlansWithDifferentFutureTransitions) {
  auto f=fixtures::make("short");
  f.request.start={0,0};f.request.destination={0,.5};
  f.request.options.maximumSearchAngleDegrees=1;
  f.request.options.adaptiveHeadings=false;
  f.request.options.retryStages=1;
  f.request.options.useGraphFallback=false;
  f.request.options.useReverseRecovery=false;
  f.request.options.useFrontierRecovery=false;
  f.request.limits.maximumRouteDuration=std::chrono::hours{6};
  f.request.vessel.sailPlanChangePenalty=std::chrono::hours{8};
  f.request.vessel.profiles.clear();
  for(int i=0;i<2;++i) {
    wr::PerformanceProfile p;p.identity="plan-"+std::to_string(i);
    p.rows={{5,{{0,6},{180,6}}},{14,{{0,6},{180,6}}},
            {30,{{0,i?6.:1.},{180,i?6.:1.}}}};
    f.request.vessel.profiles.push_back(p);
  }
  class Shift final:public wr::WeatherProvider {
   public:
    wr::TimePoint epoch;
    wr::ParameterCoverage windCoverage()const override {
      wr::ParameterCoverage c;c.available=true;c.area={-180,-90,180,90};
      c.begins=epoch;c.ends=epoch+std::chrono::hours{24};return c;
    }
    wr::ParameterCoverage currentCoverage()const override{return windCoverage();}
    wr::ParameterCoverage waveCoverage()const override{return windCoverage();}
    wr::WindSample wind(wr::GeoPoint,wr::TimePoint t)const override {
      return {true,wr::speedDirectionToVector(t<epoch+std::chrono::hours{1}?14:30,0),{}};
    }
    wr::CurrentSample current(wr::GeoPoint,wr::TimePoint)const override{return {true,{},{}};}
    wr::WaveSample waves(wr::GeoPoint,wr::TimePoint)const override{return {};}
    std::string identity()const override{return "two-sail-plan-transition";}
  };
  auto weather=std::make_shared<Shift>();weather->epoch=f.request.departure;
  f.environment.grib=weather;
  f.environment.performance=std::make_shared<wr::PolarPerformanceModel>(f.request.vessel);
  p2::Options o;o.unrestrictedFirst=false;o.runInternalQuick=false;o.adaptiveCorridor=false;
  o.focusForwardCorridor=false;
  o.preserveContinuationState=false;
  const auto collapsed=TestSolve{}.route(f.request,f.environment,o);
  EXPECT_FALSE(collapsed.route.validation.passed);
  o.preserveContinuationState=true;
  const auto result=TestSolve{}.route(f.request,f.environment,o);
  ASSERT_TRUE(result.route.validation.passed)<<result.route.message;
  ASSERT_FALSE(result.route.legs.empty());
  EXPECT_EQ(result.route.legs.front().sailPlan,1);
  EXPECT_LE(PolicyTicks(result.route.metrics.elapsed),PolicyTicks(std::chrono::hours{6}));
}

TEST(ProfessionalSearchPolicy, UnrestrictedRecoveryReopensOriginalDepartureWithinAllowance) {
  auto f=corridorBottleneck();
  p2::Options o;o.unrestrictedFirst=false;o.runInternalQuick=false;
  bool began=false;
  o.trace=[&](const auto& e) {
    if(e.stage=="unrestricted_recovery" && e.action=="begin") {
      began=true;EXPECT_EQ(PolicyTicks(e.time),PolicyTicks(f.request.departure));EXPECT_EQ(e.position,f.request.start);
    }
  };
  const auto result=TestSolve{}.route(f.request,f.environment,o);
  ASSERT_TRUE(began);
  ASSERT_TRUE(result.route.validation.passed)<<result.route.message;
  EXPECT_EQ(result.diagnostics.corridorAccepted,1U);
  EXPECT_TRUE(result.diagnostics.corridorWidthsNm.empty());
  EXPECT_LE(result.diagnostics.corridorGenerated,result.diagnostics.corridorGeneratedBudget+1);
  EXPECT_TRUE(wr::RouteValidator{}.validate(f.request,f.environment,
      *f.environment.performance,result.route.legs).passed);
}

TEST(ProfessionalSearchPolicy, UnrestrictedRecoveryFailureRetainsIncumbentAndWholeCancelOverridesIt) {
  auto f=corridorBottleneck();
  auto broad=f.request;broad.options.graphCorridorWidthNm=30;
  broad.limits.maximumGeneratedStates=2000000;
  const auto incumbent=wr::ProfessionalEngine{}.route(broad,f.environment);
  ASSERT_TRUE(incumbent.validation.passed);
  p2::Options o;o.unrestrictedFirst=false;o.runInternalQuick=false;o.corridorQualityDistanceRatio=1;
  o.corridorWorkFraction=.0001;
  p2::Diagnostics d;
  const auto retained=p2::detail::solveProfessional(f.request,f.environment,&incumbent,o,d);
  sameRoute(incumbent,retained);
  EXPECT_EQ(d.corridorAccepted,0U);
  EXPECT_GT(d.corridorGenerated,0U);
  o.corridorWorkFraction=.5;
  o.trace=[&](const auto& e) {
    if(e.stage=="unrestricted_recovery" && e.action=="begin")f.request.cancellation.cancel();
  };
  p2::Diagnostics cancelledDiagnostics;
  const auto cancelled=p2::detail::solveProfessional(f.request,f.environment,&incumbent,o,cancelledDiagnostics);
  EXPECT_EQ(cancelled.status,wr::RoutingStatus::Cancelled);
  EXPECT_TRUE(cancelled.legs.empty());EXPECT_FALSE(cancelled.validation.passed);
}

TEST(ProfessionalSearchPolicy, UnrestrictedInitialBoundsFocusedComparisonByValidatedArrival) {
  auto f=corridorBottleneck();
  p2::Options o;o.unrestrictedFirst=false;o.runInternalQuick=false;o.unrestrictedFirst=true;
  wr::TimePoint broadArrival{};std::int64_t focusedHorizon=-1;unsigned focusedEffort=0;
  bool focused=false;
  o.trace=[&](const auto& e) {
    if(e.stage=="unrestricted_initial" && e.action=="end")broadArrival=e.time;
    if(e.stage=="focused_comparison" && e.action=="begin") {
      focused=true;focusedHorizon=std::stoll(e.reason.substr(10));
    }
    if(focused && e.stage=="effort" && e.action=="begin")focusedEffort=std::stoul(e.reason);
  };
  const auto result=TestSolve{}.route(f.request,f.environment,o);
  ASSERT_TRUE(result.route.validation.passed)<<result.route.message;
  ASSERT_GT(PolicyTicks(broadArrival),PolicyTicks(f.request.departure));
  EXPECT_EQ(focusedHorizon,(broadArrival-f.request.departure).count());
  EXPECT_EQ(focusedEffort,100U);
  EXPECT_LE(PolicyTicks(result.route.metrics.elapsed),PolicyTicks(broadArrival-f.request.departure));
  EXPECT_TRUE(wr::RouteValidator{}.validate(f.request,f.environment,
      *f.environment.performance,result.route.legs).passed);
}

TEST(ProfessionalSearchPolicy, UnrestrictedInitialAcceptanceCannotOverrideWholeCancellation) {
  auto f=fixtures::make("short");
  p2::Options o;o.unrestrictedFirst=false;o.runInternalQuick=false;o.unrestrictedFirst=true;
  o.trace=[&](const auto& e) {
    if(e.stage=="unrestricted_initial" && e.action=="end")f.request.cancellation.cancel();
  };
  const auto result=TestSolve{}.route(f.request,f.environment,o);
  EXPECT_EQ(result.route.status,wr::RoutingStatus::Cancelled);
  EXPECT_TRUE(result.route.legs.empty());EXPECT_FALSE(result.route.validation.passed);
}

TEST(ProfessionalSearchPolicy, ReverseRecoveryAndQuickIncumbentRespectRouteHorizon) {
  auto f=fixtures::make("open");auto& r=f.request;
  r.start={0,-.16};r.destination={0,.16};
  r.limits.maximumRouteDuration=std::chrono::hours{1};
  r.options.forceForwardFailureForTesting=true;
  r.options.useGraphFallback=false;r.options.useFrontierRecovery=false;
  r.options.retryStages=6;
  p2::Options o;o.runInternalQuick=false;o.policy=p2::Policy::Baseline;
  const auto control=TestSolve{}.route(r,f.environment,o);
  ASSERT_TRUE(control.route.validation.passed)<<control.route.message;
  ASSERT_GT(PolicyTicks(control.route.metrics.elapsed),PolicyTicks(r.limits.maximumRouteDuration));
  o.policy=p2::Policy::Sanity;
  const auto bounded=TestSolve{}.route(r,f.environment,o);
  EXPECT_FALSE(bounded.route.validation.passed);
  EXPECT_TRUE(bounded.route.legs.empty());
  p2::Diagnostics d;
  const auto selected=p2::detail::solveProfessional(r,f.environment,&control.route,o,d);
  EXPECT_FALSE(selected.validation.passed);EXPECT_TRUE(selected.legs.empty());
}

TEST(ProfessionalSearchPolicy, GraphCannotExtendAnExpiredRouteHorizon) {
  auto f=fixtures::make("open");auto& r=f.request;
  r.start={0,-.05};r.destination={0,.05};
  r.limits.maximumRouteDuration=std::chrono::minutes{10};
  r.options.forceForwardFailureForTesting=true;
  r.options.forceReverseFailureForTesting=true;
  r.options.useFrontierRecovery=false;r.options.retryStages=7;
  p2::Options o;o.runInternalQuick=false;o.policy=p2::Policy::Baseline;
  const auto control=TestSolve{}.route(r,f.environment,o);
  ASSERT_TRUE(control.route.validation.passed)<<control.route.message;
  ASSERT_GT(PolicyTicks(control.route.metrics.elapsed),PolicyTicks(r.limits.maximumRouteDuration));
  o.policy=p2::Policy::Sanity;
  const auto bounded=TestSolve{}.route(r,f.environment,o);
  EXPECT_FALSE(bounded.route.validation.passed);
  EXPECT_TRUE(bounded.route.legs.empty());
}

TEST(ProfessionalSearchPolicy, ValidatedQuickRetainsSingleFirstTierImprovementBudget) {
  auto f=fixtures::make("short");
  const auto quick=wr::ProfessionalEngine{}.route(f.request,f.environment);
  ASSERT_TRUE(quick.validation.passed);
  f.request.options.routingEffortPercent=400;
  p2::Options o;o.runInternalQuick=false;o.unrestrictedFirst=true;
  unsigned tiers=0;bool broadTried=false;
  o.trace=[&](const auto& e) {
    if(e.stage=="unrestricted_initial")broadTried=true;
    if(e.stage=="effort" && e.action=="begin") {
      ++tiers;EXPECT_EQ(e.reason,"100");
    }
  };
  p2::Diagnostics d;
  const auto selected=p2::detail::solveProfessional(f.request,f.environment,&quick,o,d);
  ASSERT_TRUE(selected.validation.passed);
  EXPECT_FALSE(broadTried);EXPECT_EQ(tiers,1U);
  EXPECT_LE(PolicyTicks(selected.metrics.elapsed),PolicyTicks(quick.metrics.elapsed));
}

TEST(ProfessionalSearchPolicy, BoundedConnectionRefinesAnOverlongInitialEstimate) {
  auto f=fixtures::make("short");auto& r=f.request;
  r.start={0,-.05};r.destination={0,.05};
  r.environment.useCurrent=true;
  r.options.useGraphFallback=false;r.options.useReverseRecovery=false;
  r.options.useFrontierRecovery=false;r.options.retryStages=1;
  r.limits.maximumRouteDuration=std::chrono::hours{1};
  r.limits.maximumGeneratedStates=1;
  class Flat final:public wr::VesselPerformanceModel {
   public:
    bool valid(std::string* =nullptr)const override{return true;}
    wr::PerformanceCandidate evaluate(wr::PropulsionMode,wr::ProfileRole,
        const std::string&,double,double,const wr::WaveSample&)const override {
      return {true,wr::PropulsionMode::Sail,wr::ProfileRole::SailOnly,"flat",0,6,0};
    }
    std::vector<wr::PerformanceCandidate> candidates(double,double,
        const wr::WaveSample& w,wr::PropulsionMode,wr::Duration)const override {
      return {evaluate(wr::PropulsionMode::Sail,wr::ProfileRole::SailOnly,"",0,0,w)};
    }
  };
  class Tide final:public wr::WeatherProvider {
   public:
    std::shared_ptr<const wr::WeatherProvider> base;wr::TimePoint epoch;
    wr::ParameterCoverage windCoverage()const override{return base->windCoverage();}
    wr::ParameterCoverage currentCoverage()const override{return base->currentCoverage();}
    wr::ParameterCoverage waveCoverage()const override{return base->waveCoverage();}
    wr::WindSample wind(wr::GeoPoint p,wr::TimePoint t)const override{return base->wind(p,t);}
    wr::CurrentSample current(wr::GeoPoint,wr::TimePoint t)const override {
      return {true,{t<epoch+wr::Duration{450}?-8.:8.,0},{}};
    }
    wr::WaveSample waves(wr::GeoPoint p,wr::TimePoint t)const override{return base->waves(p,t);}
    std::string identity()const override{return "adverse-initial-current-then-fair";}
  };
  auto tide=std::make_shared<Tide>();tide->base=f.environment.grib;tide->epoch=r.departure;
  f.environment.grib=tide;f.environment.performance=std::make_shared<Flat>();
  p2::Options o;o.runInternalQuick=false;o.unrestrictedFirst=false;
  o.refineBoundedConnections=false;
  const auto estimated=TestSolve{}.route(r,f.environment,o);
  o.refineBoundedConnections=true;
  const auto refined=TestSolve{}.route(r,f.environment,o);
  ASSERT_TRUE(refined.route.validation.passed)<<refined.route.message;
  EXPECT_EQ(refined.route.legs.size(),1U);
  EXPECT_EQ(refined.route.diagnostics.generatedStates,0U);
  EXPECT_TRUE(!estimated.route.validation.passed || estimated.route.legs.size()>1U);
  EXPECT_LE(PolicyTicks(refined.route.metrics.elapsed),PolicyTicks(r.limits.maximumRouteDuration));
  EXPECT_TRUE(wr::RouteValidator{}.validate(r,f.environment,*f.environment.performance,
      refined.route.legs).passed);
}
