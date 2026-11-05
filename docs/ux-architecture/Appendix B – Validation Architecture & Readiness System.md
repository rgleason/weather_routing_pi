# Appendix B – Validation Architecture & Readiness System
## WeatherRouting UX Architecture v0.9
### Draft

**Status:** Draft v0.9  
**Specification Owner:** WeatherRouting UX Architecture  
**Last Updated:** 2026-11-05  
**Implementation Status:** Ready For Phase 1 Development

---

# B.1 Purpose

The Validation System ensures that:

```text
All required inputs exist

Inputs are internally consistent

Inputs are compatible with routing constraints

Routing can execute safely

Users receive actionable feedback
```

Validation is the foundation for:

```text
Readiness

Compute Enablement

User Guidance

Error Reporting

Beginner Experience
```

The routing engine shall never be the first place that invalid configurations are detected.

---

# B.2 Architectural Overview

```text
Setting Changed
        ↓

Validation Engine
        ↓

Validation Results
        ↓

Readiness Evaluator
        ↓

Readiness State
        ↓

UI Updates
        ↓

Compute Enabled/Disabled
```

---

# B.3 Validation Goals

## VAL-001 Fail Early

Invalid configurations should be detected prior to compute.

---

## VAL-002 Explain Failures

Every failure shall provide:

```text
Problem

Cause

Suggested Action
```

---

## VAL-003 Support Beginners

Beginner users should see:

```text
Next Step:
Select a destination.
```

instead of:

```text
Error 1000
```

---

## VAL-004 Data Driven

Validation rules are defined independently of UI.

---

# B.4 Validation Severity

```cpp
enum class ValidationSeverity
{
    Info,
    Warning,
    Error,
    Critical
};
```

---

## Info

Route may compute normally.

Examples:

```text
High-resolution routing selected.
```

---

## Warning

Route may compute.

Examples:

```text
Forecast expires before route end.
```

---

## Error

Route cannot compute.

Examples:

```text
Destination missing.
```

---

## Critical

Unsafe or fatal condition.

Examples:

```text
Route crosses land.
```

---

# B.5 Validation Categories

```cpp
enum class ValidationCategory
{
    Route,
    Boat,
    Weather,
    Safety,
    Optimization,
    Analysis,
    Diagnostics,
    Beginner
};
```

---

# B.6 Validation Result

## Core Data Structure

```cpp
struct ValidationResult
{
    ValidationCode code;

    ValidationSeverity severity;

    ValidationCategory category;

    SettingId settingId;

    wxString title;

    wxString message;

    wxString suggestedAction;

    bool blocksCompute;

    int readinessPenalty;
};
```

---

## Example

```cpp
{
    3000,

    ValidationSeverity::Error,

    ValidationCategory::Weather,

    SettingId::WindSource,

    "No Weather Source",

    "No weather source is available.",

    "Enable Climatology or load a GRIB file.",

    true,

    20
};
```

---

# B.7 Validation Results Collection

```cpp
struct ValidationResults
{
    std::vector<ValidationResult> items;

    bool HasErrors() const;

    bool HasCritical() const;

    bool CanCompute() const;

    int CountWarnings() const;

    int CountErrors() const;

    int CountCritical() const;
};
```

---

# B.8 Validation Context

```cpp
struct ValidationContext
{
    const RouteModel& route;

    const BoatModel& boat;

    const WeatherModel& weather;

    const ConstraintModel& constraints;

    const OptimizationModel& optimization;
};
```

---

# B.9 Error Code Allocation

| Range | Category |
|---------|---------|
| 1000-1999 | Route |
| 2000-2999 | Boat |
| 3000-3999 | Weather |
| 4000-4999 | Safety |
| 5000-5999 | Optimization |
| 6000-6999 | Analysis |
| 7000-7999 | Diagnostics |
| 8000-8999 | Beginner Experience |
| 9000-9999 | Cross-Setting Validation |

---

# B.10 Route Validation Rules

| ID | Rule | Severity | Block |
|----|------|----------|--------|
| VAL-ROUTE-001 | StartPosition valid | Error | Yes |
| VAL-ROUTE-002 | DestinationPosition valid | Error | Yes |
| VAL-ROUTE-003 | DepartureTime valid | Error | Yes |
| VAL-ROUTE-004 | Route distance > 0 | Error | Yes |
| VAL-ROUTE-005 | RouteName valid | Warning | No |
| VAL-ROUTE-006 | ArrivalConstraint valid | Error | Yes |
| VAL-ROUTE-007 | Waypoints valid | Error | Yes |

---

# B.11 Boat Validation Rules

| ID | Rule | Severity | Block |
|----|------|----------|--------|
| VAL-BOAT-001 | Boat exists | Error | Yes |
| VAL-BOAT-002 | Polar loads | Error | Yes |
| VAL-BOAT-003 | Polar contains data | Error | Yes |
| VAL-BOAT-004 | Draft > 0 | Error | Yes |
| VAL-BOAT-005 | PerformanceFactor range | Warning | No |
| VAL-BOAT-006 | SailPlan valid | Warning | No |
| VAL-BOAT-007 | FuelBurnRate valid | Warning | No |

---

# B.12 Weather Validation Rules

| ID | Rule | Severity | Block |
|----|------|----------|--------|
| VAL-WX-001 | Climatology available | Error | Yes |
| VAL-WX-002 | GRIB loads | Error | Yes |
| VAL-WX-003 | Wind source valid | Error | Yes |
| VAL-WX-004 | Forecast covers route | Warning | No |
| VAL-WX-005 | Forecast horizon sufficient | Warning | No |
| VAL-WX-006 | Wave source valid | Warning | No |
| VAL-WX-007 | Current source valid | Warning | No |

---

# B.13 Safety Validation Rules

| ID | Rule | Severity | Block |
|----|------|----------|--------|
| VAL-SAFE-001 | MaximumWind valid | Error | Yes |
| VAL-SAFE-002 | MaximumWaveHeight valid | Error | Yes |
| VAL-SAFE-003 | MinimumDepth valid | Error | Yes |
| VAL-SAFE-004 | ShoreBuffer valid | Warning | No |
| VAL-SAFE-005 | RestrictedArea settings valid | Warning | No |

---

# B.14 Optimization Validation Rules

| ID | Rule | Severity | Block |
|----|------|----------|--------|
| VAL-OPT-001 | Resolution valid | Error | Yes |
| VAL-OPT-002 | TimeStep valid | Error | Yes |
| VAL-OPT-003 | SearchDepth valid | Warning | No |
| VAL-OPT-004 | IsochronInterval valid | Error | Yes |
| VAL-OPT-005 | CourseIncrement valid | Error | Yes |
| VAL-OPT-006 | Objective weights valid | Warning | No |

---

# B.15 Cross-Setting Validation Rules

These are the most important validations.

## Route Consistency

| ID | Rule |
|----|------|
| XVAL-ROUTE-001 | Start != Destination |
| XVAL-ROUTE-002 | ArrivalConstraint > DepartureTime |
| XVAL-ROUTE-003 | Waypoints unique |
| XVAL-ROUTE-004 | Multi-stop routes contain multiple destinations |

---

## Boat Consistency

| ID | Rule |
|----|------|
| XVAL-BOAT-001 | SelectedBoat has Polar |
| XVAL-BOAT-002 | Draft compatible with selected boat |
| XVAL-BOAT-003 | SailPlan valid for boat type |

---

## Weather Consistency

| ID | Rule |
|----|------|
| XVAL-WX-001 | At least one wind source enabled |
| XVAL-WX-002 | Weather covers departure |
| XVAL-WX-003 | Weather covers route duration |
| XVAL-WX-004 | Weather covers destination |

---

## Safety Consistency

| ID | Rule |
|----|------|
| XVAL-SAFE-001 | MinimumDepth > Draft |
| XVAL-SAFE-002 | MaximumGust ≥ MaximumWind |
| XVAL-SAFE-003 | Route does not cross land |
| XVAL-SAFE-004 | Route respects shore buffer |

---

## Optimization Consistency

| ID | Rule |
|----|------|
| XVAL-OPT-001 | TimeStep ≤ IsochronInterval |
| XVAL-OPT-002 | SearchDepth > 0 |
| XVAL-OPT-003 | Objective weights sum > 0 |
| XVAL-OPT-004 | Objective weights reasonable |

---

# B.16 Validation Engine Architecture

## Interface

```cpp
class IValidator
{
public:

    virtual ~IValidator() = default;

    virtual void Validate(
        const ValidationContext& ctx,
        ValidationResults& results) = 0;
};
```

---

## Validators

```text
RouteValidator

BoatValidator

WeatherValidator

SafetyValidator

OptimizationValidator

BeginnerValidator

CrossSettingValidator
```

---

# B.17 Validation Engine

```cpp
class ValidationEngine
{
public:

    ValidationResults Evaluate(
        const ValidationContext& ctx);
};
```

---

## Execution Flow

```text
Route Validators
       ↓

Boat Validators
       ↓

Weather Validators
       ↓

Safety Validators
       ↓

Optimization Validators
       ↓

Cross Validators
       ↓

Results
```

---

# B.18 Validation Pseudocode

## Main Flow

```cpp
ValidationResults ValidationEngine::Evaluate(
    const ValidationContext& ctx)
{
    ValidationResults results;

    ValidateRoute(ctx, results);
    ValidateBoat(ctx, results);
    ValidateWeather(ctx, results);
    ValidateSafety(ctx, results);
    ValidateOptimization(ctx, results);

    ValidateCrossRules(ctx, results);

    return results;
}
```

---

## Example

```cpp
if(!ctx.route.HasDestination())
{
    results.AddError(
        1000,
        "Destination Required");
}
```

---

## Weather Example

```cpp
if(!ctx.weather.gribEnabled &&
   !ctx.weather.climatologyEnabled)
{
    results.AddError(
        3000,
        "No Weather Source");
}
```

---

## Safety Example

```cpp
if(ctx.constraints.minimumDepth
    <= ctx.boat.draft)
{
    results.AddError(
        9303,
        "Minimum depth less than draft");
}
```

---

# B.19 Readiness Architecture

Validation results feed readiness scoring.

---

## Readiness Categories

| Category | Max Score |
|-----------|------------|
| Route | 20 |
| Boat | 20 |
| Weather | 20 |
| Safety | 20 |
| Optimization | 20 |

Maximum score:

```text
100
```

---

# B.20 Readiness State

```cpp
struct ReadinessState
{
    bool computable;

    int score;

    ValidationSummary summary;

    wxString nextRecommendedAction;

    ValidationResults validationResults;
};
```

---

# B.21 Penalty Model

| Severity | Penalty |
|-----------|---------|
| Info | 0 |
| Warning | -2 |
| Error | Category = 0 |
| Critical | Score = 0 |

---

# B.22 Compute Rules

Compute enabled when:

```cpp
!HasErrors()
&&
!HasCritical()
```

---

# B.23 Beginner Guidance Engine

Validation drives user coaching.

Example:

```cpp
if(results.Contains(1000))
{
    return _("Select a destination.");
}
```

Another example:

```cpp
if(results.Contains(3000))
{
    return _("Enable Climatology.");
}
```

---

# B.24 Validation Display Levels

## Beginner

```text
Boat Ready ✅

Weather Ready ✅

Destination Missing ❌

Next Step:
Select a destination.
```

---

## Intermediate

```text
Route ✅

Boat ✅

Weather ⚠

Ready With Warnings
```

---

## Advanced

```text
VAL-WX-004

Forecast Coverage Insufficient

Recommended Action:
Load Extended Forecast
```

---

# B.25 Completion Criteria

Validation Architecture is complete when:

```text
Every SettingId
      ↓

Validation Rule
      ↓

Validation Result
      ↓

Readiness Impact
      ↓

User Guidance
```

and no route computation can occur without first passing through the Validation Engine.

This appendix serves as the authoritative specification for validation, readiness, compute enablement, and user guidance throughout the WeatherRouting system.