// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "ValidatedRouteReporting.h"

namespace wr = supercpn::weather_routing;
TEST(ValidatedRouteReporting, PreservesSourceChangeInsideOneSearchLeg) {
  wr::RoutingResult r;
  r.status = wr::RoutingStatus::Complete;
  r.validation.passed = true;
  r.legs.resize(1);
  r.legs[0].windSource.source = wr::EnvironmentalSource::GribForecast;
  r.environment.gribWindDuration = wr::Duration{7200};
  r.validation.environment.gribWindDuration = wr::Duration{3600};
  r.validation.environment.climatologyWindDuration = wr::Duration{3600};
  r.validation.sourceTransitions.resize(2);
  weather_routing::UseValidatedSourceReporting(r);
  EXPECT_EQ(r.environment.gribWindDuration, wr::Duration{3600});
  EXPECT_EQ(r.environment.climatologyWindDuration, wr::Duration{3600});
  EXPECT_EQ(r.sourceTransitions.size(), 2U);
  EXPECT_EQ(r.legs[0].windSource.source, wr::EnvironmentalSource::GribForecast);
}
TEST(ValidatedRouteReporting, DoesNotRewriteUnvalidatedOrCancelledResults) {
  for (auto status : {wr::RoutingStatus::Complete, wr::RoutingStatus::Cancelled}) {
    wr::RoutingResult r;
    r.status = status;
    r.validation.passed = status == wr::RoutingStatus::Cancelled;
    r.legs.resize(1);
    r.environment.gribWindDuration = wr::Duration{30};
    weather_routing::UseValidatedSourceReporting(r);
    EXPECT_EQ(r.environment.gribWindDuration, wr::Duration{30});
  }
}
