# WeatherRouting UX Architecture
## Version 0.9 Draft

**Status:** Draft v0.9  
**Architecture Status:** Complete  
**Implementation Status:** Beginning  
**Scope:** OpenCPN WeatherRouting Modernization

---

# 1. Executive Summary

WeatherRouting UX Architecture v0.9 defines a complete modernization of the WeatherRouting user experience while preserving the existing routing engine.

The architecture introduces:

- Beginner Experience
- Intermediate Experience
- Advanced Experience
- Registry-Driven Settings
- Validation Architecture
- Readiness Engine
- Persistence & Migration Framework
- JSON Schema Contract
- Modular C++ Architecture
- Android Readiness

The objective is to help first-time users successfully compute routes while still exposing the full power of the routing engine to advanced navigators.

---

# 2. Design Goals

## User Goals

- First route within minutes
- Guided workflow
- Explain calculations
- Hide unnecessary complexity
- Support advanced optimization

## Technical Goals

- Registry-driven settings
- Centralized validation
- Readiness scoring
- Cross-platform persistence
- Migration compatibility
- Testable architecture

---

# 3. User Levels

## Beginner

Focus:

```text
Learning
Guidance
Success
```

Controls:

```text
Demo Route
Boat Type
Destination
Departure
Compute
```

Primary Outcome:

```text
First successful route
```

---

## Intermediate

Focus:

```text
Productivity
Safety
Voyage Planning
```

Tabs:

```text
Route
Boat
Weather
Safety
Routing
Analysis
```

Primary Outcome:

```text
Daily routing workflows
```

---

## Advanced

Focus:

```text
Optimization
Diagnostics
Research
```

Tabs:

```text
Route
Boat
Weather
Constraints
Optimization
Analysis
Diagnostics
```

Primary Outcome:

```text
Full routing capability
```

---

# 4. Core Architecture

```text
UI
 ↓

Settings Registry
 ↓

Validation Engine
 ↓

Readiness Engine
 ↓

Routing Controller
 ↓

Routing Engine
 ↓

Persistence Layer
```

---

# 5. Key Components

## Setting Registry

Single source of truth.

Defines:

```text
Visibility

Validation

Persistence

Defaults

Migration
```

---

## Validation Engine

Ensures:

```text
Safe Inputs

Consistency

Route Readiness
```

---

## Readiness Engine

Provides:

```text
Readiness Score

Compute Enablement

Next Recommended Action
```

---

## Persistence Layer

Provides:

```text
Profiles

Projects

Boats

Migration
```

---

# 6. Document Structure

This architecture is supported by the following appendices.

## Appendix A

Setting Registry

File:

```text
Appendix_A_Setting_Registry.md
```

Contains:

```text
73 settings

Defaults

Owners

Visibility

Validation

Persistence
```

---

## Appendix B

Validation Architecture

File:

```text
Appendix_B_Validation_Architecture.md
```

Contains:

```text
Validation Rules

Cross Validation

Readiness Model

Compute Rules
```

---

## Appendix C

Settings Registry CSV Specification

File:

```text
Appendix_C_Settings_Registry_CSV.md
```

Contains:

```text
Machine-readable Registry

Code Generation Metadata

Visibility Metadata
```

---

## Appendix D

JSON Schema Architecture

File:

```text
Appendix_D_JSON_Schema_Architecture.md
```

Contains:

```text
Project Schema

Profile Schema

Boat Schema

Migration Contracts
```

---

## Appendix E

Persistence & Migration

File:

```text
Appendix_E_Persistence_Migration.md
```

Contains:

```text
Persistence Scopes

Providers

Migration Rules

Backup Strategy
```

---

## Appendix F

UI Specification & Wireframes

File:

```text
Appendix_F_UI_Specification.md
```

Contains:

```text
Beginner Screens

Intermediate Screens

Advanced Screens

Navigation Flows

Wireframes
```

---

## Appendix G

C++ Architecture

File:

```text
Appendix_G_CPP_Architecture.md
```

Contains:

```text
Models

Controllers

Validation Architecture

Readiness Architecture

Persistence Architecture
```

---

## Appendix H

Implementation Roadmap

File:

```text
Appendix_H_Implementation_Roadmap.md
```

Contains:

```text
Phases

Milestones

Epics

Implementation Order
```

---

# 7. Initial Implementation Sequence

The first implementation milestone is:

```text
WR-100
SettingId.h

WR-101
SettingDefinition.h

WR-102
SettingsRegistry.cpp
```

followed by:

```text
WR-200
Validation Engine

WR-300
Readiness Engine
```

---

# 8. Draft Status

This architecture is frozen as Draft v0.9 and is considered sufficient to begin implementation.

Future revisions are expected as implementation progresses.

---