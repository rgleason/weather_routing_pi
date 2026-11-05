# Appendix G – C++ Class Architecture & Component Design
## WeatherRouting UX Architecture v0.9
### Draft

---

# G.1 Purpose

Appendix G defines the implementation architecture connecting:

```text
UI
 ↓

Settings
 ↓

Validation
 ↓

Readiness
 ↓

Routing Engine
 ↓

Persistence
```

This architecture is designed to:

- Minimize coupling
- Support Beginner / Intermediate / Advanced modes
- Preserve existing routing functionality
- Enable Android support
- Enable automated testing
- Support future expansion

---

# G.2 Architectural Principles

## ARCH-001 Model Driven

All UI state derives from models.

```text
Model
 ↓

Validation
 ↓

Readiness
 ↓

UI
```

Business rules shall never live directly inside dialogs.

---

## ARCH-002 Single Source of Truth

Every setting has exactly one owner.

```text
SettingId
      ↓
SettingDefinition
      ↓
Stored Value
```

No duplicated state.

---

## ARCH-003 Centralized Validation

All validation originates from:

```cpp
ValidationEngine
```

No dialog-specific validation.

---

## ARCH-004 Registry Driven

Visibility, persistence, validation and defaults are all driven by:

```cpp
SettingDefinition
```

---

# G.3 Top-Level Architecture

```text
┌─────────────────────────────┐
│          UI Layer           │
└─────────────┬───────────────┘
              │
              ▼

┌─────────────────────────────┐
│     Experience Profiles     │
└─────────────┬───────────────┘
              │
              ▼

┌─────────────────────────────┐
│      Settings Registry      │
└─────────────┬───────────────┘
              │
              ▼

┌─────────────────────────────┐
│      Validation Engine      │
└─────────────┬───────────────┘
              │
              ▼

┌─────────────────────────────┐
│      Readiness Engine       │
└─────────────┬───────────────┘
              │
              ▼

┌─────────────────────────────┐
│      Routing Controller     │
└─────────────┬───────────────┘
              │
              ▼

┌─────────────────────────────┐
│       Routing Engine        │
└─────────────────────────────┘
```

---

# G.4 Core Enumerations

## ExperienceLevel

```cpp
enum class ExperienceLevel
{
    Beginner,
    Intermediate,
    Advanced
};
```

---

## Visibility

```cpp
enum class Visibility
{
    Hidden,
    Automatic,
    Visible
};
```

---

## PersistenceScope

```cpp
enum class PersistenceScope
{
    Runtime,
    Session,
    Profile,
    Boat,
    Project
};
```

---

## ValidationSeverity

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

# G.5 Setting Registry Architecture

## SettingId

```cpp
enum class SettingId
{
    StartPosition,
    DestinationPosition,
    DepartureTime,

    SelectedBoat,
    PolarFile,

    ClimatologyEnabled,
    GribEnabled,

    MaximumWind,
    MaximumWaveHeight,

    Resolution,
    TimeStep,

    // Remaining Appendix A entries
};
```

---

## SettingDefinition

```cpp
struct SettingDefinition
{
    SettingId id;

    wxString category;

    wxString label;

    SettingType type;

    SettingValue defaultValue;

    Visibility beginner;
    Visibility intermediate;
    Visibility advanced;

    PersistenceScope persistence;

    ValidationClass validation;

    bool computeCritical;

    wxString owner;
};
```

---

## SettingsRegistry

```cpp
class SettingsRegistry
{
public:

    static const SettingDefinition&
        Get(
            SettingId id);

    static const std::vector<
        SettingDefinition>&
        All();
};
```

Responsibilities:

- Metadata lookup
- Visibility metadata
- Persistence metadata
- Validation metadata

---

# G.6 Settings Management

## SettingsManager

Central runtime configuration service.

```cpp
class SettingsManager
{
public:

    template<typename T>
    T Get(
        SettingId id) const;

    template<typename T>
    void Set(
        SettingId id,
        const T& value);

    void ResetToDefaults();
};
```

Responsibilities:

- Persist values
- Track changes
- Notify listeners
- Apply defaults

---

# G.7 Domain Models

## RouteModel

```cpp
class RouteModel
{
public:

    GeoPosition startPosition;

    GeoPosition destinationPosition;

    wxDateTime departureTime;

    wxDateTime arrivalConstraint;

    bool reverseRouting;

    std::vector<GeoPosition> waypoints;
};
```

Owns Route settings.

---

## BoatModel

```cpp
class BoatModel
{
public:

    wxString selectedBoat;

    wxString polarFile;

    double performanceFactor;

    double draft;

    SailPlan sailPlan;
};
```

Owns Boat settings.

---

## WeatherModel

```cpp
class WeatherModel
{
public:

    bool climatologyEnabled;

    bool gribEnabled;

    bool currentEnabled;

    bool waveEnabled;

    wxString windSource;

    wxString currentSource;

    wxString waveSource;
};
```

Owns Weather settings.

---

## ConstraintModel

```cpp
class ConstraintModel
{
public:

    bool avoidLand;

    bool avoidShallows;

    double maximumWind;

    double maximumWaveHeight;

    double minimumDepth;
};
```

Owns Safety settings.

---

## OptimizationModel

```cpp
class OptimizationModel
{
public:

    RouteObjective objective;

    Resolution resolution;

    int timeStep;

    int searchDepth;

    int isochronInterval;

    double vmgWeight;

    double comfortWeight;

    double fuelWeight;
};
```

Owns Optimization settings.

---

# G.8 Validation Architecture

## ValidationContext

```cpp
struct ValidationContext
{
    RouteModel route;

    BoatModel boat;

    WeatherModel weather;

    ConstraintModel constraints;

    OptimizationModel optimization;
};
```

---

## ValidationResult

```cpp
struct ValidationResult
{
    uint32_t code;

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

## ValidationResults

```cpp
struct ValidationResults
{
    std::vector<ValidationResult> items;

    bool HasErrors() const;

    bool HasCritical() const;

    bool CanCompute() const;
};
```

---

## IValidator

```cpp
class IValidator
{
public:

    virtual ~IValidator() = default;

    virtual void Validate(
        const ValidationContext&,
        ValidationResults&) = 0;
};
```

---

## Validators

```cpp
RouteValidator

BoatValidator

WeatherValidator

SafetyValidator

OptimizationValidator

CrossSettingValidator

BeginnerValidator
```

---

# G.9 Validation Engine

```cpp
class ValidationEngine
{
public:

    ValidationResults Evaluate(
        const ValidationContext&);
};
```

Validation flow:

```text
Route Validation
      ↓

Boat Validation
      ↓

Weather Validation
      ↓

Safety Validation
      ↓

Optimization Validation
      ↓

Cross Validation
      ↓

Results
```

---

# G.10 Readiness Architecture

## ReadinessState

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

## ReadinessEvaluator

```cpp
class ReadinessEvaluator
{
public:

    ReadinessState Evaluate(
        const ValidationResults&);
};
```

Responsibilities:

- Score computation
- Category evaluation
- Compute enablement
- Next-step recommendations

---

# G.11 Routing Controller

## Purpose

Coordinates validation, readiness and route computation.

```cpp
class RoutingController
{
public:

    bool ComputeRoute();
};
```

Workflow:

```text
Compute
    ↓

Validation
    ↓

Readiness
    ↓

Routing Engine
    ↓

Analysis
```

---

# G.12 Experience Controllers

## BeginnerController

```cpp
class BeginnerController
{
public:

    void StartDemoRoute();

    void ResetDemo();

    void ShowLesson();

    wxString NextStep();
};
```

Responsibilities:

- Demo route
- Guided workflow
- Beginner education
- Route explanations

---

## ExperienceProfile

```cpp
class ExperienceProfile
{
public:

    ExperienceLevel level;

    Visibility GetVisibility(
        SettingId id) const;
};
```

Controls:

- Visible controls
- Hidden controls
- Automatic controls

---

# G.13 Analysis Architecture

## AnalysisManager

```cpp
class AnalysisManager
{
public:

    RouteSummary BuildSummary();

    ComparisonReport CompareRoutes();

    DepartureOptimization
        OptimizeDeparture();
};
```

Capabilities:

- Route comparison
- Weather analysis
- Departure optimization
- Performance review

---

# G.14 Diagnostics Architecture

## DiagnosticsManager

```cpp
class DiagnosticsManager
{
public:

    DiagnosticsReport
        GenerateReport();

    SolverMetrics
        GetMetrics();
};
```

Capabilities:

- Logs
- Metrics
- Validation export
- Solver diagnostics

---

# G.15 Persistence Architecture

## Provider Interface

```cpp
class IPersistenceProvider
{
public:

    virtual bool Load() = 0;

    virtual bool Save() = 0;
};
```

---

## Providers

```cpp
ProfileProvider

BoatProvider

ProjectProvider
```

Responsibilities:

- Profiles
- Boats
- Projects

---

# G.16 Migration Architecture

## MigrationRule

```cpp
struct MigrationRule
{
    uint32_t fromVersion;

    uint32_t toVersion;

    MigrationFunction migrate;
};
```

---

## Migrators

```cpp
SettingsMigrator

BoatMigrator

ProjectMigrator
```

Responsibilities:

- Upgrade profile data
- Upgrade project data
- Upgrade boat definitions

---

# G.17 UI Layer

## BeginnerFrame

Contains:

```text
Route Setup

Demo Route

Readiness

Compute
```

---

## IntermediateFrame

Contains:

```text
Route

Boat

Weather

Safety

Routing

Analysis
```

---

## AdvancedFrame

Contains:

```text
Route

Boat

Weather

Constraints

Optimization

Analysis

Diagnostics
```

---

# G.18 Event Flow

```text
User Changes Control
          ↓

SettingsManager::Set()
          ↓

ValidationEngine
          ↓

ReadinessEvaluator
          ↓

UI Refresh
          ↓

Compute Enable/Disable
```

---

# G.19 Module Layout

```text
weatherrouting/

  settings/
      SettingId.h
      SettingDefinition.h
      SettingsRegistry.cpp
      SettingsManager.cpp

  models/
      RouteModel.h
      BoatModel.h
      WeatherModel.h
      ConstraintModel.h
      OptimizationModel.h

  validation/
      ValidationEngine.cpp
      Validators/

  readiness/
      ReadinessEvaluator.cpp

  controllers/
      RoutingController.cpp
      BeginnerController.cpp

  diagnostics/
      DiagnosticsManager.cpp

  persistence/
      ProfileProvider.cpp
      BoatProvider.cpp
      ProjectProvider.cpp

  migration/
      SettingsMigrator.cpp
      BoatMigrator.cpp
      ProjectMigrator.cpp

  ui/
      BeginnerFrame.cpp
      IntermediateFrame.cpp
      AdvancedFrame.cpp
```

---

# G.20 Testing Architecture

## Unit Tests

```text
RegistryTests

RouteValidationTests

BoatValidationTests

WeatherValidationTests

ReadinessEvaluatorTests

MigrationTests
```

---

## Integration Tests

```text
BeginnerWorkflowTests

IntermediateWorkflowTests

AdvancedWorkflowTests

PersistenceTests

SchemaMigrationTests
```

---

# G.21 Completion Criteria

Appendix G is complete when:

```text
Every SettingId
       ↓
Owned By A Model

Every Model
       ↓
Validated

Every Validation
       ↓
Produces Results

Every Result
       ↓
Feeds Readiness

Every Readiness State
       ↓
Drives UI

Every Persisted Object
       ↓
Migratable
```

This appendix serves as the implementation blueprint for the WeatherRouting codebase and is intended to guide the transition from architecture to production code.