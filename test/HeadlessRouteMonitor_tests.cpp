#include <gtest/gtest.h>

#include "headless/HeadlessRouteMonitor.h"

namespace {

using weather_routing_headless::EvaluateHeadlessRouteMonitor;
using weather_routing_headless::HeadlessRouteMonitorDecision;

TEST(HeadlessRouteMonitor, CompletesAsSoonAsRouteWorkIsIdle) {
  EXPECT_EQ(EvaluateHeadlessRouteMonitor(false, 25, 1000),
            HeadlessRouteMonitorDecision::Complete);
}

TEST(HeadlessRouteMonitor, ContinuesActiveWorkBeforeDeadline) {
  EXPECT_EQ(EvaluateHeadlessRouteMonitor(true, 999, 1000),
            HeadlessRouteMonitorDecision::Continue);
}

TEST(HeadlessRouteMonitor, TimesOutActiveWorkAtDeadline) {
  EXPECT_EQ(EvaluateHeadlessRouteMonitor(true, 1000, 1000),
            HeadlessRouteMonitorDecision::Timeout);
  EXPECT_EQ(EvaluateHeadlessRouteMonitor(true, 1001, 1000),
            HeadlessRouteMonitorDecision::Timeout);
}

}  // namespace
