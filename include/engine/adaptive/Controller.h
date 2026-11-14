// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "RoutingEngineSettings.h"
#include "engine/native/RoutingEngineSequence.h"
#include "WorkForecast.h"
#include <functional>
#include <optional>
#include <set>

namespace weather_routing::adaptive {
inline constexpr unsigned kPolicyRevision = 1;
enum class Objective { FirstAccepted, BestCompleted };
enum class StopCause { Other, RepeatedChronologicalRepair };
enum class Role {
  Initial,
  Saved,
  Refinement,
  ReplayRecovery,
  QualityTimeAngle,
  QualityHeading,
  StandardComparison
};
struct Recipe {
  wr::Duration step{};
  double heading{};
  wr::Duration standardStep{};
  double standardHeading{};
  Role role{Role::Initial};
  std::string reason;
};
struct MemoryContext {
  bool known{};
  std::uint64_t availableMiB{}, reserveMiB{}, sharedCacheMiB{}, searchMiB{};
  bool AllowsExtraWork() const {
    return known && searchMiB >= 128 &&
           availableMiB > reserveMiB + sharedCacheMiB + searchMiB + 64;
  }
};
struct Options {
  RoutingEngine engine{RoutingEngine::Original};
  Objective objective{Objective::FirstAccepted};
  std::uint64_t nativeAllowance{};
  // Zero means no explicit user counter ceiling. The engine's internal
  // distance/effort-derived guard is separate and may be adjusted by policy.
  std::uint64_t hardWorkCeiling{};
  wr::Duration standardStep{10800};
  double standardHeading{10};
  MemoryContext memory;
  std::function<std::uint64_t()> availableMemoryMiB;
  std::function<bool()> userCancelled;
};
struct AttemptResult {
  wr::RoutingResult route;
  std::optional<std::uint64_t> quotaUsed;
  StopCause stopCause{StopCause::Other};
};
struct AttemptAudit {
  Recipe recipe;
  wr::RoutingStatus status{};
  bool accepted{}, selected{};
  std::uint64_t quota{};
  wr::Duration passage{};
  double milliseconds{};
};
struct Result {
  wr::RoutingResult route;
  std::vector<AttemptAudit> attempts;
  std::optional<Recipe> selected;
  std::uint64_t allowance{}, spent{}, standardFirstPhase{};
  double firstAcceptedMilliseconds{};
  std::string stopReason;
};
using Clock = std::chrono::steady_clock;
using Solve = std::function<AttemptResult(const wr::RoutingRequest&,
                                          const Recipe&, std::uint64_t,
                                          std::uint64_t, const StepSchedule&)>;
using Accept = std::function<bool(wr::RoutingResult&)>;

inline std::vector<Recipe> InitialRecipes(const Options& options,
                                          const wr::RoutingRequest& request,
                                          const Features& f,
                                          const StepSchedule& schedule) {
  std::vector<Recipe> out;
  auto add = [&](wr::Duration step, double angle, Role role, const char* reason,
                 wr::Duration standardStep = {}, double standardAngle = 0) {
    for (const auto& old : out)
      if (old.step == step && old.heading == angle &&
          old.standardStep == standardStep &&
          old.standardHeading == standardAngle)
        return;
    out.push_back({step, angle, standardStep, standardAngle, role, reason});
  };
  const bool hourly =
      request.environment.useCurrent &&
      (f.rapidCurrent || (f.coastalFraction > .2 && !f.currentAtStart));
  const auto saved = [&] {
    add(request.options.timeStep, request.options.headingStepDegrees,
        Role::Saved, "Saved numerical inputs; original physical constraints");
  };
  if (options.engine == RoutingEngine::Original) {
    if (!f.windAtStart) {
      saved();
      return out;
    }
    if (hourly) {
      add(std::min(wr::Duration{3600}, request.options.timeStep),
          request.options.headingStepDegrees, Role::Initial,
          "Hourly or finer current regime; native endpoint reductions "
          "retained");
      add(wr::Duration{3600}, 10, Role::Refinement,
          "Alternate hourly heading lattice");
    } else if (f.offshore) {
      add(wr::Duration{f.directNm >= 800 ? 21600 : 10800}, 20, Role::Initial,
          "Offshore first attempt; native endpoint reductions retained");
      add(wr::Duration{10800}, 15, Role::Refinement,
          "Offshore time/heading alternative");
      add(wr::Duration{3600}, 10, Role::Refinement,
          "Finer optional numerical comparison");
    } else if (f.chordBlocked || f.coastalFraction > .2 || !f.sketchResolved ||
               f.windChange3h > 3 || f.currentSpatialDelta > 1) {
      saved();
      add(wr::Duration{3600}, 10, Role::Refinement,
          "Alternate lattice under environmental uncertainty");
    } else {
      const bool edge = f.twa < 50 || f.twa > 155;
      add(wr::Duration{edge ? 21600 : 10800}, edge ? 20 : 10, Role::Initial,
          "Open-water polar alignment; native endpoint reductions retained");
      add(wr::Duration{3600}, 10, Role::Refinement,
          "Finer optional numerical comparison");
    }
    saved();
  } else if (options.engine == RoutingEngine::Quick2) {
    const auto hourlyCost = Quick2Work(f, schedule, wr::Duration{3600}, true);
    const bool geometryConcern = f.chordBlocked || f.detourRatio > 1.15 ||
                                 f.coastalFraction > .15 ||
                                 hourlyCost.hourlyFraction > .35;
    int selected = 21600;
    bool known = false;
    for (int seconds : {3600, 10800, 21600}) {
      const auto cost = Quick2Work(f, schedule, wr::Duration{seconds}, true);
      if (!cost.available) break;
      known = true;
      selected = seconds;
      if (cost.proposals <= options.nativeAllowance * .70) break;
    }
    if (geometryConcern && hourlyCost.available &&
        hourlyCost.proposals <= options.nativeAllowance * 1.25)
      selected = std::min<std::int64_t>(3600, request.options.timeStep.count());
    if (!known) {
      saved();
      return out;
    }
    add(wr::Duration{selected}, request.options.headingStepDegrees,
        Role::Initial,
        "Nominal step from VMG and frontier-work estimate; local hourly map "
        "active");
    if (selected > 3600)
      add(wr::Duration{selected == 21600 ? 10800 : 3600},
          request.options.headingStepDegrees, Role::Refinement,
          "Optional temporal refinement");
    saved();
  } else {
    saved();
    if (options.engine == RoutingEngine::Quick && hourly &&
        options.objective == Objective::BestCompleted) {
      for (double heading : {10., 20.}) {
        const bool sameStep =
            options.standardStep == wr::Duration{3600} ||
            (f.directNm < 120 && options.standardStep >= wr::Duration{3600});
        if (sameStep && heading == options.standardHeading) continue;
        add(request.options.timeStep, request.options.headingStepDegrees,
            Role::StandardComparison,
            "Bounded Standard current-field comparison", wr::Duration{3600},
            heading);
      }
    }
  }
  return out;
}

inline Result Run(const Options& options, const wr::RoutingRequest& original,
                  const Features& f, const Solve& solve,
                  const Accept& accept = {},
                  Clock::time_point deadline = Clock::time_point::max()) {
  const auto started = Clock::now();
  const bool first = options.objective == Objective::FirstAccepted;
  StepSchedule schedule(f, original);
  auto choices = InitialRecipes(options, original, f, schedule);
  const auto base = options.nativeAllowance;
  Result output;
  output.allowance = base;
  if (options.engine == RoutingEngine::Quick2 &&
      options.memory.AllowsExtraWork() && !choices.empty()) {
    const auto cost = Quick2Work(f, schedule, choices.front().step, true);
    // Unresolved sketches still carry measured coastal/variability evidence.
    // Preserve the qualified fallback admission when no frontier estimate is
    // available, rather than silently reducing it to the native allowance.
    const double fallback = std::clamp(
        std::max(1., f.hours / 24.) *
            (1. + f.coastalFraction + std::max(0., f.detourRatio - 1.) +
             std::min(1., f.windChange3h / 10.)),
        1., 4.);
    const double factor =
        cost.available
            ? std::clamp(cost.proposals / std::max(1., base * .70), 1., 4.)
            : fallback;
    output.allowance = static_cast<std::uint64_t>(base * factor);
  }
  if (options.engine == RoutingEngine::Quick &&
      options.memory.AllowsExtraWork() &&
      (f.chordBlocked || f.detourRatio > 1.15 || f.coastalFraction > .15)) {
    output.standardFirstPhase = base * 2 / 3;
    output.allowance = base + base / 2;
  }
  if (options.hardWorkCeiling)
    output.allowance = std::min(output.allowance, options.hardWorkCeiling);
  if (output.allowance <= base) output.standardFirstPhase = 0;
  bool have = false, replayUsed = false, qualityPrepared = false;
  struct Probe {
    Recipe recipe;
    double work{}, milliseconds{};
  };
  std::vector<Probe> probes;
  output.route.status = wr::RoutingStatus::ResourceLimitReached;
  output.route.message = "Automatic routing allowance ended before an attempt";
  const auto underPressure = [&] {
    if (!options.availableMemoryMiB) return false;
    const auto available = options.availableMemoryMiB();
    return available && available < options.memory.reserveMiB + 64;
  };
  const auto cancelled = [&] {
    if (options.userCancelled) return options.userCancelled();
    // A screening deadline is not an instruction to discard an incumbent.
    return Clock::now() < deadline && original.cancellation.cancelled();
  };
  for (std::size_t index = 0; index < choices.size(); ++index) {
    if (cancelled()) {
      output.route = {};
      output.route.status = wr::RoutingStatus::Cancelled;
      output.route.message = "Routing stopped";
      break;
    }
    if (Clock::now() >= deadline || output.spent >= output.allowance) break;
    const auto remaining = output.allowance - output.spent;
    if (have && (first || remaining < base / 8 || underPressure())) break;
    const auto recipe = choices[index];
    if (options.engine == RoutingEngine::Quick2 && have) {
      const auto estimate = Quick2Work(f, schedule, recipe.step, true);
      if (estimate.available && estimate.proposals > remaining * .85) break;
    }
    auto request = original;
    request.options.timeStep = recipe.step;
    request.options.headingStepDegrees = recipe.heading;
    const bool standardEquivalent =
        options.engine == RoutingEngine::Quick &&
        (!recipe.standardHeading ||
         recipe.standardHeading == options.standardHeading) &&
        (recipe.standardStep == wr::Duration{} ||
         recipe.standardStep == options.standardStep ||
         (f.directNm < 120 && recipe.standardStep >= wr::Duration{3600} &&
          options.standardStep >= wr::Duration{3600}));
    const bool saved = standardEquivalent ||
                       (recipe.step == original.options.timeStep &&
                        recipe.heading == original.options.headingStepDegrees &&
                        recipe.standardStep == wr::Duration{});
    const auto attemptStarted = Clock::now();
    auto attemptDeadline = deadline;
    if (deadline != Clock::time_point::max() && index + 1 < choices.size() &&
        !have && !saved)
      attemptDeadline = attemptStarted + (deadline - attemptStarted) * 2 / 3;
    request.cancellation =
        original.cancellation.bounded(attemptDeadline, nullptr);
    auto outcome =
        solve(request, recipe, remaining, output.standardFirstPhase, schedule);
    const auto elapsed =
        std::chrono::duration<double, std::milli>(Clock::now() - attemptStarted)
            .count();
    bool valid = native::AcceptedRoute(outcome.route);
    if (valid && accept)
      valid = accept(outcome.route) && native::AcceptedRoute(outcome.route);
    if (!valid && native::AcceptedRoute(outcome.route)) {
      outcome.route.validation.passed = false;
      outcome.route.status = wr::RoutingStatus::ValidationFailure;
    }
    const auto quota = outcome.quotaUsed.value_or(0);
    output.spent += quota;
    AttemptAudit audit{recipe, outcome.route.status,          valid,  false,
                       quota,  outcome.route.metrics.elapsed, elapsed};
    bool addedRecovery = false;
    if (options.engine == RoutingEngine::Original && saved && !valid &&
        !replayUsed &&
        outcome.stopCause == StopCause::RepeatedChronologicalRepair &&
        recipe.step > original.options.minimumTimeStep &&
        output.spent < output.allowance) {
      auto smaller =
          std::max(original.options.minimumTimeStep, recipe.step / 2);
      const bool seen =
          std::any_of(choices.begin(), choices.end(), [&](const auto& p) {
            return p.step == smaller && p.heading == recipe.heading;
          });
      if (!seen) {
        choices.insert(choices.begin() + index + 1,
                       {smaller,
                        recipe.heading,
                        {},
                        0,
                        Role::ReplayRecovery,
                        "Smaller nominal step after repeated chronological "
                        "repair failures"});
        replayUsed = addedRecovery = true;
      }
    }
    if (!first && options.engine == RoutingEngine::Original && saved && valid &&
        !qualityPrepared && original.environment.useCurrent && f.hours > 0 &&
        recipe.step > original.options.minimumTimeStep && quota > 0) {
      qualityPrepared = true;
      const auto smaller =
          std::max(original.options.minimumTimeStep, recipe.step / 2);
      const double wanted = recipe.heading < 10   ? 10
                            : recipe.heading < 15 ? 15
                            : recipe.heading < 20 ? 20
                                                  : recipe.heading;
      const double angleLimit =
          2 *
          std::asin(std::min(
              1., double(recipe.step.count()) / smaller.count() *
                      std::sin(recipe.heading * std::numbers::pi / 360.))) *
          180 / std::numbers::pi;
      const double angle = std::min(wanted, angleLimit);
      const double factor =
          std::pow(double(recipe.step.count()) / smaller.count(), 3) *
          std::pow(recipe.heading / angle, 2) * 1.25;
      if (f.hours <= 24)
        probes.push_back({{smaller,
                           angle,
                           {},
                           0,
                           Role::QualityTimeAngle,
                           "Bounded coupled time/angle comparison"},
                          quota * factor,
                          elapsed * factor});
      if (angle != recipe.heading) {
        const double angleFactor = std::pow(recipe.heading / angle, 2) * 1.25;
        probes.push_back({{recipe.step,
                           angle,
                           {},
                           0,
                           Role::QualityHeading,
                           "Bounded alternate heading lattice"},
                          quota * angleFactor,
                          elapsed * angleFactor});
      }
    }
    const bool had = have;
    if (valid && (!have || outcome.route.metrics.elapsed <
                               output.route.metrics.elapsed)) {
      if (!have)
        output.firstAcceptedMilliseconds =
            std::chrono::duration<double, std::milli>(Clock::now() - started)
                .count();
      output.route = std::move(outcome.route);
      output.selected = recipe;
      have = audit.selected = true;
    } else if (!have)
      output.route = std::move(outcome.route);
    output.attempts.push_back(audit);
    if (first && have) {
      output.stopReason = "First accepted route";
      break;
    }
    // A cancellation cancels the operation even when an earlier candidate
    // exists.
    if (cancelled()) {
      output.route = {};
      output.route.status = wr::RoutingStatus::Cancelled;
      output.route.message = "Routing stopped";
      break;
    }
    if (index + 1 < choices.size()) {
      auto reference = std::find_if(
          choices.begin() + index + 1, choices.end(), [&](const auto& p) {
            return p.step == original.options.timeStep &&
                   p.heading == original.options.headingStepDegrees &&
                   p.standardStep == wr::Duration{};
          });
      if (reference != choices.end() &&
          reference != choices.begin() + index + 1)
        std::rotate(choices.begin() + index + 1, reference, reference + 1);
    }
    auto extend = [&] {
      if (!have || output.spent >= output.allowance ||
          output.allowance - output.spent < base / 8 || underPressure())
        return false;
      while (!probes.empty()) {
        auto p = probes.front();
        probes.erase(probes.begin());
        const bool seen = std::any_of(
            choices.begin(), choices.begin() + index + 1, [&](const auto& old) {
              return p.recipe.step == old.step &&
                     p.recipe.heading == old.heading;
            });
        const double millisecondsLeft =
            deadline == Clock::time_point::max()
                ? std::numeric_limits<double>::infinity()
                : std::chrono::duration<double, std::milli>(deadline -
                                                            Clock::now())
                      .count();
        if (seen || p.work > (output.allowance - output.spent) * .85 ||
            p.milliseconds > millisecondsLeft * .75)
          continue;
        choices.resize(index + 1);
        choices.push_back(std::move(p.recipe));
        return true;
      }
      return false;
    };
    if (recipe.role == Role::QualityTimeAngle ||
        recipe.role == Role::QualityHeading) {
      if (extend()) continue;
      break;
    }
    const bool standardComparisons =
        options.engine == RoutingEngine::Quick && choices.size() > 1;
    if (!standardComparisons && have && saved && valid && f.hours > 0 &&
        output.route.metrics.elapsed.count() <= f.hours * 3600 * 1.05 &&
        !addedRecovery) {
      if (extend()) continue;
      break;
    }
    if (!standardComparisons && had && !audit.selected && !addedRecovery) {
      if (extend()) continue;
      break;
    }
    if (have && underPressure()) break;
    if (output.route.status == wr::RoutingStatus::WindForecastRequired ||
        options.engine == RoutingEngine::Main ||
        options.engine == RoutingEngine::Atlas ||
        options.engine == RoutingEngine::Alternative)
      break;
    if (index + 1 == choices.size()) extend();
  }
  return output;
}
}  // namespace weather_routing::adaptive
