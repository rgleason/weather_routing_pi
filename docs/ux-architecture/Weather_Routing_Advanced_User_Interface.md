# Weather_Routing Advanced User Interface

## Vision

The Advanced User Interface transforms WeatherRouting into a professional navigation workstation focused on route optimization, validation, analysis, and decision support. The chart remains the primary workspace while routing controls are exposed through contextual panels and progressive disclosure.

---

# Workspace Architecture

```text
+-----------------------------------------------------------------------------------+
| Weather Routing                                                      [Compute ▼] |
+-----------------------------------------------------------------------------------+
| File  Route  Weather  Analysis  Constraints  View  Tools  Help                  |
+-----------------------------------------------------------------------------------+

+-------------+----------------------------------------------------+---------------+
|             |                                                    |               |
| PROJECT     |                                                    |   INSPECTOR   |
|             |                                                    |               |
| Route Plan  |                                                    | Route Info    |
| Weather     |               CHART WINDOW                         | Weather       |
| Constraints |                                                    | Currents      |
| Analysis    |                                                    | Waves         |
| Diagnostics |                                                    | Solver        |
|             |                                                    |               |
+-------------+----------------------------------------------------+---------------+

+-----------------------------------------------------------------------------------+
| Results | Warnings | Routing Log | Comparison | Statistics                        |
+-----------------------------------------------------------------------------------+
```

---

# Advanced Workspace Areas

## Route Definition

- Boat Profile
- Polar Selection
- Performance Factor
- Safety Factor
- Start / Finish
- Departure Time
- Routing Objective

Objectives:

- Fastest ETA
- Earliest Arrival
- Minimum Distance
- Minimum Fuel
- Comfort
- Custom Objective

## Weather Sources

Supported data sources:

- GRIB
- Climatology
- Currents
- Waves
- Tides
- Ice

Features:

- Enable / Disable
- Source Prioritization
- Forecast Coverage Review
- Layer Inspection

## Constraints

### Vessel Limits

- Wind Speed
- Gust Speed
- Wave Height
- Current Velocity
- Heel Angle
- Leeway Angle

### Operational Constraints

- TSS Avoidance
- Restricted Areas
- Shoal Avoidance
- Ice Exclusion
- Offshore Distance Limits

### Time Constraints

- Departure Window
- Arrival Targets

## Routing Engine Controls

### Solver Selection

- Isochrone
- Genetic
- Dynamic Programming
- Hybrid

### Resolution Controls

- Time Step
- Angular Resolution
- Grid Resolution

### Expert Controls

- Branch Limit
- Dominance Pruning
- State Reduction
- Backtracking Depth

## Analysis Workspace

### Route Comparison

Compare:

- Fastest
- Comfort
- Fuel Efficient
- Alternate Departures

### Sensitivity Analysis

Evaluate:

- Boat Speed Variance
- Wind Changes
- Departure Changes
- Forecast Changes

### Ensemble Routing

Support:

- GFS
- ECMWF
- CMC
- ICON

## Diagnostics

Validation checks:

- Polar Availability
- Weather Availability
- Current Data Availability
- Departure Validation
- Destination Validation

Warnings framework:

- Missing Weather Inputs
- Forecast Coverage Limits
- Polar Extrapolation
- Constraint Violations

---

# Chart Interaction Model

## Workspace Modes

```cpp
enum class WorkspaceMode
{
    Navigation,
    RoutePlanning,
    RouteInspection,
    WeatherInspection,
    ConstraintEditing,
    Analysis,
    Diagnostics
};
```

## 1. Navigation

Default OpenCPN interaction.

Functions:

- Pan
- Zoom
- Measure
- Select AIS Targets
- Select Routes

## 2. Route Planning

Functions:

- Set Start
- Set Finish
- Move Waypoints
- Edit Route Geometry

## 3. Route Inspection

Functions:

- Hover Leg Information
- Leg Selection
- Detailed Segment Analysis
- Center On Segment

## 4. Weather Inspection

Functions:

- Weather Probe
- Forecast Inspection
- Time-Linked Weather Review

Displayed Data:

- Wind
- Current
- Pressure
- Waves
- Temperature
- Rain

## 5. Constraint Editing

Direct chart editing:

- No-Go Zones
- Hazard Areas
- Traffic Separation Schemes
- Preferred Routing Areas
- Cost Regions

## 6. Analysis Mode

Compare multiple routes simultaneously.

Examples:

- Fastest
- Comfort
- Fuel
- Delayed Departure

## 7. Diagnostic Mode

Developer and expert visualization.

Displays:

- Isochrones
- Solver States
- Routing Graph
- Search Tree
- Forecast Boundaries

---

# Responsive Desktop Layout

## Standard Desktop (1920x1080)

```text
+-----------+-------------------------------------+------------------+
| Navigator |              Chart                  | Inspector        |
+-----------+-------------------------------------+------------------+
| Results / Diagnostics / Statistics / Compare                       |
+-------------------------------------------------------------------+
```

Target Sizes:

- Navigator: 260 px
- Inspector: 320 px
- Bottom Panel: 250 px
- Chart: Remaining Space

## Wide Desktop (2560x1440)

Persistent comparison and analysis panels.

## Ultra-Wide Desktop (3440x1440)

Three-panel analysis environment:

- Results
- Diagnostics
- Route Comparison

## Compact Desktop (1280x800)

Adaptive behavior:

- Inspector collapses automatically
- Bottom panel tabbed
- Chart remains dominant

## Layout Enumeration

```cpp
enum class DockLayout
{
    Compact,
    Standard,
    Wide,
    UltraWide
};
```

---

# Keyboard Shortcuts

## Global

| Shortcut | Action |
|-----------|--------|
| F1 | Context Help |
| F5 | Refresh Weather |
| F9 | Recompute Route |
| F11 | Full Screen |
| Ctrl+N | New Project |
| Ctrl+O | Open Project |
| Ctrl+S | Save Project |
| Ctrl+Shift+S | Save As |
| Esc | Cancel Operation |

## Workspace Selection

| Shortcut | Workspace |
|-----------|-----------|
| Alt+1 | Navigation |
| Alt+2 | Route Planning |
| Alt+3 | Route Inspection |
| Alt+4 | Weather Inspection |
| Alt+5 | Constraint Editing |
| Alt+6 | Analysis |
| Alt+7 | Diagnostics |

## Route Planning

| Shortcut | Action |
|-----------|--------|
| R | Route Planning |
| S | Set Start |
| F | Set Finish |
| I | Insert Waypoint |
| M | Move Waypoint |
| Delete | Delete Waypoint |

## Weather Inspection

| Shortcut | Action |
|-----------|--------|
| W | Weather Probe |
| P | Drop Probe |
| T | Toggle Forecast Panel |

## Forecast Timeline

| Shortcut | Action |
|-----------|--------|
| ← | Previous Step |
| → | Next Step |
| Shift+← | -6 Hours |
| Shift+→ | +6 Hours |
| Ctrl+← | -24 Hours |
| Ctrl+→ | +24 Hours |

## Analysis

| Shortcut | Action |
|-----------|--------|
| A | Analysis Mode |
| 1 | Fastest Route |
| 2 | Comfort Route |
| 3 | Fuel Route |
| 4 | Alternate Departure |

## Compute

| Shortcut | Action |
|-----------|--------|
| Ctrl+R | Compute Route |
| Ctrl+Shift+R | Recompute Route |
| Ctrl+D | Diagnostics Summary |
| Ctrl+W | Weather Validation |

## Command Palette

```text
Ctrl+Shift+P
```

Provides search-driven access to all routing commands.

---

# Accessibility Requirements

## Compliance Target

- WCAG 2.2 AA
- Full Keyboard Navigation
- Screen Reader Support
- High Contrast Themes
- 200% Scaling Support
- Color Independent Information Display

## Visual Themes

- Standard
- Marine Night
- High Contrast
- Red Night Vision

## Screen Reader Support

All controls require meaningful labels.

Examples:

- Compute Weather Route
- Open Route Comparison
- Show Weather Inspection Panel

## Route Leg Narration

Selected route legs must announce:

- Position
- Heading
- Boat Speed
- Wind
- Current
- ETA

## Non-Color Indicators

Warnings and states must also use:

- Icons
- Labels
- Patterns
- Tooltips

Examples:

- ✅ Valid
- ⚠ Warning
- ⓘ Information

---

# Design Principles

1. Chart First
2. Progressive Disclosure
3. Professional Navigation Workflow
4. Multi-Route Analysis
5. Solver Transparency
6. Explainability
7. Accessibility by Default
8. Keyboard-Driven Operation
9. Responsive Layouts
10. Consistent First User → Intermediate → Advanced Experience
