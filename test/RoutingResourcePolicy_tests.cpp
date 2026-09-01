#include <gtest/gtest.h>

#include "RoutingResourcePolicy.h"

TEST(RoutingResourcePolicy, ScalesCoupledFinalRouteLimits) {
  const auto standard =
      weather_routing::SelectRoutingResourcePolicy(100.0, 100, false);
  const auto extended =
      weather_routing::SelectRoutingResourcePolicy(100.0, 150, false);
  const auto thorough =
      weather_routing::SelectRoutingResourcePolicy(100.0, 200, false);
  const auto exhaustive =
      weather_routing::SelectRoutingResourcePolicy(100.0, 400, false);

  EXPECT_EQ(1145000U, standard.maximum_generated_states);
  EXPECT_EQ(20000U, standard.maximum_coastal_endpoint_generated_states);
  EXPECT_EQ(675000U, standard.maximum_forward_generated_states);
  EXPECT_EQ(512U, standard.maximum_reverse_candidates);
  EXPECT_EQ(40960U, standard.maximum_reverse_bridge_attempts);
  EXPECT_EQ(225000U, standard.maximum_frontier_recovery_generated_states);
  EXPECT_EQ(90000U, standard.maximum_frontier_recovery_labels);
  EXPECT_EQ(225000U, standard.maximum_graph_generated_states);
  EXPECT_EQ(70000U, standard.maximum_retained_states);
  EXPECT_EQ(180000U, standard.maximum_graph_labels);
  EXPECT_EQ(1717500U, extended.maximum_generated_states);
  EXPECT_EQ(30000U, extended.maximum_coastal_endpoint_generated_states);
  EXPECT_EQ(105000U, extended.maximum_retained_states);
  EXPECT_EQ(270000U, extended.maximum_graph_labels);
  EXPECT_EQ(2290000U, thorough.maximum_generated_states);
  EXPECT_EQ(40000U, thorough.maximum_coastal_endpoint_generated_states);
  EXPECT_EQ(140000U, thorough.maximum_retained_states);
  EXPECT_EQ(360000U, thorough.maximum_graph_labels);
  EXPECT_EQ(4580000U, exhaustive.maximum_generated_states);
  EXPECT_EQ(80000U, exhaustive.maximum_coastal_endpoint_generated_states);
  EXPECT_EQ(2048U, exhaustive.maximum_reverse_candidates);
  EXPECT_EQ(163840U, exhaustive.maximum_reverse_bridge_attempts);
  EXPECT_EQ(280000U, exhaustive.maximum_retained_states);
  EXPECT_EQ(720000U, exhaustive.maximum_graph_labels);
}

TEST(RoutingResourcePolicy, ClampsEffortAndDistance) {
  const auto minimum =
      weather_routing::SelectRoutingResourcePolicy(1.0, 1, false);
  const auto maximum =
      weather_routing::SelectRoutingResourcePolicy(1000.0, 999, false);

  EXPECT_EQ(695000U, minimum.maximum_generated_states);
  EXPECT_EQ(18080000U, maximum.maximum_generated_states);
  EXPECT_EQ(1120000U, maximum.maximum_retained_states);
  EXPECT_EQ(2880000U, maximum.maximum_graph_labels);
}

TEST(RoutingResourcePolicy, NormalizesToSupportedEffortLevels) {
  EXPECT_EQ(100, weather_routing::NormalizeRoutingEffortPercent(1));
  EXPECT_EQ(100, weather_routing::NormalizeRoutingEffortPercent(125));
  EXPECT_EQ(150, weather_routing::NormalizeRoutingEffortPercent(126));
  EXPECT_EQ(150, weather_routing::NormalizeRoutingEffortPercent(175));
  EXPECT_EQ(200, weather_routing::NormalizeRoutingEffortPercent(176));
  EXPECT_EQ(200, weather_routing::NormalizeRoutingEffortPercent(300));
  EXPECT_EQ(400, weather_routing::NormalizeRoutingEffortPercent(301));
  EXPECT_EQ(400, weather_routing::NormalizeRoutingEffortPercent(999));
}

TEST(RoutingResourcePolicy, HigherEffortContainsExactIntegerBaseline) {
  const auto standard =
      weather_routing::SelectRoutingResourcePolicy(100.00007, 100, false);
  const auto exhaustive =
      weather_routing::SelectRoutingResourcePolicy(100.00007, 400, false);

  EXPECT_EQ(standard.maximum_forward_generated_states,
            (exhaustive.maximum_forward_generated_states + 2U) / 4U);
  EXPECT_EQ(standard.maximum_coastal_endpoint_generated_states,
            (exhaustive.maximum_coastal_endpoint_generated_states + 2U) /
                4U);
  EXPECT_EQ(standard.maximum_reverse_candidates,
            (exhaustive.maximum_reverse_candidates + 2U) / 4U);
  EXPECT_EQ(standard.maximum_frontier_recovery_generated_states,
            (exhaustive.maximum_frontier_recovery_generated_states + 2U) /
                4U);
  EXPECT_EQ(standard.maximum_graph_generated_states,
            (exhaustive.maximum_graph_generated_states + 2U) / 4U);
}

TEST(RoutingResourcePolicy, ScoutUsesDeterministicWorkIndependentOfEffort) {
  const auto standard =
      weather_routing::SelectRoutingResourcePolicy(137.5, 100, true);
  const auto exhaustive =
      weather_routing::SelectRoutingResourcePolicy(137.5, 400, true);

  EXPECT_EQ(86500U, standard.maximum_generated_states);
  EXPECT_EQ(4000U, standard.maximum_coastal_endpoint_generated_states);
  EXPECT_EQ(0U, standard.maximum_reverse_candidates);
  EXPECT_EQ(0U, standard.maximum_reverse_bridge_attempts);
  EXPECT_EQ(13750U, standard.maximum_retained_states);
  EXPECT_EQ(1U, standard.maximum_graph_labels);
  EXPECT_EQ(standard.maximum_generated_states,
            exhaustive.maximum_generated_states);
  EXPECT_EQ(standard.maximum_retained_states,
            exhaustive.maximum_retained_states);
}

TEST(RoutingResourcePolicy, ShortRoutesBeginWithFocusedGraphCorridor) {
  EXPECT_DOUBLE_EQ(6.0,
                   weather_routing::SelectInitialGraphCorridorWidthNm(10.0));
  EXPECT_DOUBLE_EQ(8.0,
                   weather_routing::SelectInitialGraphCorridorWidthNm(40.0));
  EXPECT_DOUBLE_EQ(20.0,
                   weather_routing::SelectInitialGraphCorridorWidthNm(100.0));
  EXPECT_DOUBLE_EQ(20.0,
                   weather_routing::SelectInitialGraphCorridorWidthNm(500.0));
  EXPECT_DOUBLE_EQ(
      20.0, weather_routing::SelectInitialGraphCorridorWidthNm(NAN));
}
