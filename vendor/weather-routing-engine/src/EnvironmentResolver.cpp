#include "RoutingInternal.h"

namespace supercpn::weather_routing::internal {
namespace {
RoutingWarning warning(RoutingWarningCode code, std::string message) {
  return {code, std::move(message)};
}
}  // namespace

ResolvedEnvironment resolveEnvironment(const RoutingRequest& request,
                                       const RoutingEnvironment& environment,
                                       GeoPoint position, TimePoint time) {
  ResolvedEnvironment result;
  if (request.environment.useWind) {
    if (environment.grib)
      result.snapshot.wind = environment.grib->wind(position, time);
    if (!result.snapshot.wind.available && environment.climatology &&
        request.environment.climatology !=
            ClimatologyFallbackPolicy::Disallow &&
        (request.environment.climatology !=
             ClimatologyFallbackPolicy::RequireExplicitAcknowledgement ||
         request.environment.climatologyAcknowledged)) {
      result.snapshot.wind = environment.climatology->wind(position, time);
      if (result.snapshot.wind.available) {
        result.snapshot.wind.metadata.source = EnvironmentalSource::Climatology;
        if (result.snapshot.wind.metadata.fallbackReason.empty())
          result.snapshot.wind.metadata.fallbackReason =
              "GRIB wind unavailable";
        result.warnings.push_back(
            warning(RoutingWarningCode::ClimatologyWindUsed,
                    "climatological wind replaced unavailable GRIB wind"));
      }
    }
    if (!result.snapshot.wind.available) {
      result.failureStatus = RoutingStatus::WindForecastRequired;
      result.failureReason = "no authorised wind source at route sample";
      return result;
    }
  }

  if (request.environment.useCurrent) {
    if (environment.grib)
      result.snapshot.current = environment.grib->current(position, time);
    if (!result.snapshot.current.available && environment.xtdCurrent)
      result.snapshot.current = environment.xtdCurrent->sample(position, time);
    if (!result.snapshot.current.available &&
        request.environment.missingCurrent != MissingCurrentPolicy::Disallow &&
        (request.environment.missingCurrent !=
             MissingCurrentPolicy::RequireExplicitAcknowledgement ||
         request.environment.zeroCurrentAcknowledged)) {
      result.snapshot.current.available = true;
      result.snapshot.current.velocity = {};
      result.snapshot.current.metadata = {
          EnvironmentalSource::NoDataAssumedZero,
          "explicit-zero-current",
          "no current model",
          {},
          time,
          {},
          "no current source available"};
      result.warnings.push_back(
          warning(RoutingWarningCode::CurrentAssumedZero,
                  "current was explicitly assumed to be zero"));
    }
    if (!result.snapshot.current.available) {
      result.failureStatus = RoutingStatus::CurrentDataRequired;
      result.failureReason = "no authorised current source at route sample";
      return result;
    }
  } else {
    result.snapshot.current.available = true;
    result.snapshot.current.velocity = {};
    result.snapshot.current.metadata = {EnvironmentalSource::NoDataAssumedZero,
                                        "current-disabled",
                                        "current disabled by request",
                                        {},
                                        time,
                                        {},
                                        "current disabled"};
  }

  if (request.environment.useWaves && environment.grib)
    result.snapshot.waves = environment.grib->waves(position, time);
  if (request.constraints.maximumWaveHeightMetres &&
      !result.snapshot.waves.available) {
    const bool allowed =
        request.environment.missingWaves !=
            MissingWavePolicy::DisallowWhenConstrained &&
        (request.environment.missingWaves !=
             MissingWavePolicy::RequireExplicitAcknowledgement ||
         request.environment.missingWavesAcknowledged);
    if (!allowed) {
      result.failureStatus = RoutingStatus::WaveDataRequired;
      result.failureReason =
          "wave constraint is active but no authorised wave data exists";
      return result;
    }
    result.warnings.push_back(
        warning(RoutingWarningCode::WaveDataMissing,
                "wave data is unavailable and was explicitly waived"));
  }
  return result;
}

TransitionReason transitionReason(EnvironmentalSource previous,
                                  EnvironmentalSource next) {
  if (previous == EnvironmentalSource::Missing)
    return TransitionReason::InitialSource;
  if (next == EnvironmentalSource::NoDataAssumedZero)
    return TransitionReason::ExplicitZeroAssumption;
  if ((previous == EnvironmentalSource::Climatology &&
       next == EnvironmentalSource::GribForecast) ||
      (previous == EnvironmentalSource::XtdCurrentPrediction &&
       next == EnvironmentalSource::GribForecast))
    return TransitionReason::HigherPrioritySourceRestored;
  return TransitionReason::ParameterUnavailable;
}

void appendTransitions(std::vector<EnvironmentalSourceTransition>& transitions,
                       const EnvironmentalSnapshot* previous,
                       const EnvironmentalSnapshot& current, GeoPoint position,
                       TimePoint time, const EnvironmentalPolicy& policy) {
  const auto add = [&](EnvironmentalVariable variable,
                       EnvironmentalSource before, EnvironmentalSource after,
                       bool acknowledgement) {
    if (before == after) return;
    transitions.push_back({variable, before, after, position, time,
                           transitionReason(before, after), acknowledgement});
  };
  const EnvironmentalSource missing = EnvironmentalSource::Missing;
  add(EnvironmentalVariable::Wind,
      previous ? previous->wind.metadata.source : missing,
      current.wind.available ? current.wind.metadata.source : missing,
      current.wind.metadata.source == EnvironmentalSource::Climatology &&
          policy.climatology ==
              ClimatologyFallbackPolicy::RequireExplicitAcknowledgement);
  add(EnvironmentalVariable::Current,
      previous ? previous->current.metadata.source : missing,
      current.current.available ? current.current.metadata.source : missing,
      current.current.metadata.source ==
              EnvironmentalSource::NoDataAssumedZero &&
          policy.missingCurrent ==
              MissingCurrentPolicy::RequireExplicitAcknowledgement);
  add(EnvironmentalVariable::Waves,
      previous ? previous->waves.metadata.source : missing,
      current.waves.available ? current.waves.metadata.source : missing,
      !current.waves.available &&
          policy.missingWaves ==
              MissingWavePolicy::RequireExplicitAcknowledgement);
}

}  // namespace supercpn::weather_routing::internal
