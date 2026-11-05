# Appendix D – JSON Schema Architecture & Data Exchange Specification
## WeatherRouting UX Architecture v0.9
### Draft

**Status:** Draft v0.9  
**Specification Owner:** WeatherRouting UX Architecture  
**Last Updated:** 2026-11-05  
**Implementation Status:** Architecture Complete

---

# D.1 Purpose

Appendix D defines the canonical data contract for all WeatherRouting persisted objects.

The schema architecture serves as the foundation for:

```text
Project Files

User Profiles

Boat Definitions

Import / Export

Cloud Synchronization

Android Storage

Automated Testing

Migration

Future APIs
```

The JSON schema becomes the external representation of the Setting Registry defined in Appendix A.

---

# D.2 Design Principles

## JSON-001 Schema First

All persisted structures must conform to a published schema.

```text
Classes
    ↓

JSON Schema
    ↓

Serialization
```

Not:

```text
Classes
    ↓

Ad-Hoc JSON
```

---

## JSON-002 Forward Migration

Future versions may add:

```text
Fields

Objects

Collections
```

without breaking existing project files.

---

## JSON-003 Human Readable

All JSON should remain understandable to users.

Example:

```json
{
  "maximumWind": 35
}
```

instead of:

```json
{
  "mwx": 35
}
```

---

## JSON-004 Platform Independent

JSON must load identically on:

```text
Windows
Linux
macOS
Android
```

---

# D.3 Schema Hierarchy

```text
weatherrouting.schema.json
      │
      ├── route.schema.json
      ├── boat.schema.json
      ├── weather.schema.json
      ├── constraints.schema.json
      ├── optimization.schema.json
      ├── analysis.schema.json
      ├── diagnostics.schema.json
      ├── profile.schema.json
      └── project.schema.json
```

---

# D.4 Root Schema

## Filename

```text
weatherrouting.schema.json
```

## Purpose

Defines complete WeatherRouting configuration.

### Root Structure

```json
{
  "schemaVersion": 1,

  "route": {},

  "boat": {},

  "weather": {},

  "constraints": {},

  "optimization": {},

  "analysis": {},

  "diagnostics": {}
}
```

---

# D.5 Common Definitions

## Coordinate

Used by:

```text
Routes
Waypoints
Hazards
Projects
```

Example:

```json
{
  "latitude": 44.9417,
  "longitude": -85.9875
}
```

Constraints:

```text
Latitude  = -90..90

Longitude = -180..180
```

---

## DateTime

All timestamps stored as:

```text
ISO-8601 UTC
```

Example:

```json
{
  "departureTime":
    "2027-06-15T12:00:00Z"
}
```

---

# D.6 Route Schema

## route.schema.json

Purpose:

```text
Route Definition
```

### Route Object

```json
{
  "startPosition": {},

  "destinationPosition": {},

  "departureTime": "",

  "routeName": "",

  "reverseRouting": false,

  "arrivalConstraint": null,

  "waypoints": []
}
```

### Required Fields

```text
startPosition

destinationPosition

departureTime
```

### Optional Fields

```text
routeName

arrivalConstraint

waypoints

reverseRouting
```

---

# D.7 Boat Schema

## boat.schema.json

Purpose:

```text
Boat Performance Model
```

### Boat Object

```json
{
  "boatType": "Cruiser",

  "selectedBoat": "Bristol 32",

  "polarFile": "Bristol32.xml",

  "performanceFactor": 100,

  "draft": 4.5,

  "sailPlan": "Standard",

  "engineAssist": false,

  "fuelBurnRate": 0.5,

  "motoringSpeed": 5.5
}
```

### Validation Rules

```text
performanceFactor = 10-150

draft > 0

fuelBurnRate >= 0

motoringSpeed >= 0
```

---

# D.8 Weather Schema

## weather.schema.json

Purpose:

```text
Weather Source Configuration
```

### Weather Object

```json
{
  "climatologyEnabled": true,

  "gribEnabled": false,

  "currentEnabled": false,

  "waveEnabled": false,

  "windSource": "Auto",

  "currentSource": "Auto",

  "waveSource": "Auto",

  "forecastHorizon": 168,

  "interpolationMethod": "Bilinear"
}
```

### Interpolation Values

```text
Nearest

Bilinear

Bicubic
```

---

# D.9 Constraints Schema

## constraints.schema.json

Purpose:

```text
Navigation Safety Constraints
```

### Constraint Object

```json
{
  "avoidLand": true,

  "avoidShallows": true,

  "maximumWind": 35,

  "maximumWaveHeight": 8,

  "minimumDepth": 6,

  "shoreBuffer": 0.25,

  "maximumGust": 45,

  "currentLimit": 0,

  "restrictedAreas": true,

  "tssAvoidance": true
}
```

### Units

```text
Wind:
knots

Wave Height:
feet

Depth:
feet

Shore Buffer:
nautical miles
```

---

# D.10 Optimization Schema

## optimization.schema.json

Purpose:

```text
Routing Engine Controls
```

### Optimization Object

```json
{
  "routeObjective": "Fastest",

  "resolution": "Medium",

  "timeStep": 30,

  "routeSmoothing": true,

  "searchDepth": 100,

  "isochronInterval": 30,

  "courseIncrement": 5,

  "vmgWeight": 100,

  "comfortWeight": 0,

  "fuelWeight": 0
}
```

### Resolution Values

```text
Low

Medium

High

VeryHigh
```

### Route Objective Values

```text
Fastest

Comfortable

Hybrid
```

---

# D.11 Analysis Schema

## analysis.schema.json

Purpose:

```text
Analysis Preferences
```

### Analysis Object

```json
{
  "routeComparison": false,

  "departureOptimization": false,

  "weatherAlongRoute": false,

  "sensitivityAnalysis": false,

  "performanceAnalysis": false
}
```

---

# D.12 Diagnostics Schema

## diagnostics.schema.json

Purpose:

```text
Diagnostics Configuration
```

### Diagnostics Object

```json
{
  "computeLog": false,

  "isochronOverlay": false,

  "searchTree": false,

  "rejectedPaths": false,

  "solverMetrics": false,

  "weatherDiagnostics": false
}
```

---

# D.13 Beginner Profile Schema

## profile.schema.json

Purpose:

```text
User Experience Preferences
```

### Profile Object

```json
{
  "experienceLevel": "Beginner",

  "showGuidedHelp": true,

  "enableDemoRoute": true,

  "enableGuidedValidation": true
}
```

### Experience Levels

```text
Beginner

Intermediate

Advanced
```

---

# D.14 Project Schema

## project.schema.json

Purpose:

```text
Persist Entire Routing Project
```

### Project Structure

```json
{
  "schemaVersion": 1,

  "projectName": "Weekend Cruise",

  "createdUtc":
    "2027-06-01T18:00:00Z",

  "modifiedUtc":
    "2027-06-02T12:00:00Z",

  "route": {},

  "boat": {},

  "weather": {},

  "constraints": {},

  "optimization": {}
}
```

---

# D.15 Validation Metadata Schema

Future extension.

### Validation Snapshot

```json
{
  "code": 3000,

  "severity": "Error",

  "message":
    "No Weather Source",

  "suggestedAction":
    "Enable Climatology"
}
```

---

# D.16 Readiness Snapshot Schema

Purpose:

```text
Testing

Diagnostics

Compute Reports
```

Example:

```json
{
  "score": 92,

  "computable": true,

  "warnings": 2,

  "errors": 0,

  "critical": 0
}
```

---

# D.17 Schema Versioning

Every persisted document shall contain:

```json
{
  "schemaVersion": 1
}
```

### Evolution Example

```text
Version 1
    ↓
MaximumWind

Version 2
    ↓
MaximumWind
MaximumGust

Version 3
    ↓
MaximumWind
MaximumGust
WindComfortFactor
```

---

# D.18 Migration Requirements

```text
Load JSON
    ↓

Compare Version
    ↓

Migrate
    ↓

Validate
    ↓

Load Model
```

No object should be loaded prior to migration.

---

# D.19 Code Generation Targets

The schema architecture supports automatic generation of:

```text
C++ Serialization

Default Objects

Validation Metadata

Persistence Bindings

Migration Tables

Test Fixtures

Documentation
```

---

# D.20 Schema Governance

Changes to Appendix D require updates to:

```text
Appendix A
Setting Registry

Appendix B
Validation Rules

Appendix E
Persistence & Migration
```

The schema is therefore considered a governed contract.

---

# D.21 Completion Criteria

Appendix D is complete when:

```text
Every SettingId
       ↓
Serializable

Every Persisted Object
       ↓
Schema Defined

Every Schema Version
       ↓
Migratable
```

This appendix serves as the data-contract specification for persistence, import/export, testing, Android support, and future interoperability.