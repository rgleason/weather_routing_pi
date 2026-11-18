// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "QuickCompletion.h"
#include "Quick2Fixtures.h"
#include "engine/native/RoutingEngineSequence.h"

namespace wr = supercpn::weather_routing;
namespace {
wr::PartialRouteArchive::Entry Prefix(const wr::RoutingRequest& request,
                                     std::vector<wr::RouteLeg> legs) {
  auto end = legs.back().end;
  auto time = legs.back().endTime;
  return {{end,time,"qualified test prefix",wr::distanceNm(end,request.destination),
           wr::PartialRoutePhase::Beam},wr::FreezePartialLegs(std::move(legs))};
}
}
TEST(QuickCompletion, FinishesProvenPrefixesAndPassesFreshWholeRouteReplay) {
  for (const auto name : {"open", "upwind", "tidal", "island"}) {
    SCOPED_TRACE(name);
    auto f = quick2_fixtures::make(name);
    auto full = quick2_routing::Engine{}.route(f.request,f.environment).route;
    ASSERT_TRUE(weather_routing::native::AcceptedRoute(full)) << full.message;
    ASSERT_GT(full.legs.size(),1U);
    auto legs = full.legs;
    legs.resize(legs.size()-1);
    auto entry = Prefix(f.request,std::move(legs));
    const auto* pointer = &entry;
    auto completed = weather_routing::CompleteQuickPrefixes(f.request,f.environment,
        std::span{&pointer,1});
    ASSERT_TRUE(weather_routing::native::AcceptedRoute(completed.route))
        << completed.route.message << " calls=" << completed.motionCalls;
    EXPECT_LE(wr::distanceNm(completed.route.legs.back().end,f.request.destination),.002);
    wr::PolarPerformanceModel polar(f.request.vessel);
    auto replay = wr::RouteValidator{}.validate(f.request,f.environment,
        f.environment.performance ? *f.environment.performance : polar,completed.route.legs);
    EXPECT_TRUE(replay.passed) << replay.failureReason;
    EXPECT_LE(completed.motionCalls,4000U);
    EXPECT_EQ(completed.route.engineIdentity,"quick3");
    EXPECT_NE(completed.route.message.find("Quick3"),std::string::npos);
    if (!f.request.vessel.propulsion.allowMotor) {
      for (const auto& leg : completed.route.legs)
        EXPECT_EQ(leg.propulsionMode,wr::PropulsionMode::Sail);
    }
    EXPECT_EQ(completed.route.environment.gribWindDuration,
              completed.route.validation.environment.gribWindDuration);
  }
}
TEST(QuickCompletion, RejectsInvalidEarlierPrefixAndDoesNotSnapToDestination) {
  auto f = quick2_fixtures::make("open");
  auto full = quick2_routing::Engine{}.route(f.request,f.environment).route;
  ASSERT_GT(full.legs.size(),1U);
  full.legs.resize(full.legs.size()-1);
  full.legs.front().start = wr::destinationPoint(f.request.start,0,5);
  auto entry = Prefix(f.request,std::move(full.legs));
  const auto* pointer = &entry;
  auto result=weather_routing::CompleteQuickPrefixes(f.request,f.environment,std::span{&pointer,1});
  EXPECT_FALSE(weather_routing::native::AcceptedRoute(result.route));
  EXPECT_TRUE(result.route.legs.empty());
  EXPECT_EQ(result.prefixesRejected,1U);
  EXPECT_EQ(result.motionCalls,0U);
}
TEST(QuickCompletion, ExhaustedChildAllowanceNeverCancelsTheParent) {
  auto f = quick2_fixtures::make("open");
  auto full = quick2_routing::Engine{}.route(f.request,f.environment).route;
  ASSERT_FALSE(full.legs.empty());
  auto entry = Prefix(f.request,std::move(full.legs));
  const auto* pointer=&entry;
  weather_routing::QuickCompletionOptions options;
  options.maximumMotionCalls=0;
  auto result=weather_routing::CompleteQuickPrefixes(f.request,f.environment,std::span{&pointer,1},options);
  EXPECT_TRUE(result.allowanceReached);
  EXPECT_FALSE(f.request.cancellation.cancelled());
  EXPECT_EQ(result.route.status,wr::RoutingStatus::SearchIncomplete);
  f.request.cancellation.cancel();
  result=weather_routing::CompleteQuickPrefixes(f.request,f.environment,std::span{&pointer,1});
  EXPECT_EQ(result.route.status,wr::RoutingStatus::Cancelled);
}

TEST(QuickCompletion, SequencePreservesRecoveredIdentityAndEarlierAttempts) {
  auto f=quick2_fixtures::make("open");
  auto full=quick2_routing::Engine{}.route(f.request,f.environment).route;
  ASSERT_GT(full.legs.size(),1U);
  full.legs.pop_back();
  auto entry=Prefix(f.request,std::move(full.legs));const auto* pointer=&entry;
  wr::CancellationToken cancel;
  auto result=weather_routing::native::RunRoutingEngineSequence(
      weather_routing::RoutingEngine::Auto,cancel,[](auto) {
        wr::RoutingResult failed;failed.status=wr::RoutingStatus::SearchIncomplete;return failed;
      },{},weather_routing::kSelectableEnginesMask,[&]() -> std::optional<wr::RoutingResult> {
        return weather_routing::CompleteQuickPrefixes(f.request,f.environment,std::span{&pointer,1}).route;
      });
  ASSERT_TRUE(weather_routing::native::AcceptedRoute(result));
  EXPECT_EQ(result.engineIdentity,"quick3");
  ASSERT_EQ(result.engineAttempts.size(),3U);
  EXPECT_EQ(result.engineAttempts.back().engine,"Quick3 recovery");
  EXPECT_EQ(result.attemptFailures.size(),2U);
}

TEST(QuickCompletion, MotorEnabledCompletionPreservesHardMotorTimeLimit) {
  auto f=quick2_fixtures::make("open");
  // A permitted sailing mode with a calm polar makes the first moving leg
  // switch to motor, so its manoeuvre delay must be excluded from motor time.
  f.request.vessel.propulsion.allowSailing=true;
  for (auto& row : f.request.vessel.profiles.front().rows)
    for (auto& point : row.points) point.boatSpeedKnots=0;
  f.request.vessel.propulsion.allowMotor=true;
  f.request.vessel.propulsion.motorBelowSailingSpeedKnots=2;
  f.request.vessel.propulsion.configuredMotorSpeedKnots=6;
  f.request.vessel.propulsion.modeChangePenalty=wr::Duration{1200};
  f.environment.performance=std::make_shared<wr::PolarPerformanceModel>(f.request.vessel);
  auto full=quick2_routing::Engine{}.route(f.request,f.environment).route;
  ASSERT_TRUE(weather_routing::native::AcceptedRoute(full)) << full.message;
  ASSERT_GT(full.legs.size(),1U);
  wr::Duration moving{};
  unsigned transitions=0;
  for (const auto& leg : full.legs) {
    if (leg.stationaryWait) continue;
    EXPECT_EQ(leg.propulsionMode,wr::PropulsionMode::Motor);
    moving += leg.endTime-leg.startTime;
    if (leg.propulsionTransition) { moving-=wr::Duration{1200}; ++transitions; }
  }
  ASSERT_GT(transitions,0U);
  // The independent validator conservatively counts the complete motor-leg
  // duration, including manoeuvres. Preserve that established contract.
  f.request.vessel.propulsion.maximumMotorTime=
      moving+wr::Duration{1200*transitions}+wr::Duration{120};
  full.legs.pop_back();
  auto entry=Prefix(f.request,std::move(full.legs));const auto* pointer=&entry;
  auto recovered=weather_routing::CompleteQuickPrefixes(f.request,f.environment,std::span{&pointer,1});
  EXPECT_TRUE(weather_routing::native::AcceptedRoute(recovered.route)) << recovered.route.message;
  f.request.vessel.propulsion.maximumMotorTime=moving-wr::Duration{120};
  auto denied=weather_routing::CompleteQuickPrefixes(f.request,f.environment,std::span{&pointer,1});
  EXPECT_FALSE(weather_routing::native::AcceptedRoute(denied.route));
}

TEST(QuickCompletion, AutoArchiveNeverCopiesOutOfRangePrefixes) {
  const wr::GeoPoint start{0,0},end{0,20};
  const wr::TimePoint departure{wr::Duration{1000}};
  wr::PartialRouteArchive archive(start,end,departure,wr::Duration{1000000},1024,16,40);
  unsigned copies=0;
  archive.offer({0,10},departure+wr::Duration{100},"far",[&]{++copies;return wr::PartialRouteTrace{};});
  EXPECT_EQ(copies,0U);
  archive.offer(wr::destinationPoint(end,270,5),departure+wr::Duration{100},"near",[&]{++copies;return wr::PartialRouteTrace{};});
  EXPECT_EQ(copies,1U);
}
