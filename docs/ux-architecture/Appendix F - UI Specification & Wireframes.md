# Appendix F – UI Specification & Wireframes
## WeatherRouting UX Architecture v0.9
### Draft

---

# F.1 Purpose

This appendix defines the WeatherRouting user interface architecture.

The design supports three user profiles:

- Beginner
- Intermediate
- Advanced

The goals are:

- First-user success
- Progressive complexity
- Validation-driven workflow
- Readiness-guided routing
- Full advanced capability

---

# F.2 Design Principles

## UI-001 Progressive Disclosure

Show only the controls required for the user's level.

```text
Beginner
    ↓
Intermediate
    ↓
Advanced
```

---

## UI-002 Validation Driven

All UI updates are driven by the Validation Engine.

```text
Setting Changed
        ↓
Validation
        ↓
Readiness
        ↓
UI Update
```

---

## UI-003 Readiness Guided

Every level displays readiness.

```text
Route
Boat
Weather
Safety
Optimization
```

---

# F.3 Experience Levels

| Level | Audience |
|---------|----------|
| Beginner | First-time users |
| Intermediate | Most users |
| Advanced | Power users |

---

# F.4 Beginner Main Screen

## Purpose

Enable first route computation in under two minutes.

## Controls

| Control |
|----------|
| Start Position |
| Destination |
| Departure Time |
| Boat Type |
| Route Readiness |
| Compute |

---

## Beginner Wireframe

```text
┌─────────────────────────────────────────────┐
│ Weather Routing                             │
│ Beginner Mode                         (?)   │
├─────────────────────────────────────────────┤
│                                             │
│  Welcome to Weather Routing                 │
│                                             │
│  Learn Routing with a Demo Route            │
│                                             │
│  [ Start Demo Route ]                       │
│                                             │
├─────────────────────────────────────────────┤
│ Route Setup                                 │
├─────────────────────────────────────────────┤
│                                             │
│ Start                                       │
│ [ Current Position                    ]     │
│                                             │
│ Destination                                 │
│ [ Select Destination                  ]     │
│                                             │
│ Departure                                   │
│ [ Now                               ▼ ]     │
│                                             │
│ Boat Type                                   │
│                                             │
│ ○ Cruiser                                   │
│ ○ Racer                                     │
│ ○ Powerboat                                 │
│                                             │
├─────────────────────────────────────────────┤
│ Route Readiness                             │
├─────────────────────────────────────────────┤
│                                             │
│ Route      🔴                               │
│ Boat       🟢                               │
│ Weather    🟢                               │
│ Safety     🟢                               │
│                                             │
│ Score: 80%                                  │
│                                             │
│ Next Step:                                  │
│ Select a destination.                       │
│                                             │
├─────────────────────────────────────────────┤
│ [ Compute Route ]                           │
│                                             │
│ [ Reset Demo ]                              │
└─────────────────────────────────────────────┘
```

---

# F.5 Beginner Route Explanation Screen

## Purpose

Teach routing concepts after successful route generation.

### Wireframe

```text
┌─────────────────────────────────────────────┐
│ Route Successfully Computed                 │
├─────────────────────────────────────────────┤
│                                             │
│ Distance      42 nm                         │
│ ETA           7h 15m                        │
│ Avg Speed     5.8 kt                        │
│                                             │
├─────────────────────────────────────────────┤
│ Why This Route?                             │
├─────────────────────────────────────────────┤
│                                             │
│ ✓ Followed stronger wind                    │
│ ✓ Avoided light air region                  │
│ ✓ Reduced travel time                       │
│                                             │
├─────────────────────────────────────────────┤
│ Learn More                                  │
│                                             │
│ [ About Routing ]                           │
│ [ About Weather ]                           │
│ [ About Boat Speed ]                        │
│                                             │
├─────────────────────────────────────────────┤
│ [ Try My Own Route ]                        │
└─────────────────────────────────────────────┘
```

---

# F.6 Intermediate Main Window

## Tabs

- Route
- Boat
- Weather
- Safety
- Routing
- Analysis

## Main Layout

```text
┌─────────────────────────────────────────────┐
│ Route Boat Weather Safety Routing Analysis  │
├─────────────────────────────────────────────┤
│                                             │
│           Active Tab Content                │
│                                             │
├─────────────────────────────────────────────┤
│ Score: 92%  Warnings:1   Ready To Compute   │
│                                   [Compute] │
└─────────────────────────────────────────────┘
```

---

# F.7 Intermediate Route Tab

Controls:

- Start Position
- Destination
- Departure Time
- Route Name

Wireframe:

```text
┌─────────────────────────────────────────────┐
│ ROUTE                                       │
├─────────────────────────────────────────────┤
│ Start                                       │
│ Destination                                 │
│ Departure                                   │
│ Route Name                                  │
├─────────────────────────────────────────────┤
│ Route Validation                            │
│ Start          ✅                           │
│ Destination    ✅                           │
│ Departure      ✅                           │
└─────────────────────────────────────────────┘
```

---

# F.8 Intermediate Boat Tab

Controls:

- Boat Selection
- Polar File
- Performance Factor
- Draft

Validation:

- Boat Exists
- Polar Loaded
- Draft Valid

---

# F.9 Intermediate Weather Tab

Controls:

- Climatology
- GRIB
- Currents
- Waves
- Wind Source

Coverage Display:

```text
Wind      🟢
Current   🟢
Wave      🟡
Route     🟢
```

---

# F.10 Intermediate Safety Tab

Controls:

- Avoid Land
- Avoid Shallows
- Maximum Wind
- Maximum Waves
- Minimum Depth
- Restricted Areas
- TSS Avoidance

---

# F.11 Intermediate Routing Tab

Controls:

- Route Objective
- Resolution
- Time Step
- Route Smoothing

Objectives:

- Fastest
- Comfortable
- Hybrid

---

# F.12 Intermediate Analysis Tab

Controls:

- Route Comparison
- Weather Along Route
- Departure Optimization

Displays:

- Distance
- ETA
- Average Speed
- Weather Summary

---

# F.13 Advanced Main Window

## Tabs

- Route
- Boat
- Weather
- Constraints
- Optimization
- Analysis
- Diagnostics

Wireframe:

```text
┌─────────────────────────────────────────────┐
│ Route Boat Weather Constraints Optimization │
│ Analysis Diagnostics                        │
├─────────────────────────────────────────────┤
│                                             │
│        Active Advanced Tab                  │
│                                             │
├─────────────────────────────────────────────┤
│ Validation Summary                          │
│                                             │
│ Route.........PASS                          │
│ Boat..........PASS                          │
│ Weather.......PASS                          │
│ Safety........PASS                          │
│ Optimization..PASS                          │
│                                             │
│ Score: 98%                                  │
│                                             │
│                             [ Compute ]     │
└─────────────────────────────────────────────┘
```

---

# F.14 Advanced Route Controls

Additional controls:

- Arrival Constraint
- Reverse Routing
- Waypoints
- Multi-Stop Routes

---

# F.15 Advanced Boat Controls

Additional controls:

- Sail Plan
- Polar Editor
- Engine Assist
- Fuel Burn Rate
- Motoring Speed

---

# F.16 Advanced Weather Controls

Additional controls:

- Current Source
- Wave Source
- Source Priority
- Forecast Horizon
- Interpolation Method

---

# F.17 Advanced Constraint Controls

Additional controls:

- Maximum Gust
- Shore Buffer
- Current Limit

---

# F.18 Advanced Optimization Controls

Additional controls:

- Search Depth
- Isochron Interval
- Course Increment
- VMG Weight
- Comfort Weight
- Fuel Weight
- Convergence Tolerance
- Search Pruning

---

# F.19 Advanced Diagnostics

Displays:

- Validation Results
- Solver Statistics
- Search Tree
- Rejected Paths
- Weather Diagnostics

Export:

```text
Export Diagnostics
```

Produces:

```text
Validation Report

Readiness Report

Solver Statistics

Configuration Snapshot
```

---

# F.20 Shared Readiness Display

All experience levels use:

```cpp
ReadinessState
```

## Beginner

```text
Boat Ready ✅

Weather Ready ✅

Destination Missing ❌

Next Step:
Select a destination.
```

## Intermediate

```text
Route ✅

Boat ✅

Weather ⚠

Ready With Warnings
```

## Advanced

```text
VAL-WX-004

Forecast Coverage Incomplete

Recommended Action:
Load Extended Forecast
```

---

# F.21 Navigation Flows

## Beginner

```text
Home
 ↓
Compute
 ↓
Explanation
```

## Intermediate

```text
Route
 ↓
Boat
 ↓
Weather
 ↓
Safety
 ↓
Compute
 ↓
Analysis
```

## Advanced

```text
Route
 ↓
Boat
 ↓
Weather
 ↓
Constraints
 ↓
Optimization
 ↓
Compute
 ↓
Diagnostics
```

---

# F.22 Completion Criteria

Appendix F is complete when:

```text
Every SettingId
        ↓
Mapped To UI Control

Every UI Control
        ↓
Mapped To Validation

Every Validation
        ↓
Mapped To Readiness

Every Readiness State
        ↓
Mapped To User Feedback
```

This appendix serves as the implementation baseline for all WeatherRouting user interface development.