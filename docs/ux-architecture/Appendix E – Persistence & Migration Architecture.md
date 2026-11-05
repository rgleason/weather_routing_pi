# Appendix E – Persistence & Migration Architecture
## WeatherRouting UX Architecture v0.9
### Draft

---

# E.1 Purpose

Appendix E defines how WeatherRouting settings, boats, routes, projects, user preferences, and future schema changes are stored and migrated.

This appendix establishes:

```text
Storage Locations
Persistence Scopes
Settings Lifetime
Profile Management
Project Management
Migration Rules
Backward Compatibility
Future Android Support
```

---

# E.2 Design Goals

## PERSIST-001 Single Source of Truth

All persistence behavior derives from:

```cpp
SettingDefinition
```

specifically:

```cpp
PersistenceScope
```

No persistence rules shall be hard-coded in UI dialogs.

---

## PERSIST-002 Safe Upgrades

Users shall not lose:

- Boats
- Routes
- Projects
- Preferences

when upgrading WeatherRouting.

---

## PERSIST-003 Cross Platform

The persistence architecture shall support:

```text
Windows
Linux
macOS
Android
```

without changes to the routing engine.

---

# E.3 Persistence Scopes

## Runtime

Temporary values.

Never saved.

### Examples

```text
Readiness Score
Validation Results
Warnings
Current Route Statistics
```

### Lifetime

```text
One computation cycle
```

---

## Session

Valid only for the current route session.

### Examples

```text
Start Position
Destination
Departure Time
```

### Lifetime

```text
Plugin open until closed
```

---

## Profile

User preferences.

### Examples

```text
Experience Level
Maximum Wind
Preferred Resolution
GRIB Enabled
Readiness Panel State
```

### Lifetime

```text
Across sessions
```

---

## Boat

Associated with boat definitions.

### Examples

```text
Polar File
Performance Factor
Draft
Fuel Use
Motoring Speed
```

### Lifetime

```text
Life of boat definition
```

---

## Project

Associated with saved route projects.

### Examples

```text
Route Name
Waypoints
Weather Sources
Arrival Constraints
```

### Lifetime

```text
Life of project file
```

---

# E.4 Persistence Architecture

## Overview

```text
UI
 ↓

SettingsManager
 ↓

PersistenceProvider
 ↓

Storage Backend
```

---

## SettingsManager

Centralized access to persisted values.

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
};
```

Responsibilities:

- Read settings
- Write settings
- Apply defaults
- Notify listeners
- Coordinate persistence

---

# E.5 Persistence Provider Interface

```cpp
class IPersistenceProvider
{
public:

    virtual bool Load() = 0;

    virtual bool Save() = 0;

    virtual ~IPersistenceProvider() =
        default;
};
```

---

# E.6 Persistence Providers

## ProfileProvider

Stores:

```text
User Preferences
Experience Levels
UI State
```

```cpp
class ProfileProvider :
    public IPersistenceProvider
{
};
```

---

## BoatProvider

Stores:

```text
Boat Definitions
Performance Data
Polars
```

```cpp
class BoatProvider :
    public IPersistenceProvider
{
};
```

---

## ProjectProvider

Stores:

```text
Routes
Projects
Constraints
Optimization Settings
```

```cpp
class ProjectProvider :
    public IPersistenceProvider
{
};
```

---

# E.7 File Layout

## Profile File

Filename:

```text
weatherrouting_profile.json
```

Example:

```json
{
  "schemaVersion": 1,
  "experienceLevel": "Intermediate",
  "maximumWind": 35,
  "gribEnabled": true
}
```

---

## Boat File

Filename:

```text
Bristol32.boat.json
```

Example:

```json
{
  "boatName": "Bristol 32",
  "polarFile": "Bristol32.xml",
  "performanceFactor": 100,
  "draft": 4.5
}
```

---

## Project File

Filename:

```text
WeekendCruise.wrproj
```

Example:

```json
{
  "schemaVersion": 1,

  "route": {},

  "boat": {},

  "weather": {},

  "constraints": {},

  "optimization": {}
}
```

---

# E.8 Schema Versioning

## Current Version

```cpp
constexpr uint32_t
SETTINGS_SCHEMA_VERSION = 1;
```

Every persisted object shall contain:

```json
{
  "schemaVersion": 1
}
```

---

# E.9 Migration Framework

## Migration Rule

```cpp
struct MigrationRule
{
    uint32_t fromVersion;

    uint32_t toVersion;

    std::function<void(JsonObject&)>
        migrate;
};
```

Purpose:

```text
Upgrade persisted data
between schema versions.
```

---

## Migration Flow

```text
Load Object
       ↓

Read Version
       ↓

Apply Required Migrations
       ↓

Validate
       ↓

Use Object
```

---

# E.10 Settings Migrator

```cpp
class SettingsMigrator
{
public:

    bool Migrate(
        JsonObject& settings,
        uint32_t fromVersion);
};
```

Purpose:

```text
Upgrade profile data
between schema versions.
```

---

# E.11 Example Migration

## Version 1

```json
{
  "maximumWind": 35
}
```

---

## Version 2

Introduces:

```text
maximumGust
```

Migration:

```cpp
void MigrateV1ToV2(
    JsonObject& data)
{
    if(!data.contains(
        "maximumGust"))
    {
        data["maximumGust"] = 45;
    }
}
```

---

# E.12 Boat Migration

```cpp
class BoatMigrator
{
};
```

Used when boat definitions evolve.

Example:

```text
Version 1:
Draft Only

Version 2:
Draft
SailPlan
Motoring Model
```

---

# E.13 Project Migration

```cpp
class