# WeatherRouting Accessibility Specification and Test Checklist

Note

One important note: this is a much more complete consolidation than the earlier checklist document, but it is still an architectural specification rather than the full 40-60 page implementation-grade accessibility standard we discussed.

The master document includes:

Accessibility vision and principles
Beginner, Intermediate, and Advanced accessibility models
Chart accessibility architecture
Route and weather accessibility
Screen reader requirements
wxWidgets accessibility guidance
Audio accessibility
Reduced motion requirements
Cognitive accessibility
Responsive accessibility
WCAG 2.2 AA mapping
Accessibility test checklist
Acceptance testing
Release certification criteria

The next level of detail would expand this into separate sections covering:

Complete keyboard interaction matrix for every screen
Full focus-navigation state machine
Detailed wxAccessible class specifications
Route leg accessibility object model
Weather probe accessibility object model
Accessibility event architecture
ARIA-equivalent mappings
Screen reader announcement catalog
Accessibility CI test automation
WCAG traceability matrix (Requirement → Implementation → Test Case)
Android/Touch accessibility requirements
OpenCPN integration requirements

That would become the definitive implementation contract for WeatherRouting accessibility.


## Version 1.0

# 1. Purpose

This document defines accessibility requirements, WCAG 2.2 AA mapping, and verification procedures for:

- Beginner User Experience
- Intermediate User Experience
- Advanced User Experience
- Windows, macOS, Linux
- Future tablet/mobile adaptations

---

# 2. Accessibility Vision

Accessibility is a core design requirement.

Users shall be able to:

1. Configure a route
2. Validate weather data
3. Compute a route
4. Analyze route decisions
5. Export results

using:

- Keyboard only
- Screen reader
- High contrast mode

---

# 3. User Experience Accessibility Goals

## Beginner

Goal: Reduce confusion.

Requirements:
- Guided workflow
- Simple language
- Error recovery assistance
- Tutorial narration support
- Minimal visible controls

## Intermediate

Goal: Improve efficiency.

Requirements:
- Context-sensitive help
- Keyboard accelerators
- Expandable advanced controls
- Accessible validation reporting

## Advanced

Goal: Enable complete functionality.

Requirements:
- Full keyboard operation
- Accessible chart inspection
- Route comparison accessibility
- Command palette support
- Solver explanation access

---

# 4. Core Accessibility Requirements

## Keyboard First

All features must be operable without a mouse.

## Visible Focus

- Minimum 3px focus indicator
- Visible in all themes
- Visible on chart objects

## Color Independence

Information shall never rely solely on color.

## Responsive Scaling

Supported:

- 100%
- 125%
- 150%
- 200%
- 300%

## Themes

- Standard
- Marine Night
- High Contrast
- Red Night Vision

---

# 5. Chart Accessibility

## Keyboard Navigation

- Arrow Keys: Pan
- + : Zoom In
- - : Zoom Out
- Home : Fit Route

## Accessible Route Summary

Provide:

- Distance
- ETA
- Maximum Wind
- Maximum Waves
- Average Boat Speed

## Accessible Route Legs

Each leg exposes:

- Position
- Heading
- Boat Speed
- Wind
- Current
- Time

---

# 6. Screen Reader Requirements

Supported:

- NVDA
- JAWS
- Narrator
- VoiceOver
- Orca

Implement wxAccessible for:

- Route lists
- Route legs
- Weather probes
- Analysis tables
- Warning lists

---

# 7. WCAG 2.2 AA Success Criteria Mapping

## Perceivable

### 1.1.1 Non-text Content

Applicability:
- Icons
- Tool buttons
- Route graphics
- Charts

### 1.3.1 Info and Relationships

Applicability:
- Project tree
- Results tables
- Comparison views

### 1.4.1 Use of Color

All route states and warnings require text and/or icons.

### 1.4.3 Contrast Minimum

- 4.5:1 normal text
- 3:1 large text

### 1.4.11 Non-text Contrast

Controls and focus indicators must meet 3:1 minimum contrast.

## Operable

### 2.1.1 Keyboard

All functions available by keyboard.

### 2.1.2 No Keyboard Trap

All dialogs and panels must be escapable.

### 2.4.3 Focus Order

Toolbar → Navigator → Chart → Inspector → Results → Status Bar

### 2.4.7 Focus Visible

All focused elements visible in every theme.

### 2.5.7 Dragging Movements

Keyboard alternatives required for drag operations.

### 2.5.8 Target Size Minimum

Minimum:
- 24x24 px

Recommended:
- 44x44 px

## Understandable

### 3.2.3 Consistent Navigation

Navigation patterns remain consistent across user levels.

### 3.3.1 Error Identification

Errors described in plain language.

### 3.3.3 Error Suggestion

Errors include recovery guidance.

## Robust

### 4.1.2 Name Role Value

All controls expose:
- Name
- Role
- State
- Value

### 4.1.3 Status Messages

Screen readers receive:
- Route started
- Route completed
- Validation errors
- Warnings

---

# 8. Accessibility Test Checklist

## Keyboard Navigation

- [ ] Launch without mouse
- [ ] Navigate menus
- [ ] Compute route
- [ ] Inspect route
- [ ] Export report
- [ ] Compare routes

## Focus Management

- [ ] Correct tab order
- [ ] Visible focus indicator
- [ ] No hidden focus targets
- [ ] Focus preserved after layout changes

## Screen Reader Testing

### NVDA
- [ ] Pass

### JAWS
- [ ] Pass

### Narrator
- [ ] Pass

### VoiceOver
- [ ] Pass

### Orca
- [ ] Pass

## Route Accessibility

- [ ] Route summary announced
- [ ] Route leg details announced
- [ ] ETA announced
- [ ] Wind and wave data announced

## Weather Accessibility

- [ ] Weather probe announced
- [ ] Forecast time announced
- [ ] Wind announced
- [ ] Current announced

## Color and Contrast

- [ ] Standard Theme
- [ ] Marine Night Theme
- [ ] High Contrast Theme
- [ ] Red Night Theme

## Scaling

- [ ] 100%
- [ ] 125%
- [ ] 150%
- [ ] 200%
- [ ] 300%

## Layout Testing

### 1280x800
- [ ] Pass

### 1920x1080
- [ ] Pass

### 2560x1440
- [ ] Pass

### 3440x1440
- [ ] Pass

## Error Handling

- [ ] Missing weather explained
- [ ] Missing polar explained
- [ ] Corrective actions provided

## Reduced Motion

- [ ] Forecast animation disabled
- [ ] Panel animation disabled
- [ ] Chart animation reduced

---

# 9. Accessibility Acceptance Criteria

Beginner users can:

- Create route
- Compute route
- Understand errors

Intermediate users can:

- Configure routing
- Validate weather
- Save and open projects

Advanced users can:

- Create routes
- Create constraints
- Compare routes
- Export analysis

using only:

- Keyboard
- Screen Reader
- High Contrast Theme

---

# 10. Certification

Release Criteria:

- WCAG 2.2 AA compliance target met
- Keyboard-only workflow validated
- Screen reader testing completed
- High contrast verification completed
- Responsive layout verification completed
