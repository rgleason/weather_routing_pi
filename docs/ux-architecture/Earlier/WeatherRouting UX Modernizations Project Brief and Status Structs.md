WeatherRouting UX Modernization Project Brief

Project Title

WeatherRouting Progressive User Experience and First-Route Success Initiative

Executive Summary

WeatherRouting has evolved into a powerful routing platform, but much of its user interface reflects the needs of experienced users rather than first-time sailors. New users are currently exposed to concepts such as GRIB files, boat polars, routing constraints, weather source selection, and optimization controls before successfully generating their first route.

This project shifts the primary design goal from:

"Expose routing functionality"

to:

"Guide users from their first successful route through progressively deeper levels of routing expertise."

-----------------------------------------------------
Every WeatherRouting user should be able to generate a successful climatology-based route within minutes of installation, using Boat.xml and default settings, before they are asked to understand any advanced routing concepts.
------------------------------------------------------

The project introduces:

First User, Intermediate, and Advanced experience levels
Guaranteed first-route success using Climatology and Boat.xml
Progressive disclosure of settings
Context-sensitive learning and help
Preflight validation and recovery
Android-friendly simplified workflows
Vision Statement


I started at this point and it developed going down the file.
This is better and more comprehensive than I had earlier.
======================================

// RouteStatus.h

#pragma once

#include <wx/string.h>
#include <wx/datetime.h>

enum class RouteState
{
    Idle,
    Queued,
    Running,
    Completed,
    Cancelled,
    Failed
};

enum class RouteSeverity
{
    None,
    Info,
    Warning,
    Error,
    Fatal
};

enum class RouteError
{
    None = 0,

    // Configuration (1000)
    InvalidConfiguration = 1000,
    MissingStartPosition,
    MissingDestination,
    MissingBoat,
    MissingPolar,

    // Environment / Data (2000)
    MissingWeatherData = 2000,
    MissingWindData,
    MissingCurrentData,
    MissingWaveData,
    WeatherInterpolationFailed,

    // Routing (3000)
    NoSolutionFound = 3000,
    LandBlocked,
    ConstraintViolation,
    MaxIterationsExceeded,

    // Runtime (4000)
    CancelledByUser = 4000,
    ThreadShutdown,
    InternalError
};

struct RouteDiagnostic
{
    RouteError error = RouteError::None;
    RouteSeverity severity = RouteSeverity::None;

    // Human-readable explanation
    wxString detail;

    // Where failure occurred
    wxString phase;

    // Optional context
    wxDateTime routeTime;

    double lat = NAN;
    double lon = NAN;

    bool HasError() const
    {
        return error != RouteError::None;
    }
};

struct RouteStatus
{
    RouteState state = RouteState::Idle;

    RouteDiagnostic diagnostic;

    wxDateTime lastUpdate;

    bool IsRunning() const
    {
        return state == RouteState::Running;
    }

    bool IsFinished() const
    {
        return state == RouteState::Completed ||
               state == RouteState::Cancelled ||
               state == RouteState::Failed;
    }

    bool IsFailure() const
    {
        return state == RouteState::Failed;
    }
};


=========================================================

Fix error codes

=========================================================

Yes. The original enum has a subtle problem:

MissingWeatherData = 2000,
MissingWindData,
MissingCurrentData,
MissingWaveData,
WeatherInterpolationFailed,


which becomes:

MissingWeatherData       = 2000
MissingWindData          = 2001
MissingCurrentData       = 2002
MissingWaveData          = 2003
WeatherInterpolationFailed = 2004


That works, but it is fragile. If someone later inserts an entry, all subsequent codes change.

For long-term diagnostics and bug reports, each code should be fixed explicitly.

I'd write:

enum class RouteError : uint16_t
{
    None = 0,

    // =====================================================
    // Configuration (1000-1999)
    // =====================================================

    InvalidConfiguration = 1000,
    MissingStartPosition = 1001,
    MissingDestination   = 1002,
    MissingBoat          = 1003,
    MissingPolar         = 1004,

    // =====================================================
    // Data Sources (2000-2999)
    // =====================================================

    MissingWeatherData       = 2000,
    MissingWindData          = 2001,
    MissingCurrentData       = 2002,
    MissingWaveData          = 2003,
    MissingTidalData         = 2004,
    WeatherInterpolationFailed = 2005,
    GribCommunicationFailure = 2006,
    ClimatologyFailure       = 2007,

    // =====================================================
    // Routing Engine (3000-3999)
    // =====================================================

    NoSolutionFound        = 3000,
    LandBlocked            = 3001,
    ConstraintViolation    = 3002,
    MaxIterationsExceeded  = 3003,
    RouteAborted           = 3004,
    InvalidRouteGeometry   = 3005,

    // =====================================================
    // Runtime / Infrastructure (4000-4999)
    // =====================================================

    CancelledByUser      = 4000,
    ThreadShutdown       = 4001,
    Timeout              = 4002,
    InternalError        = 4999
};


Then your helper becomes stable forever:

inline wxString ErrorCode(RouteError error)
{
    return wxString::Format(
        "WR%04u",
        static_cast<unsigned>(error));
}


Examples:

WR1004  Missing Polar
WR2001  Missing Wind Data
WR3000  No Solution Found
WR3001  Land Blocked
WR4000  Cancelled By User
WR4999  Internal Error


I'd also separate errors from warnings.

For example:

enum class RouteWarning : uint16_t
{
    None = 0,

    MissingCurrentData      = 5001,
    MissingWaveData         = 5002,
    ClimatologyFallback     = 5003,
    ReducedWeatherCoverage  = 5004
};


because many of those situations should not fail a route. A route can often complete successfully while issuing warnings.

For the WeatherRouting engine specifically, I would reserve:

1000-1999 Configuration
2000-2999 Weather/Data
3000-3999 Routing
4000-4999 Runtime
5000-5999 Warnings


That gives you room to grow for years without renumbering anything.


===================================
error-code system self-validating at compile time.

=====================================

For this design, I'd go beyond just static_assert(error != ...) and make the error-code system self-validating at compile time.

A common failure mode is:

MissingWindData = 2001,
MissingCurrentData = 2001,   // duplicate accidentally added later


which compiles fine.

Option 1: Explicit Sentinel Validation
enum class RouteError : uint16_t
{
    None = 0,

    InvalidConfiguration = 1000,
    MissingStartPosition = 1001,
    MissingDestination   = 1002,
    MissingBoat          = 1003,
    MissingPolar         = 1004,

    MissingWeatherData   = 2000,
    MissingWindData      = 2001,
    MissingCurrentData   = 2002,

    InternalError        = 4999,

    Last = InternalError
};

static_assert(
    static_cast<uint16_t>(RouteError::Last) == 4999,
    "RouteError numbering changed"
);


Useful, but not sufficient.

Option 2: Single Source of Truth (Recommended)

Define error codes once:

#define ROUTE_ERRORS(X) \
    X(None,                     0)    \
    X(InvalidConfiguration,  1000)    \
    X(MissingStartPosition,  1001)    \
    X(MissingDestination,    1002)    \
    X(MissingBoat,           1003)    \
    X(MissingPolar,          1004)    \
    X(MissingWeatherData,    2000)    \
    X(MissingWindData,       2001)    \
    X(MissingCurrentData,    2002)    \
    X(NoSolutionFound,       3000)    \
    X(LandBlocked,           3001)    \
    X(CancelledByUser,       4000)    \
    X(InternalError,         4999)


Generate enum:

enum class RouteError : uint16_t
{
#define DECLARE_ERROR(name, value) name = value,
    ROUTE_ERRORS(DECLARE_ERROR)
#undef DECLARE_ERROR
};


Generate strings:

constexpr std::string_view ToString(RouteError e)
{
    switch (e)
    {
#define ERROR_CASE(name, value) \
        case RouteError::name: return #name;

        ROUTE_ERRORS(ERROR_CASE)

#undef ERROR_CASE
    }

    return "Unknown";
}


Now you can't forget to update the switch.

Option 3: Compile-Time Table Validation

Create a constexpr table:

struct ErrorInfo
{
    RouteError error;
    uint16_t code;
};

constexpr ErrorInfo kErrors[] =
{
    { RouteError::InvalidConfiguration, 1000 },
    { RouteError::MissingStartPosition, 1001 },
    { RouteError::MissingDestination,   1002 },
    { RouteError::MissingBoat,          1003 },
    { RouteError::MissingPolar,         1004 },

    { RouteError::MissingWeatherData,   2000 },
    { RouteError::MissingWindData,      2001 },
    { RouteError::MissingCurrentData,   2002 }
};


Validate uniqueness:

template <size_t N>
constexpr bool HasDuplicateCodes(
    const ErrorInfo (&table)[N])
{
    for (size_t i = 0; i < N; i++)
    {
        for (size_t j = i + 1; j < N; j++)
        {
            if (table[i].code == table[j].code)
                return true;
        }
    }

    return false;
}

static_assert(
    !HasDuplicateCodes(kErrors),
    "Duplicate RouteError code detected");


Now duplicate IDs become build failures.

Option 4: Validate Error Categories

Since you want ranges:

1000-1999 Configuration
2000-2999 Data
3000-3999 Routing
4000-4999 Runtime


enforce them:

constexpr bool IsConfiguration(RouteError e)
{
    const auto v = static_cast<uint16_t>(e);
    return v >= 1000 && v < 2000;
}


Then:

static_assert(
    IsConfiguration(RouteError::MissingBoat),
    "MissingBoat in wrong category");


or more generally:

static_assert(
    static_cast<uint16_t>(RouteError::MissingBoat) == 1003);

static_assert(
    static_cast<uint16_t>(RouteError::NoSolutionFound) == 3000);

What I'd Use in WeatherRouting

I'd combine:

Explicit numeric assignments
X-macro definition
Compile-time duplicate-code validation
Compile-time string mapping generation

That gives you:

RouteError
ComputeErrorString()
ErrorCode()


all derived from a single source of truth.

Then a future developer cannot:

duplicate a code
renumber a code accidentally
forget to update the string table
forget to update the UI mapping

without breaking the build. That's exactly the sort of protection that's valuable in a long-lived project like WeatherRouting.


==============================================================
 header-only definition

 ==================================================================



For WeatherRouting, I'd make this a header-only definition containing:

Error codes
Compile-time validation
String mapping
Category mapping
Diagnostic structs

The goal is that adding a new error requires changing one list only, and forgetting anything becomes a compile-time error.

#ifndef _WEATHER_ROUTING_ROUTE_STATUS_H_
#define _WEATHER_ROUTING_ROUTE_STATUS_H_

#include <array>
#include <cstdint>
#include <string_view>

#include <wx/datetime.h>
#include <wx/string.h>

// ============================================================================
// Error Definitions
//
// Single source of truth.
// Every error code is declared exactly once.
// ============================================================================

#define WR_ROUTE_ERRORS(X)                                                     \
    /* ------------------------------------------------------------ */         \
    /* Configuration (1000-1999)                                   */         \
    /* ------------------------------------------------------------ */         \
    X(InvalidConfiguration,     1000)                                          \
    X(MissingStartPosition,     1001)                                          \
    X(MissingDestination,       1002)                                          \
    X(MissingBoat,              1003)                                          \
    X(MissingPolar,             1004)                                          \
                                                                               \
    /* ------------------------------------------------------------ */         \
    /* Weather/Data (2000-2999)                                    */         \
    /* ------------------------------------------------------------ */         \
    X(MissingWeatherData,       2000)                                          \
    X(MissingWindData,          2001)                                          \
    X(MissingCurrentData,       2002)                                          \
    X(MissingWaveData,          2003)                                          \
    X(MissingTidalData,         2004)                                          \
    X(WeatherInterpolationFailed, 2005)                                        \
    X(GribCommunicationFailure, 2006)                                          \
    X(ClimatologyFailure,       2007)                                          \
                                                                               \
    /* ------------------------------------------------------------ */         \
    /* Routing Engine (3000-3999)                                 */         \
    /* ------------------------------------------------------------ */         \
    X(NoSolutionFound,          3000)                                          \
    X(LandBlocked,              3001)                                          \
    X(ConstraintViolation,      3002)                                          \
    X(MaxIterationsExceeded,    3003)                                          \
    X(RouteAborted,             3004)                                          \
    X(InvalidRouteGeometry,     3005)                                          \
                                                                               \
    /* ------------------------------------------------------------ */         \
    /* Runtime (4000-4999)                                        */         \
    /* ------------------------------------------------------------ */         \
    X(CancelledByUser,          4000)                                          \
    X(ThreadShutdown,           4001)                                          \
    X(Timeout,                 4002)                                           \
    X(InternalError,           4999)

// ============================================================================
// Route State
// ============================================================================

enum class RouteState
{
    Idle,
    Queued,
    Running,
    Completed,
    Cancelled,
    Failed
};

enum class RouteSeverity
{
    None,
    Info,
    Warning,
    Error,
    Fatal
};

// ============================================================================
// Route Error
// ============================================================================

enum class RouteError : uint16_t
{
    None = 0,

#define DECLARE_ERROR(name, code) name = code,
    WR_ROUTE_ERRORS(DECLARE_ERROR)
#undef DECLARE_ERROR
};

// ============================================================================
// Compile-time Error Table
// ============================================================================

struct RouteErrorInfo
{
    RouteError error;
    uint16_t code;
    std::string_view name;
};

constexpr RouteErrorInfo kRouteErrors[] =
{
#define ERROR_INFO(name, code) \
    { RouteError::name, code, #name },

    WR_ROUTE_ERRORS(ERROR_INFO)

#undef ERROR_INFO
};

// ============================================================================
// Compile-time Validation
// ============================================================================

template <size_t N>
constexpr bool HasDuplicateCodes(
    const RouteErrorInfo (&table)[N])
{
    for (size_t i = 0; i < N; ++i)
    {
        for (size_t j = i + 1; j < N; ++j)
        {
            if (table[i].code == table[j].code)
            {
                return true;
            }
        }
    }

    return false;
}

static_assert(
    !HasDuplicateCodes(kRouteErrors),
    "Duplicate RouteError codes detected");

static_assert(
    static_cast<uint16_t>(RouteError::InternalError) == 4999,
    "Reserved RouteError values have changed");

// ============================================================================
// Category Helpers
// ============================================================================

constexpr uint16_t ErrorCode(RouteError error)
{
    return static_cast<uint16_t>(error);
}

constexpr bool IsConfigurationError(RouteError error)
{
    const auto code = ErrorCode(error);
    return code >= 1000 && code < 2000;
}

constexpr bool IsDataError(RouteError error)
{
    const auto code = ErrorCode(error);
    return code >= 2000 && code < 3000;
}

constexpr bool IsRoutingError(RouteError error)
{
    const auto code = ErrorCode(error);
    return code >= 3000 && code < 4000;
}

constexpr bool IsRuntimeError(RouteError error)
{
    const auto code = ErrorCode(error);
    return code >= 4000 && code < 5000;
}

// ============================================================================
// String Conversions
// ============================================================================

constexpr std::string_view RouteErrorName(RouteError error)
{
    switch (error)
    {
#define ERROR_CASE(name, code) \
        case RouteError::name: return #name;

        WR_ROUTE_ERRORS(ERROR_CASE)

#undef ERROR_CASE

    case RouteError::None:
        return "None";
    }

    return "Unknown";
}

// ============================================================================
// UI Helpers
// ============================================================================

inline wxString FormatErrorCode(RouteError error)
{
    return wxString::Format(
        "WR%04u",
        static_cast<unsigned>(error));
}

// ============================================================================
// Diagnostic Context
// ============================================================================

struct RouteDiagnostic
{
    RouteError error = RouteError::None;
    RouteSeverity severity = RouteSeverity::None;

    wxString detail;
    wxString phase;

    wxDateTime routeTime;

    double lat = NAN;
    double lon = NAN;

    bool HasError() const
    {
        return error != RouteError::None;
    }
};

// ============================================================================
// Public Engine Status Structure
// ============================================================================

struct RouteStatus
{
    RouteState state = RouteState::Idle;

    RouteDiagnostic diagnostic;

    wxDateTime lastUpdate;

    bool IsRunning() const
    {
        return state == RouteState::Running;
    }

    bool IsFinished() const
    {
        return state == RouteState::Completed ||
               state == RouteState::Cancelled ||
               state == RouteState::Failed;
    }

    bool IsFailure() const
    {
        return state == RouteState::Failed;
    }
};

#endif // _WEATHER_ROUTING_ROUTE_STATUS_H_


A future refinement I would strongly consider for WeatherRouting is adding a second compile-time validated RouteWarning enum (5000-5999). 

===========================
NOTE THIS PAUL
Missing wave or current data often shouldn't fail routing. 
Separating errors from warnings will keep Failed reserved for conditions that truly prevent route generation.
========================================

A few things in that header will not compile cleanly across all of the platforms WeatherRouting supports (MSVC, GCC, Clang, C++17).

1. Missing <cmath> for NAN

You use:

double lat = NAN;
double lon = NAN;


but never include:

#include <cmath>


Better:

#include <limits>

double lat =
    std::numeric_limits<double>::quiet_NaN();

double lon =
    std::numeric_limits<double>::quiet_NaN();


This is portable and constexpr-friendly.

2. Missing <cstddef> for size_t

You use:

template <size_t N>


Add:

#include <cstddef>

3. std::array include is unused

Remove:

#include <array>


since you never use it.

4. Macro line continuation bug

This line:

X(Timeout,                 4002)                                           \
X(InternalError,           4999)


works.

But if someone later appends another error they must remember to add a trailing \.

To make maintenance safer I'd add a final sentinel:

X(InternalError,           4999)


and keep a comment:

/* Last entry intentionally has no '\' */


Otherwise mistakes get hard to diagnose.

5. Missing validation of enum table count

Currently:

RouteError


and

kRouteErrors[]


can drift apart.

I would add:

constexpr std::size_t kRouteErrorCount =
    sizeof(kRouteErrors) /
    sizeof(kRouteErrors[0]);


and later compare against a generated count.

Even better, generate both from the same macro list and never manually maintain a count.

6. RouteErrorName() warning

MSVC may warn:

warning C4715:
not all control paths return a value


because it doesn't always reason about enum exhaustiveness.

Safer:

constexpr std::string_view
RouteErrorName(RouteError error)
{
    switch (error)
    {
#define ERROR_CASE(name, code) \
    case RouteError::name: return #name;

        WR_ROUTE_ERRORS(ERROR_CASE)

#undef ERROR_CASE

        case RouteError::None:
            return "None";

        default:
            return "Unknown";
    }
}

7. Consider making RouteDiagnostic::HasError() constexpr

Instead of:

bool HasError() const
{
    return error != RouteError::None;
}


use:

constexpr bool HasError() const noexcept
{
    return error != RouteError::None;
}


Same for:

IsRunning()
IsFinished()
IsFailure()

8. wxString::Format() type conversion

This:

static_cast<unsigned>(error)


is implementation-dependent because the enum underlying type is uint16_t.

I'd write:

static_cast<unsigned>(
    static_cast<uint16_t>(error))


or:

ErrorCode(error)


Example:

inline wxString FormatErrorCode(RouteError error)
{
    return wxString::Format(
        "WR%04u",
        static_cast<unsigned>(ErrorCode(error)));
}

9. Add noexcept to helpers

Useful throughout:

constexpr uint16_t
ErrorCode(RouteError error) noexcept
{
    return static_cast<uint16_t>(error);
}


and

constexpr bool
IsDataError(RouteError error) noexcept
{
    ...
}

10. Biggest architectural improvement

I would actually add:

enum class RoutePhase
{
    None,

    Configuration,
    WeatherAcquisition,
    RouteInitialization,
    IsochroneExpansion,
    RouteOptimization,
    RouteExtraction,
    Rendering
};


then replace:

wxString phase;


with:

RoutePhase phase =
    RoutePhase::None;


The UI can localize it later.

This avoids dozens of inconsistent strings such as:

"IsoRoute"
"IsoRouteExpansion"
"Iso Route Expansion"
"Expansion"


which inevitably creep in over time.

For WeatherRouting specifically, that change will pay for itself very quickly when you're debugging engine failures.

=============================================
Do this please. Adding a second compile-time validated  enum (5000-5999). Missing wave or current data often shouldn't fail routing. Separating errors from warnings will keep  reserved for conditions that truly prevent route generation.


==========================================

I think this is exactly the right direction for WeatherRouting.

Many "bad data" situations should not cause:

RouteState::Failed


They should produce:

RouteState::Completed


with warnings attached.

For example:

Condition	Error or Warning?Missing polar file	Error
No weather source available	Error
Boat not configured	Error
GRIB unavailable but climatology available	Warning
Wave data unavailable	Warning
Current data unavailable	Warning
Some time steps interpolated	Warning
Partial weather coverage	Warning
RouteWarning Definition

Use the same X-macro pattern as RouteError.

// ============================================================================
// Warning Definitions
// 5000-5999
// ============================================================================

#define WR_ROUTE_WARNINGS(X)                                                   \
                                                                               \
    /* ------------------------------------------------------------ */         \
    /* Weather/Data Quality                                        */         \
    /* ------------------------------------------------------------ */         \
    X(MissingCurrentData,       5000)                                          \
    X(MissingWaveData,          5001)                                          \
    X(MissingTidalData,         5002)                                          \
    X(PartialWeatherCoverage,   5003)                                          \
    X(InterpolatedWeatherData,  5004)                                          \
                                                                               \
    /* ------------------------------------------------------------ */         \
    /* Fallback Operations                                         */         \
    /* ------------------------------------------------------------ */         \
    X(ClimatologyFallback,      5100)                                          \
    X(DefaultPolarUsed,         5101)                                          \
                                                                               \
    /* ------------------------------------------------------------ */         \
    /* Routing Quality                                              */         \
    /* ------------------------------------------------------------ */         \
    X(RouteNearConstraintLimit, 5200)                                          \
    X(LimitedOptimization,      5201)

RouteWarning Enum
enum class RouteWarning : uint16_t
{
    None = 0,

#define DECLARE_WARNING(name, code) \
    name = code,

    WR_ROUTE_WARNINGS(DECLARE_WARNING)

#undef DECLARE_WARNING
};

Compile-Time Validation
struct RouteWarningInfo
{
    RouteWarning warning;
    uint16_t code;
    std::string_view name;
};

constexpr RouteWarningInfo kRouteWarnings[] =
{
#define WARNING_INFO(name, code) \
    { RouteWarning::name, code, #name },

    WR_ROUTE_WARNINGS(WARNING_INFO)

#undef WARNING_INFO
};


Reuse the duplicate-code validator:

static_assert(
    !HasDuplicateCodes(kRouteWarnings),
    "Duplicate RouteWarning codes detected");

Diagnostic Structure

Instead of just:

struct RouteDiagnostic
{
    RouteError error;
};


allow both.

struct RouteWarningEntry
{
    RouteWarning warning = RouteWarning::None;

    wxString detail;

    wxDateTime routeTime;

    double lat =
        std::numeric_limits<double>::quiet_NaN();

    double lon =
        std::numeric_limits<double>::quiet_NaN();
};

Route Status

Add warning accumulation.

#include <vector>

struct RouteStatus
{
    RouteState state = RouteState::Idle;

    RouteDiagnostic diagnostic;

    std::vector<RouteWarningEntry> warnings;

    wxDateTime lastUpdate;

    bool HasWarnings() const
    {
        return !warnings.empty();
    }

    bool HasError() const
    {
        return diagnostic.HasError();
    }
};

UI Behavior

Instead of:

Failed
Missing Wave Data


you'd get:

Completed
2 Warnings


and a tooltip:

WR5000 Missing Current Data
WR5001 Missing Wave Data


while an actual failure remains:

Failed
WR2000 Missing Weather Data

Helper Functions
inline wxString FormatWarningCode(RouteWarning warning)
{
    return wxString::Format(
        "WR%04u",
        static_cast<unsigned>(
            static_cast<uint16_t>(warning)));
}


Examples:

WR5000 Missing Current Data
WR5001 Missing Wave Data
WR5003 Partial Weather Coverage
WR5100 Climatology Fallback
WR5200 Route Near Constraint Limit

Engine API

The compute engine can remain simple:

status.state = RouteState::Completed;

status.warnings.push_back(
{
    RouteWarning::MissingCurrentData,
    _("Current data unavailable")
});


while failures stay explicit:

status.state = RouteState::Failed;

status.diagnostic.error =
    RouteError::MissingWeatherData;


The key rule becomes:

Errors prevent route generation. Warnings describe reduced-quality route generation.

That distinction will make the route list, reports, diagnostics, and future bug reports much cleaner.

=================================================

When the user hits "Compute" check to determine if weather data is available and enabled (Climatology and Grib or xGrib files.  If not prompt the user for the data, or just open Climatology, confirm it is active, and then prompt for a current grib file.


===========================


I like this idea, but I'd frame it as a preflight validation system rather than a simple "missing data" check.

The goal is:

Compute
   ↓
Validate configuration
   ↓
Validate weather sources
   ↓
Offer automatic fixes
   ↓
Compute


instead of:

Compute
   ↓
Fail 3 minutes later

Introduce a Preflight Stage

Add:

enum class RoutePreflightResult
{
    Ok,
    HasWarnings,
    UserCancelled,
    CannotCompute
};


and:

struct RoutePreflightStatus
{
    bool gribAvailable = false;
    bool climatologyAvailable = false;

    bool gribEnabled = false;
    bool climatologyEnabled = false;

    bool weatherAvailable = false;

    std::vector<RouteError> errors;
    std::vector<RouteWarning> warnings;
};

Compute Button Flow

Instead of:

ComputeRoute();


do:

if (!PreflightCompute())
    return;

ComputeRoute();

Weather Source Rules
Accept
GRIB only

Accept
xGrib only

Accept
Climatology only

Best
GRIB + Climatology

Reject
No weather source available

Example Decision Matrix
GRIB	Climatology	ResultYes	Yes	Compute
Yes	No	Compute
No	Yes	Compute
No	No	Stop

When using only Climatology:

RouteWarning::ClimatologyFallback

User Experience
Case A - Nothing Available
Weather Routing requires weather data.

No active GRIB data source was found.
No active Climatology data source was found.

Would you like to:

[Open Climatology]
[Load GRIB File]
[Cancel]

Case B - Climatology Installed But Disabled

Automatically open:

SendPluginMessage("CLIMATOLOGY_PI", "SHOW_DIALOG");


or whatever activation mechanism the plugin exposes.

Then:

Enable Climatology and press Continue.

Case C - Climatology Active, No GRIB

This is the one I like most:

Climatology is active.

No current GRIB forecast is available.

You may:
 - Continue using climatology only
 - Load a GRIB file
 - Cancel


Buttons:

[Continue]
[Load GRIB]
[Cancel]

Weather Quality Score

You could also provide:

enum class WeatherCoverage
{
    FullForecast,
    ForecastPlusClimate,
    ClimateOnly,
    Insufficient
};


Then display:

Weather Sources
---------------
GRIB Forecast: Available
Climatology: Available

Routing Quality: Full Forecast


or

Weather Sources
---------------
GRIB Forecast: Not Available
Climatology: Available

Routing Quality: Climate Only

Error vs Warning

This ties directly into the RouteWarning work.

These should be warnings:

MissingCurrentData
MissingWaveData
PartialWeatherCoverage
ClimatologyFallback


These should be errors:

MissingPolar
MissingBoat
MissingWeatherData
InvalidConfiguration


Only errors prevent compute.

What I Would Do In WeatherRouting

I would add a dedicated:

RoutePreflightStatus
WeatherRouting::ValidateForCompute()


which checks:

Boat/polar configured.
Start and destination valid.
At least one weather source available.
GRIB timeline covers requested route start.
Climatology plugin reachable.
xGrib/GRIB plugin reachable.

Then the Compute button becomes:

switch (ValidateForCompute())
{
case RoutePreflightResult::Ok:
    ComputeRoute();
    break;

case RoutePreflightResult::HasWarnings:
    ShowWeatherWarningDialog();
    break;

case RoutePreflightResult::CannotCompute:
    ShowWeatherSetupDialog();
    break;

case RoutePreflightResult::UserCancelled:
    return;
}


=======================================

That gives users a chance to correct the problem before the engine starts and avoids most of the "Failed - Missing Weather Data" routes that clutter the list.

========================================

I'd make this a Compute Preflight Manager that runs before any route computation starts.

The design goal is:

User presses Compute
       │
       ▼
Validate Configuration
       │
       ▼
Validate Weather Sources
       │
       ▼
Resolve Missing Requirements
       │
       ▼
Compute Routes


rather than allowing routes to be created that immediately fail.

Header
// ComputePreflight.h

#pragma once

#include "RouteStatus.h"

#include <vector>

enum class WeatherSourceAvailability
{
    NotAvailable,
    Available,
    Active
};

enum class RoutePreflightResult
{
    Ok,
    HasWarnings,
    UserCancelled,
    CannotCompute
};

struct RoutePreflightStatus
{
    // Weather

    WeatherSourceAvailability climatology =
        WeatherSourceAvailability::NotAvailable;

    WeatherSourceAvailability grib =
        WeatherSourceAvailability::NotAvailable;

    WeatherSourceAvailability xgrib =
        WeatherSourceAvailability::NotAvailable;

    bool weatherAvailable = false;

    // Validation

    std::vector<RouteError> errors;
    std::vector<RouteWarning> warnings;

    bool HasErrors() const noexcept
    {
        return !errors.empty();
    }

    bool HasWarnings() const noexcept
    {
        return !warnings.empty();
    }
};

Core Validation Function
RoutePreflightStatus
WeatherRouting::ValidateForCompute() const
{
    RoutePreflightStatus result;

    // ------------------------------------------------------
    // Boat
    // ------------------------------------------------------

    if (!BoatIsConfigured())
        result.errors.push_back(
            RouteError::MissingBoat);

    if (!PolarIsAvailable())
        result.errors.push_back(
            RouteError::MissingPolar);

    // ------------------------------------------------------
    // Positions
    // ------------------------------------------------------

    if (!StartPositionValid())
        result.errors.push_back(
            RouteError::MissingStartPosition);

    if (!DestinationPositionValid())
        result.errors.push_back(
            RouteError::MissingDestination);

    // ------------------------------------------------------
    // Weather Sources
    // ------------------------------------------------------

    if (IsClimatologyInstalled())
    {
        result.climatology =
            IsClimatologyEnabled()
            ? WeatherSourceAvailability::Active
            : WeatherSourceAvailability::Available;
    }

    if (IsGribDataAvailable())
    {
        result.grib =
            WeatherSourceAvailability::Active;
    }

    if (IsXGribDataAvailable())
    {
        result.xgrib =
            WeatherSourceAvailability::Active;
    }

    result.weatherAvailable =
        result.climatology ==
            WeatherSourceAvailability::Active ||
        result.grib ==
            WeatherSourceAvailability::Active ||
        result.xgrib ==
            WeatherSourceAvailability::Active;

    if (!result.weatherAvailable)
    {
        result.errors.push_back(
            RouteError::MissingWeatherData);
    }

    // ------------------------------------------------------
    // Quality Warnings
    // ------------------------------------------------------

    if (!CurrentDataAvailable())
    {
        result.warnings.push_back(
            RouteWarning::MissingCurrentData);
    }

    if (!WaveDataAvailable())
    {
        result.warnings.push_back(
            RouteWarning::MissingWaveData);
    }

    return result;
}

Compute Button Flow

Current code is probably roughly:

void WeatherRouting::OnCompute(wxCommandEvent&)
{
    ComputeSelected();
}


Refactor to:

void WeatherRouting::OnCompute(wxCommandEvent&)
{
    RoutePreflightStatus status =
        ValidateForCompute();

    RoutePreflightResult result =
        ResolvePreflight(status);

    switch (result)
    {
        case RoutePreflightResult::Ok:
        case RoutePreflightResult::HasWarnings:
            ComputeSelected();
            return;

        case RoutePreflightResult::CannotCompute:
        case RoutePreflightResult::UserCancelled:
            return;
    }
}

Weather Resolution Dialog

The important case:

No Weather Data Available

Weather Routing requires at least one
active weather source.

Climatology: Installed but inactive
GRIB: Not loaded

Choose an action:

[Open Climatology]
[Load GRIB]
[Cancel]


Implementation:

RoutePreflightResult
WeatherRouting::ResolvePreflight(
    const RoutePreflightStatus& status)
{
    if (!status.HasErrors())
    {
        if (status.HasWarnings())
        {
            ShowPreflightWarnings(status);
            return RoutePreflightResult::HasWarnings;
        }

        return RoutePreflightResult::Ok;
    }

    if (ContainsError(
            status,
            RouteError::MissingWeatherData))
    {
        return ResolveMissingWeather();
    }

    ShowPreflightErrors(status);

    return RoutePreflightResult::CannotCompute;
}

Automatic Climatology Activation

If Climatology is installed but inactive:

RoutePreflightResult
WeatherRouting::ResolveMissingWeather()
{
    if (IsClimatologyInstalled() &&
        !IsClimatologyEnabled())
    {
        OpenClimatologyPlugin();

        if (AskUser(
                _("Enable climatology and continue?")))
        {
            return RoutePreflightResult::UserCancelled;
        }
    }

    ...
}

GRIB Acquisition Prompt
if (!IsGribDataAvailable())
{
    int answer =
        OCPNMessageBox(
            _("No current GRIB forecast loaded.\n"
              "Load a GRIB file now?"),
            _("Weather Routing"),
            wxYES_NO);

    if (answer == wxYES)
    {
        OpenGribPlugin();
        return RoutePreflightResult::UserCancelled;
    }
}

Preferred User Experience
Best Case
Climatology Active
GRIB Loaded
Current Data Available
Wave Data Available

→ Compute

Climate Only
Climatology Active
GRIB Not Loaded

Warning:
Forecast quality reduced.

[Continue]
[Load GRIB]

Nothing Available
No weather data available.

[Open Climatology]
[Load GRIB]
[Cancel]

Fatal Configuration Error
No boat polar selected.

[Open Boat Configuration]
[Cancel]


The key architectural change is that MissingWeatherData should never become a route failure if it can be detected before computation starts. In the new model it becomes a preflight error, keeping the route list clean and reserving RouteState::Failed for genuine runtime failures during route generation.

====================================

There are a few architectural problems in my previous draft once you consider how WeatherRouting, Climatology, and GRIB/xGrib actually work inside OpenCPN.

Problem #1: "Enable Climatology"

This assumption is probably wrong:

IsClimatologyInstalled() &&
!IsClimatologyEnabled()


Climatology is not typically a required continuously-running weather source in the same sense as a loaded GRIB forecast.

What you really want to know is:

Can the plugin provide weather data right now?


not

Is its dialog visible?


So I'd replace:

bool IsClimatologyEnabled();


with:

bool IsClimatologyAvailable();


which verifies:

plugin loaded
API reachable
weather queries return data

The dialog state should not matter.

Problem #2: Opening Climatology Is Not Activation

This sequence is weak:

OpenClimatologyPlugin();
Enable climatology and continue?


The user shouldn't have to understand plugin internals.

Instead:

OpenClimatologyConfiguration();


only when:

!IsClimatologyAvailable()


and then re-run validation after the dialog closes.

Problem #3: GRIB Is Different

For GRIB/xGrib, availability is tied to an actual forecast file.

You need something closer to:

enum class WeatherSourceStatus
{
    Unavailable,
    Available,
    Active,
    NoDataLoaded
};


Example:

GRIB plugin installed
GRIB plugin loaded
NO forecast loaded


should report:

NoDataLoaded


not:

Unavailable

Better Weather Detection
struct WeatherAvailability
{
    bool climatologyAvailable = false;

    bool gribPluginAvailable = false;
    bool gribForecastLoaded = false;

    bool xgribPluginAvailable = false;
    bool xgribForecastLoaded = false;
};


Validation:

WeatherAvailability
WeatherRouting::DetectWeatherSources() const
{
    WeatherAvailability w;

    w.climatologyAvailable =
        ClimatologyCanProvideData();

    w.gribPluginAvailable =
        IsGribPluginAvailable();

    w.gribForecastLoaded =
        IsGribForecastLoaded();

    w.xgribPluginAvailable =
        IsXGribPluginAvailable();

    w.xgribForecastLoaded =
        IsXGribForecastLoaded();

    return w;
}

Weather Setup Dialog

Instead of several message boxes, I'd build one routing-specific dialog.

enum class WeatherDialogResult
{
    Continue,
    LoadGrib,
    OpenClimatology,
    Cancel
};

Example 1

No forecast loaded.

Weather Sources

✓ Climatology available

✗ No GRIB forecast loaded

Forecast routing works best with a current GRIB.

Choose an action:

[Load GRIB]
[Continue With Climatology]
[Cancel]

Example 2

Nothing available.

Weather Sources

✗ Climatology unavailable

✗ No GRIB forecast

Weather Routing cannot compute
without any weather source.

[Open Climatology]
[Load GRIB]
[Cancel]

Handlers
WeatherDialogResult
WeatherRouting::HandleWeatherSetup(
    const WeatherAvailability& weather)
{
    WeatherSetupDialog dlg(this, weather);

    const int rc = dlg.ShowModal();

    switch (rc)
    {
        case ID_LOAD_GRIB:
            OpenGribDialog();
            return WeatherDialogResult::LoadGrib;

        case ID_OPEN_CLIMATOLOGY:
            OpenClimatologyDialog();
            return WeatherDialogResult::OpenClimatology;

        case wxID_OK:
            return WeatherDialogResult::Continue;

        default:
            return WeatherDialogResult::Cancel;
    }
}

Compute Flow

The flow then becomes:

void WeatherRouting::OnCompute(wxCommandEvent&)
{
    for (;;)
    {
        RoutePreflightStatus status =
            ValidateForCompute();

        if (!status.HasErrors())
        {
            ComputeSelected();
            return;
        }

        if (!ContainsError(
                status,
                RouteError::MissingWeatherData))
        {
            ShowPreflightErrors(status);
            return;
        }

        switch (HandleWeatherSetup(
                    DetectWeatherSources()))
        {
            case WeatherDialogResult::Continue:
                continue;

            case WeatherDialogResult::LoadGrib:
                continue;

            case WeatherDialogResult::OpenClimatology:
                continue;

            case WeatherDialogResult::Cancel:
                return;
        }
    }
}

WeatherRouting-Specific Enhancement

Since WeatherRouting supports both forecast and climatology routing, I'd add a user preference:

enum class WeatherRequirement
{
    AnyWeatherSource,
    PreferForecast,
    RequireForecast
};


Then:

RequireForecast


would force a GRIB/xGrib file before computation.

PreferForecast


would warn but continue.

AnyWeatherSource


would allow climatology-only routing.

================================

That would make the preflight system flexible enough for offshore routing, climatology studies, and everyday forecast routing without cluttering the route list with avoidable failures.

====================================

First user support default button:
Climatology is a preferred data source for Learning and should be enabled when the plugin is first used or installed.  There should be a button for First user support which sets all settings to a condition that will produce a route, using Climatology only.

=====================================

I think this is an excellent idea, and I'd actually elevate it from a weather dialog option to a first-run recovery mode.

Your observation is important:

New users should be able to produce a route immediately.

Today the typical failure chain is:

Install WeatherRouting
    ↓
Press Compute
    ↓
Missing GRIB
Missing Current
Missing Wave
Missing Polar
Missing Configuration
    ↓
User gives up


For learning, demonstrations, club training, documentation, and first use, Climatology should be the "safe default."

New Concept: First User Support Mode

Add:

enum class ComputeSetupMode
{
    Standard,
    FirstUserSupport
};


and:

bool m_FirstRunDetected;


stored in plugin configuration.

Preflight Dialog

When routing prerequisites are missing:

Weather Routing Setup

No weather forecast is currently loaded.

Weather Routing supports:

• GRIB Forecast Routing
• xGrib Forecast Routing
• Climatology Routing

For first-time users we recommend
Climatology Routing.

[ First User Support ]
[ Load GRIB ]
[ Cancel ]


Default button:

First User Support

What First User Support Does

The goal is:

Guaranteed Route Generation


not:

Perfect Routing Configuration


Implementation:

void WeatherRouting::ApplyFirstUserSupport()
{
    //
    // Weather
    //

    EnableClimatologyRouting();

    DisableGribRequirement();

    //
    // Constraints
    //

    EnableDefaultWindLimits();

    DisableAdvancedWaveLimits();

    //
    // Routing
    //

    SetRouteTimeNow();

    EnableClimatologyFallback();

    //
    // UI
    //

    ShowFirstUserSupportBanner();
}

Climatology Side

If Climatology is installed:

if (!ClimatologyCanProvideData())
{
    OpenClimatologyDialog();
}


When the dialog closes:

ValidateForCompute();


again.

If Climatology Is Missing

Display:

Climatology plugin not available.

Would you like to:

[ Install Climatology ]
[ Load GRIB ]
[ Cancel ]

User Messaging

After configuration:

First User Support Enabled

Weather Routing has been configured
to use Climatology weather data.

This configuration is intended for:

• Learning
• Training
• Route experimentation
• Verifying plugin operation

For best forecasting accuracy,
load a GRIB file and rerun the route.

New Warning Type

I would add:

X(FirstUserSupportMode, 5102)


to RouteWarning.

Then routes generated this way report:

Completed

Warnings:
WR5102 First User Support Mode
WR5100 Climatology Fallback


without being considered failures.

Compute Flow
Compute
   ↓
Validate
   ↓
No weather available
   ↓
Weather Setup Dialog
   ↓
First User Support
   ↓
Enable Climatology
Configure Safe Defaults
   ↓
Revalidate
   ↓
Compute Route

Even Better

I would make this a toolbar command:

Help
 └── First User Support Setup...


so when someone posts:

Weather Routing won't compute

the answer becomes:

Help → First User Support Setup


and 90% of first-user configuration problems disappear.

For SHYC training, documentation, and general onboarding, I think a "Guarantee the user can compute a climatology route" button is far more valuable than asking them to locate a GRIB file before they've ever successfully generated a route.

===========================================

Define exact first-user default settings  and implement the support button handler  and Add the Climatology only validation path.   Also we have standard boat.xml setups that were intentionally used for this, such that 1. First User routes were defined using climatology only. 2. A predefined Boat.xml using specific plars is set. 3. Any other necessary setting are in place. The WR package that is installed and set so that when the user highlights the Route Configuration and hits "Compute" the optimal route is created.

=============================================

This is better than a "First User Support Mode". I would turn it into a formal, repeatable configuration profile:

Compute Profiles
──────────────────────────
• First User (Default)
• Climatology Study
• Forecast Routing
• Advanced Routing


The installed default should be:

First User


with the explicit design requirement:

A newly installed WeatherRouting + Climatology system should produce a valid route immediately after the user selects start/end points and presses Compute.

First User Default Configuration
Weather Sources
UseClimatology = true;
RequireGrib = false;
RequireXGrib = false;
AllowClimatologyFallback = true;


Validation rule:

if (ClimatologyCanProvideData())
{
    weather_ok = true;
}
else
{
    weather_ok = false;
}


No GRIB required.

No forecast required.

No user prompt for GRIB.

Boat Selection

You mentioned this already exists.

WeatherRouting ships with:

Boat.xml


specifically to guarantee route generation.

On first-user setup:

if (configuration.boatFile.empty())
{
    configuration.boatFile =
        GetDefaultBoatXMLPath();
}


Validation:

if (!BoatFileExists())
{
    ApplyFirstUserBoat();
}

Routing Constraints

Avoid settings that commonly prevent routes.

Disable:

WaveHeightLimit
WindLimit
CurrentLimit
AdvancedPerformanceConstraints


or set them to permissive values.

Example:

configuration.MaxWindKnots = 999.0;
configuration.MaxWaveHeight = 999.0;


until the user intentionally changes them.

Time Settings

Always valid:

configuration.StartTime =
    wxDateTime::Now();

configuration.TimeStep =
    1.0;


No dependency on GRIB timeline.

Data Sources

Prefer:

Climatology


because:

always available
global coverage
predictable
suitable for learning
reproducible tutorials

which aligns with your stated objective.

New Validation Mode

Add:

enum class RoutingProfile
{
    FirstUser,
    ClimatologyStudy,
    ForecastRouting,
    AdvancedRouting
};


Store on each configuration.

configuration.Profile =
    RoutingProfile::FirstUser;

Climatology-Only Validation Path

Current validation:

MissingWeatherData


should become:

bool WeatherRouting::ValidateWeather(
    const RouteMapConfiguration& cfg)
{
    switch (cfg.Profile)
    {
    case RoutingProfile::FirstUser:

        return ClimatologyCanProvideData();

    case RoutingProfile::ClimatologyStudy:

        return ClimatologyCanProvideData();

    case RoutingProfile::ForecastRouting:

        return IsGribForecastLoaded() ||
               IsXGribForecastLoaded();

    case RoutingProfile::AdvancedRouting:

        return ValidateAdvancedSources();
    }

    return false;
}

First User Support Button

Toolbar:

Help
 └── First User Setup...


or

Route Configuration
 └── First User Setup


Handler:

void WeatherRouting::OnFirstUserSetup(
    wxCommandEvent&)
{
    ApplyFirstUserDefaults();

    wxMessageBox(
        _("Weather Routing has been configured "
          "for first-time use.\n\n"
          "Climatology will be used as the "
          "weather source.\n"
          "The default Boat.xml polar has "
          "been selected.\n\n"
          "You can now select a route and "
          "press Compute."),
        _("Weather Routing"),
        wxOK | wxICON_INFORMATION);
}

Implementation
void WeatherRouting::ApplyFirstUserDefaults()
{
    RouteMapConfiguration& cfg =
        m_ConfigurationDialog.Configuration();

    cfg.Profile = RoutingProfile::FirstUser;

    //
    // Weather
    //

    cfg.UseClimatology = true;
    cfg.RequireGrib = false;

    //
    // Boat
    //

    cfg.boatFile =
        GetDefaultBoatXMLPath();

    //
    // Constraints
    //

    cfg.MaxWindKnots = 999.0;
    cfg.MaxWaveHeight = 999.0;

    //
    // Start Time
    //

    cfg.StartTime =
        wxDateTime::Now();

    SaveXML();
}

Compute Flow
Compute
   ↓
ValidateForCompute()
   ↓
Profile = FirstUser
   ↓
Check Boat.xml
   ↓
Check Climatology
   ↓
Success
   ↓
Compute Route


No GRIB dialogs.

No forecast dialogs.

No weather source selection.

No warnings.

Just:

Select Route
Press Compute
Get Route


which is exactly what a first-time WeatherRouting user should experience. For training environments, club instruction, documentation, and onboarding, this profile becomes the reference configuration against which all tutorials and screenshots are created.

=======================

Specify the bundled Boat.xml polar
Define route-selection defaults
Add tests for first-user computation
Have First-user Rest button.
Simplify the dialogs to use only what is needed for the first user experience.

===========================

This is where I would stop thinking about "error recovery" and start thinking about a formal First User Profile.

The requirement becomes:

A new user installs WeatherRouting + Climatology, defines a route, presses Compute, and gets a route with no knowledge of GRIBs, currents, waves, polar selection, or advanced constraints.

That requirement should drive everything.

1. Specify the Bundled Polar

The existing generic Boat.xml is serving a very important purpose:

Not accuracy.
Predictability.


I would formalize it:

constexpr char FIRST_USER_POLAR[] =
    "Boat.xml";


and never allow the first-user setup to choose anything else.

The profile should always set:

cfg.boatFile = GetDataDir() +
               wxFILE_SEP_PATH +
               "boats" +
               wxFILE_SEP_PATH +
               "Boat.xml";


Validation:

if (!wxFileExists(cfg.boatFile))
{
    AddError(RouteError::MissingPolar);
}


No boat chooser.

No questions.

No dialog.

2. Route Selection Defaults

These should bias toward successful computation.

Weather
UseClimatology = true;

PreferGrib = false;

RequireForecast = false;

RequireCurrentData = false;

RequireWaveData = false;

Routing
DetectLand = true;

OptimizeTacking = true;

OptimizeVMG = true;

Constraints

Very permissive:

MaxWind = 999.0;

MaxWaveHeight = 999.0;

MaxCurrent = 999.0;

Start Time
wxDateTime::Now();

Data Sources
Climatology only


required.

GRIB optional

3. First User Compute Validation Path

Existing:

ValidateForCompute()


branches based on profile.

switch(cfg.profile)
{
    case RoutingProfile::FirstUser:
        return ValidateFirstUser();

    case RoutingProfile::Forecast:
        return ValidateForecast();

    case RoutingProfile::Advanced:
        return ValidateAdvanced();
}


First User Validation:

bool WeatherRouting::ValidateFirstUser()
{
    return
        StartPositionValid() &&
        DestinationPositionValid() &&
        BoatXmlExists() &&
        ClimatologyCanProvideData();
}


Nothing else.

No:

GRIB
Wave
Current
Forecast span
Forecast age


checks.

4. First User Reset Button

I'd put this in two places.

Route Configuration Dialog

Button:

[Reset to First User Defaults]

Help Menu
Help
 └── Reset Weather Routing to First User Defaults


Handler:

void WeatherRouting::OnResetToFirstUserDefaults(
    wxCommandEvent&)
{
    ApplyFirstUserDefaults();

    wxMessageBox(
        _("Weather Routing has been reset "
          "to the First User profile."),
        _("Weather Routing"),
        wxOK | wxICON_INFORMATION);
}


Implementation:

void WeatherRouting::ApplyFirstUserDefaults()
{
    RouteMapConfiguration& cfg =
        CurrentConfiguration();

    cfg.Profile = RoutingProfile::FirstUser;

    cfg.boatFile =
        GetDefaultBoatXml();

    cfg.UseClimatology = true;

    cfg.RequireForecast = false;

    cfg.RequireCurrentData = false;

    cfg.RequireWaveData = false;

    cfg.StartTime =
        wxDateTime::Now();

    SaveXML();
}

5. First User Tests

These are some of the most valuable tests the project can have.

Test 1

Fresh installation.

TEST(FirstUser, DefaultConfigurationLoads)


Verify:

Boat.xml selected
Climatology enabled
Profile == FirstUser

Test 2

Compute succeeds with Climatology only.

TEST(FirstUser, ComputeUsingClimatologyOnly)


Setup:

No GRIB
No xGrib

Climatology available


Expect:

ValidateForCompute() == Ok

Test 3

GRIB absent does not fail.

TEST(FirstUser, MissingGribAllowed)


Expect:

No MissingWeatherData error

Test 4

Missing Boat.xml fails.

TEST(FirstUser, MissingBoatXmlFails)


Expect:

MissingPolar

Test 5

Route computes after reset.

TEST(FirstUser, ResetRestoresComputableState)


Setup:

User breaks configuration.


Call:

ApplyFirstUserDefaults();


Verify:

ValidateForCompute() == Ok;

6. Simplify Dialogs

For First User profile, hide most of the advanced options.

Instead of showing:

Current Source
Wave Source
Boundary Detection
Tacking Strategy
Time Interpolation
VMG Optimization
Isochrone Density


show only:

First User Routing

Boat:
    Boat.xml

Weather:
    Climatology

Start Time:
    Now

[Compute]
[Advanced...]


Advanced users already know where to find advanced settings.

First users do not.

Recommendation

I would make First User a first-class profile stored in RouteMapConfiguration:

enum class RoutingProfile
{
    FirstUser,
    Climatology,
    Forecast,
    Advanced
};


and guarantee this invariant:

ApplyFirstUserDefaults();

ValidateForCompute();


must always return:

RoutePreflightResult::Ok


on a system with:

WeatherRouting installed
Climatology installed
Boat.xml present


That gives you a deterministic onboarding experience and makes documentation, tutorials, SHYC training sessions, and support dramatically simpler.

========================================

Then build up from there with Intermediate-User experience and Advanced-user experience.​‌

Additionally, the menus and dialog must be simplified​‌
1. Help and Learning buttons using (?) are used throughout the dialogs so that the user can click or hoover to learn about the setting and what it does.​‌
2. The menu settings can then be formatted more appropriately and condensed without excessive text.​‌
3. The menu system should be designed for First-user or "Basic", "Intermediate" and "Advanced", not necessarily as tabs, but perhaps so. I am thinking in terms of  the  Basic menu filling out with more selections and parameters for Intermediate users. Then perhaps the "Advanced settings are put in the Advanced tab and that becomes available.​‌
4. As I said all of these settings need refinement, consolidation and improvement​‌

===========================================

I think you're actually describing a much bigger redesign than "First User Settings". You're describing a progressive-disclosure user experience model for WeatherRouting.

The current WeatherRouting architecture grew over many years and accumulated features, so the configuration dialog is effectively exposing implementation details instead of exposing user goals.

A new user wants:

I want to sail from A to B.


They do not want:

Isochrone density
Data source priority
Interpolation strategy
VMG optimization
Land avoidance tuning
Current blending
Wave limits


Those should appear later.

User Experience Levels

Instead of:

RoutingProfile


I'd introduce:

enum class UserExperienceLevel
{
    FirstUser,
    Intermediate,
    Advanced
};


stored globally in plugin settings.

First User

Goal:

Learn WeatherRouting


Visible sections:

------------------------------------------------
Weather Routing
------------------------------------------------

Route:
    Start
    Destination

Boat:
    Boat.xml

Weather:
    Climatology

[ Compute ]

[ ? What is Climatology? ]
[ Switch to Intermediate ]


Nothing else.

No GRIB selection.

No polar selector.

No constraints.

No advanced routing strategy.

No forecast timeline.

Intermediate

Goal:

Use WeatherRouting regularly


Now expose:

------------------------------------------------
Boat
------------------------------------------------

Polar File

------------------------------------------------
Weather
------------------------------------------------

Use Climatology

Use GRIB

Use xGrib

------------------------------------------------
Constraints
------------------------------------------------

Maximum Wind

Maximum Waves

------------------------------------------------
Compute
------------------------------------------------


Everything gets help icons.

Example:

Maximum Wind  [ ? ]


Hover:

Do not route through regions
with winds stronger than this.

Advanced

Goal:

Tune routing behavior


Expose everything.

Weather Blending

Isochrone Parameters

Optimization Strategy

Current Weighting

Interpolation Controls

Routing Resolution

Advanced Constraints

Dialog Layout

I would avoid tabs at first.

Instead:

Basic

Intermediate

Advanced


as a single drop-down in the upper corner:

View Level:
[ Basic ▼ ]


Changing it dynamically expands the dialog.

This avoids:

Basic Tab
Advanced Tab

Advanced Tab still visible


which tempts beginners to go there.

Help System

I strongly agree with your proposal.

Every non-obvious control gets:

[ ? ]


implemented as:

wxBitmapButton


or modern help icons.

Hover:

wxToolTip


Click:

HelpDialog(topic);


Example:

Boat Polar [?]


Click:

A boat polar describes how fast
your boat sails at different wind
speeds and wind angles.

The default Boat.xml is suitable
for learning and demonstrations.

Contextual Learning

Instead of sending people to manuals:

What is GRIB?          [?]
What is Climatology?   [?]
What is a Polar?       [?]


Each help popup:

1-minute explanation


plus:

Learn More...


linking to documentation.

First User Reset

I would make this highly visible.

Toolbar:

First User Setup


Menu:

Help
 └── Reset to First User Mode


Dialog:

Restore WeatherRouting to the
recommended learning configuration?

Boat: Boat.xml
Weather: Climatology
Routing: Default

[ Restore ]
[ Cancel ]

First User Compute Guarantee

This should become an explicit design requirement.

Given:

WeatherRouting Installed
Climatology Installed
Boat.xml Present
Start Position Defined
Destination Defined


then:

Select Route Configuration
Press Compute


must always succeed.

No dialogs.

No weather errors.

No missing GRIB errors.

No missing-wave warnings.

Just:

Computing...


followed by:

Route Complete

Proposed Documentation Definition

I would actually write this into the design document:

WeatherRouting UX Requirement #1

A first-time user shall be able
to generate a valid route using:

  • bundled Boat.xml
  • climatology weather data
  • default settings

without loading a GRIB file,
editing a polar,
or changing advanced settings.

======================================================
Everything else in the UI should be designed around preserving that guarantee.

In my opinion, this may be the single highest-value usability improvement possible for WeatherRouting because it aligns the software with how sailors actually learn: first make one successful route, then learn what all the controls mean.

=======================================================

Draft the three-level settings matrix, Design the simplified dialog layout, Define contextual help topics and text

=========================================================

This is becoming a full UX strategy, and I think it's exactly the right direction for WeatherRouting. The current system is largely feature-oriented, while what you're proposing is a user-oriented workflow.

The guiding principle should be:

WeatherRouting should teach users progressively, not confront them with every capability on day one.

1. Three-Level Settings Matrix
First User (Basic)

Goal: Compute a successful route in under 5 minutes

Visible Settings:

Route
Start Position
Destination Position
Departure Time
Boat
Boat: Boat.xml
Weather
Climatology Enabled
Actions
Compute
Reset to First User Defaults
Learn Weather Routing

Everything else hidden.

Validation

Required:

Start Position
Destination Position
Boat.xml
Climatology Available


No GRIB requirements.

No wave requirements.

No current requirements.

No advanced constraints.

Intermediate User

Goal: Use WeatherRouting regularly

Everything in Basic plus:

Boat
Polar Selection
Weather
Climatology
GRIB
xGrib
Weather Source Priority
Constraints
Max Wind
Max Wave Height
Max Current
Route Preferences
Avoid Land
Optimize VMG
Tacking Preference
Results
Route Statistics

Hidden:

Isochrone internals
Optimization tuning
Interpolation controls
Experimental settings

Advanced User

Goal: Tune routing algorithm behavior

Everything exposed.

Optimization
Isochrone Spacing
Time Step
Position Granularity
Pruning Controls
Weather
Interpolation Strategy
Weather Blending
Data Weighting
Constraints
All constraint parameters
Experimental
Advanced engine settings
Debugging and diagnostics
2. Simplified Configuration Dialog

I would not use fixed tabs.

Instead:

+--------------------------------------------------+
| Route Configuration                              |
+--------------------------------------------------+

Experience Level:
[ Basic ▼ ]

----------------------------------------------------
Route
----------------------------------------------------

Start        [________________]

Destination  [________________]

Departure    [ Now ▼ ]

----------------------------------------------------
Boat
----------------------------------------------------

Boat.xml

[ ? ]

----------------------------------------------------
Weather
----------------------------------------------------

Climatology

[ ? ]

----------------------------------------------------

[ Compute ]
[ Reset ]
[ Learn ]


Changing:

Experience Level


to

Intermediate


causes additional groups to appear.

Changing to:

Advanced


reveals all remaining groups.

3. Help System Architecture

Every non-obvious control gets:

[ ? ]

Hover

One-sentence explanation.

Click

A compact help dialog.

4. Contextual Help Topics
What is Weather Routing?
Weather Routing predicts the fastest
or safest sailing route using weather
data and your boat's performance.

Choose a start point, a destination,
and press Compute to generate a route.

What is Climatology?
Climatology uses long-term averaged
weather conditions from historical data.

It provides worldwide coverage and is
ideal for learning, practice, and route
planning when a forecast is unavailable.

What is a GRIB File?
A GRIB file contains weather forecasts
including wind, pressure, waves, and
other conditions.

GRIB routing is generally more accurate
than climatology for short-term planning.

What is a Boat Polar?
A polar describes how fast a boat sails
at different wind speeds and angles.

Weather Routing uses the polar to estimate
boat speed while computing a route.

Boat.xml is the default learning boat and
works well for first-time users.

Wind Limit
The route will avoid areas where wind
speeds exceed this value.

Leave this disabled unless you have a
specific operational limit.

Wave Limit
The route will avoid areas where wave
heights exceed this value.

Large wave limits can significantly
alter route selection.

Current Data
Current data describes the movement of
water.

When available it can improve route
accuracy, especially near coasts and
strong tidal regions.

Departure Time
The route is calculated from this time.

For climatology routing the exact time
is less important than for forecast-based
routing.

VMG Optimization
VMG means Velocity Made Good.

This option helps choose headings that
maximize progress toward the destination.

5. Learn Button

In Basic mode:

[ Learn ]


opens:

------------------------------------------------
Getting Started
------------------------------------------------

1. Select Start
2. Select Destination
3. Press Compute

Weather Routing is currently using:

✓ Boat.xml
✓ Climatology

This configuration is designed to
produce a route with minimal setup.

[ Next ]

6. First User Reset

Every dialog should expose:

Reset to First User Defaults


This becomes the support team's universal solution.

When a user says:

Weather Routing is not working.

Support can simply say:

Press:
Reset to First User Defaults

Then try Compute again.

7. Core UX Requirement

I would explicitly document:

UX-001 First User Success

Given:

WeatherRouting installed
Climatology installed
Boat.xml available
Start position selected
Destination selected


The following workflow shall succeed:

Open Route Configuration
Select route
Press Compute


without:

Loading GRIB data
Selecting a polar
Editing constraints
Opening advanced dialogs


Everything in the UI should be evaluated against whether it helps or hurts that requirement.

This would give WeatherRouting a much clearer progression:

Basic
  ↓
Intermediate
  ↓
Advanced


instead of presenting an advanced routing workstation to someone who is simply trying to create their first route.

============================================
Turn this into implementation tickets, Create wireframes for each user level, Map settings to existing controls.   Additionally some version of  First-user Basic can become the favorite Android interface.

=============================================
This is large enough that I would break it into an explicit UX modernization project with three epics.

Epic 1: Progressive User Experience
WR-UX-001 User Experience Levels
Objective

Support three experience levels:

enum class UserExperienceLevel
{
    FirstUser,
    Intermediate,
    Advanced
};


Stored globally, not per-route.

Acceptance Criteria
User can switch levels from Preferences.
Changes are immediate.
Existing routes remain compatible.
Hidden settings retain values.
WR-UX-002 First User Profile
Objective

Guarantee successful route generation.

Configuration
Weather:
    Climatology

Boat:
    Boat.xml

Forecast:
    Not required

Constraints:
    Disabled

Advanced:
    Hidden

Acceptance Criteria

Given:

WeatherRouting installed
Climatology installed
Boat.xml exists


Then:

Start
Destination
Compute


must create a route.

WR-UX-003 Intermediate Profile
Objective

Expose operational settings.

Visible:

Boat Polar
Weather Source Selection
Wind Limit
Wave Limit
Current Limit
Optimization Mode


Hidden:

Isochrone Parameters
Interpolation Strategy
Engine Diagnostics

WR-UX-004 Advanced Profile
Objective

Expose all routing controls.

Visible:

Everything

Epic 2: Dialog Simplification
WR-UI-001 Replace Current Dialog Structure

Current dialogs expose implementation details.

New structure:

Route Configuration


contains progressive sections.

Basic Wireframe
+--------------------------------------------------+
| Weather Routing                                  |
+--------------------------------------------------+

Level:
[ Basic ▼ ]

----------------------------------------------------
Route
----------------------------------------------------

Start
[____________________________]

Destination
[____________________________]

Departure
[ Now ▼ ] [?]

----------------------------------------------------
Boat
----------------------------------------------------

Boat.xml [?]

----------------------------------------------------
Weather
----------------------------------------------------

Climatology [?]

----------------------------------------------------

[ Compute ]

[ Reset to First User ]

[ Learn ]

Intermediate Wireframe
+--------------------------------------------------+
| Weather Routing                                  |
+--------------------------------------------------+

Level:
[ Intermediate ▼ ]

----------------------------------------------------
Route
----------------------------------------------------

Start
Destination
Departure

----------------------------------------------------
Boat
----------------------------------------------------

Polar File                    [?]

----------------------------------------------------
Weather
----------------------------------------------------

Use Climatology              [?]
Use GRIB                     [?]
Use xGrib                    [?]

----------------------------------------------------
Constraints
----------------------------------------------------

Maximum Wind                 [?]

Maximum Waves                [?]

----------------------------------------------------

[ Compute ]

Advanced Wireframe
+--------------------------------------------------+
| Weather Routing                                  |
+--------------------------------------------------+

Level:
[ Advanced ▼ ]

----------------------------------------------------
Route
----------------------------------------------------

----------------------------------------------------
Boat
----------------------------------------------------

----------------------------------------------------
Weather
----------------------------------------------------

----------------------------------------------------
Constraints
----------------------------------------------------

----------------------------------------------------
Optimization
----------------------------------------------------

Isochrone Step               [?]
Time Step                    [?]
Pruning                      [?]

----------------------------------------------------
Diagnostics
----------------------------------------------------

Enable Logging               [?]

Enable Engine Tracing        [?]

----------------------------------------------------

[ Compute ]

Epic 3: Help and Learning System
WR-HELP-001 Universal Help Icon

Every advanced setting receives:

[ ? ]


Hover:

1 sentence explanation


Click:

Detailed help dialog

Help Topic Matrix
Weather Routing
Weather Routing computes a sailing
route using weather conditions and
boat performance.

Start with a route and press Compute.

Climatology
Climatology uses long-term historical
weather averages.

It is ideal for learning and practice
because coverage is always available.

GRIB
A GRIB file contains weather forecasts.

GRIB routing is usually preferred for
actual voyage planning.

Boat Polar
A boat polar defines performance at
different wind speeds and angles.

Boat.xml is supplied as the standard
learning boat.

Wind Limit
Route generation avoids regions
exceeding this wind speed.

Wave Limit
Route generation avoids regions
exceeding this wave height.

VMG Optimization
Velocity Made Good optimization seeks
the fastest progress toward destination.

Isochrone Step
Controls routing resolution.

Smaller values improve precision but
require more computation.

Mapping to Existing Controls
Existing Control	Basic	Intermediate	AdvancedStart Position	✓	✓	✓
Destination	✓	✓	✓
Departure Time	✓	✓	✓
Boat.xml	✓	✓	✓
Boat Polar Selection	Hidden	✓	✓
Climatology	✓	✓	✓
GRIB Selection	Hidden	✓	✓
xGrib Selection	Hidden	✓	✓
Wind Limits	Hidden	✓	✓
Wave Limits	Hidden	✓	✓
Current Limits	Hidden	✓	✓
Route Statistics	Hidden	✓	✓
Isochrone Controls	Hidden	Hidden	✓
Interpolation Controls	Hidden	Hidden	✓
Debug Logging	Hidden	Hidden	✓
Engine Diagnostics	Hidden	Hidden	✓


===================================================
Android Strategy
===========================================================
I think this is one of the strongest arguments for the entire redesign.

The Android interface should essentially be:

Weather Routing Basic


with:

Start
Destination
Departure Time

Compute


and almost nothing else.

Android Wireframe:

--------------------------------
Weather Routing
--------------------------------

Start

Destination

Departure Time

Weather:
    Climatology

Boat:
    Boat.xml

[ Compute ]


Optional:

[ More Settings ]


which opens the Intermediate profile.

This gives Android users a touchscreen-oriented workflow and naturally reuses the same "First User" architecture instead of maintaining a separate mobile UX.

Final Design Principle

I would formally document:

UX-001

Every user must be able to generate
their first route successfully using:

    Boat.xml
    Climatology

without loading a GRIB file,
editing a polar,
or using advanced settings.

========================================================
If a proposed setting, dialog, warning, or workflow interferes with that requirement, it belongs in Intermediate or Advanced mode, not in Basic mode.
================================================

==========================================

Create a control-mapping spreadsheet, Define settings migration and defaults.

============================================


For this work, I would create two implementation artifacts:

A control-mapping spreadsheet owned by development.
A settings migration specification owned by architecture.

The spreadsheet becomes the master inventory used during the UI redesign.

Control Mapping Spreadsheet

Suggested file:

docs/ux/WeatherRouting-Control-Mapping.xlsx


Columns:

ID	Current Dialog	Current Control	Configuration Variable	Basic	Intermediate	Advanced	Help Topic	Default Value	Migration Action

Example:

ID	Current Dialog	Current Control	Variable	Basic	Intermediate	Advanced	Help Topic	DefaultWR-001	Configuration	Start Position	Start	✓	✓	✓	HT-START	User
WR-002	Configuration	Destination	End	✓	✓	✓	HT-END	User
WR-003	Configuration	Boat File	BoatFileName	Hidden	✓	✓	HT-POLAR	Boat.xml
WR-004	Configuration	Climatology	UseClimatology	✓	✓	✓	HT-CLIM	Enabled
WR-005	Configuration	GRIB	UseGrib	Hidden	✓	✓	HT-GRIB	Disabled
WR-006	Configuration	Wave Limit	MaxWaveHeight	Hidden	✓	✓	HT-WAVE	Disabled
WR-007	Configuration	Time Step	DeltaTime	Hidden	Hidden	✓	HT-TSTEP	Existing
WR-008	Configuration	Isochrone Density	Density	Hidden	Hidden	✓	HT-ISO	Existing
Settings Migration Architecture
Version 1

Current configuration:

RouteMapConfiguration


contains many independent fields.

Version 2

Add:

enum class UserExperienceLevel
{
    FirstUser,
    Intermediate,
    Advanced
};


Stored globally:

/PlugIns/WeatherRouting/UserLevel

New Defaults
First User
UserLevel = FirstUser

BoatFile = Boat.xml

UseClimatology = true

UseGRIB = false

UseXGrib = false

RequireForecast = false

MaxWind = Disabled

MaxWave = Disabled

MaxCurrent = Disabled

DepartureTime = Now

Intermediate
UserLevel = Intermediate

Boat selection enabled

GRIB enabled

Constraint editing enabled

Route statistics enabled

Advanced
UserLevel = Advanced

All controls visible

Migration Logic

During startup:

if (!ConfigExists("UserLevel"))
{
    UserLevel = FirstUser;
}


Then:

MigrateWeatherRoutingSettings();


Pseudo-code:

void WeatherRouting::MigrateSettings()
{
    if (m_ConfigVersion < 2)
    {
        UserLevel = FirstUser;

        m_ConfigVersion = 2;

        SaveXML();
    }
}

First User Reset Specification

Menu:

Help
 └── Reset To First User Defaults


Toolbar:

🏠 Reset


Handler:

void WeatherRouting::ResetToFirstUserDefaults()
{
    ApplyFirstUserDefaults();

    RebuildDialogs();

    SaveXML();
}

Basic Android Profile

This may actually become the biggest payoff.

Android should map almost directly to:

UserExperienceLevel::FirstUser


Android Layout:

----------------------------------
Weather Routing
----------------------------------

Start

Destination

Departure

----------------------------------

Weather
Climatology

Boat
Boat.xml

----------------------------------

[ Compute ]


No:

GRIB controls
Polar editor
Wave constraints
Isochrone settings
Advanced weather


on the primary screen.

Those move behind:

More Settings...


or:

Switch To Intermediate

Help Topic IDs

These become stable IDs used by tooltips and documentation.

Topic ID	TopicHT-WR	Weather Routing
HT-START	Start Position
HT-END	Destination
HT-DEPARTURE	Departure Time
HT-BOAT	Boat Selection
HT-POLAR	Boat Polar
HT-CLIM	Climatology
HT-GRIB	GRIB Forecast
HT-XGRIB	xGrib
HT-WIND	Wind Limits
HT-WAVE	Wave Limits
HT-CURRENT	Current Data
HT-VMG	Velocity Made Good
HT-ISO	Isochrones
HT-TSTEP	Time Step
HT-DIAG	Diagnostics
Recommended Ticket Breakdown
Epic A: First User Experience
WR-UX-001 User Experience Levels
WR-UX-002 First User Profile
WR-UX-003 First User Reset
WR-UX-004 First User Validation Path
WR-UX-005 Android Basic Profile

Epic B: Dialog Simplification
WR-UI-001 Control Inventory
WR-UI-002 Control Mapping
WR-UI-003 Progressive Disclosure
WR-UI-004 Dynamic Visibility Framework

Epic C: Learning System
WR-HELP-001 Help Infrastructure
WR-HELP-002 Tooltip Framework
WR-HELP-003 Learning Dialogs
WR-HELP-004 First User Wizard

Epic D: Configuration Management
WR-CONFIG-001 Settings Migration
WR-CONFIG-002 Versioned Config
WR-CONFIG-003 First User Defaults
WR-CONFIG-004 Settings Reset Support

==========================
This gives you a concrete roadmap from the current WeatherRouting UI to a progressive Basic → Intermediate → Advanced experience while also creating a natural Android UI based on the Basic profile.
=============================



++++++++++++++++++++++++++++++++++

So in summary what have we tried to accomplish here?

+++++++++++++++++++++++++++++++++++

In summary, we have been trying to accomplish three related goals:

1. Make WeatherRouting successful for first-time users

Today the software largely assumes users already understand:

GRIB files
boat polars
routing constraints
weather data sources
climatology
route configuration

The new design reverses that assumption.

The primary requirement became:

A user should be able to install WeatherRouting, select a start and destination, press Compute, and successfully generate a route.

To achieve that we defined:

First User Profile

Using:

Boat.xml
Climatology
Safe defaults


without requiring:

GRIBs
Custom Polars
Advanced Settings


And we introduced:

ResetToFirstUserDefaults()


as the universal recovery mechanism.

2. Separate Learning, Normal Use, and Expert Use

We recognized that a beginner and an expert have very different needs.

Therefore we created:

enum class UserExperienceLevel
{
    FirstUser,
    Intermediate,
    Advanced
};


with progressive disclosure.

First User

Goal:

Make a route.


Visible:

Start
Destination
Departure
Boat.xml
Climatology
Compute

Intermediate

Goal:

Customize routing.


Adds:

Boat Polars
GRIB Settings
Wind Limits
Wave Limits
Current Limits

Advanced

Goal:

Tune the routing engine.


Adds:

Isochrone Parameters
Interpolation Controls
Optimization Controls
Diagnostics
Debug Features

3. Build a Self-Teaching Interface

The current dialogs contain years of accumulated terminology.

We proposed replacing "know it already" with "learn it in place".

Every significant setting gets:

[ ? ]


support.

Hover:

1 sentence summary


Click:

Detailed explanation


Examples:

What is Climatology?
What is a GRIB?
What is a Polar?
What is VMG?


This turns the application itself into the user's first tutorial.

4. Modernize Validation and Error Handling

Instead of:

Compute
    ↓
Fail


we moved toward:

Compute
    ↓
Preflight Validation
    ↓
Fix Problems
    ↓
Compute


Including:

weather validation
boat validation
climatology-only routing support
first-user setup support

And we established the distinction:

RouteError


versus

RouteWarning


so that:

No weather source


becomes a true error

while:

Missing wave data


becomes a warning.

5. Prepare for Android

An important discovery was:

The First User profile is almost exactly what Android needs.

Android should likely become:

Start
Destination
Departure

Weather:
    Climatology

Boat:
    Boat.xml

Compute


with everything else hidden behind:

More Settings...


This allows the same architecture to support:

Desktop First User
Desktop Intermediate
Desktop Advanced
Android


without maintaining separate workflows.

6. Create an Implementation Roadmap

We ended with a concrete development roadmap:

UX
WR-UX-001 User Levels
WR-UX-002 First User Profile
WR-UX-003 Reset to First User
WR-UX-004 Android Basic Experience

UI
WR-UI-001 Control Inventory
WR-UI-002 Control Mapping
WR-UI-003 Progressive Disclosure

Help System
WR-HELP-001 Context Help
WR-HELP-002 Learning Dialogs
WR-HELP-003 First User Wizard

Configuration
WR-CONFIG-001 Settings Migration
WR-CONFIG-002 Versioned Configuration
WR-CONFIG-003 Default Profiles

The Core Accomplishment

If I had to reduce everything to a single sentence:

We transformed the WeatherRouting design goal from "Expose all routing capabilities" to "Guide users from their first successful route through progressively deeper levels of routing expertise."

That is a significant shift in philosophy, and in my opinion it could have a bigger impact on adoption than almost any routing-engine enhancement.

===============================================

Turn this summary into a project brief,  Define measurable success metrics, Prioritize implementation roadmap.  

===============================================

WeatherRouting UX Modernization Project Brief
Project Title

WeatherRouting Progressive User Experience and First-Route Success Initiative

Executive Summary

WeatherRouting has evolved into a powerful routing platform, but much of its user interface reflects the needs of experienced users rather than first-time sailors. New users are currently exposed to concepts such as GRIB files, boat polars, routing constraints, weather source selection, and optimization controls before successfully generating their first route.

This project shifts the primary design goal from:

"Expose routing functionality"

to:

"Guide users from their first successful route through progressively deeper levels of routing expertise."

The project introduces:

First User, Intermediate, and Advanced experience levels
Guaranteed first-route success using Climatology and Boat.xml
Progressive disclosure of settings
Context-sensitive learning and help
Preflight validation and recovery
Android-friendly simplified workflows
Vision Statement

A first-time WeatherRouting user should be able to:

Install WeatherRouting
Select Start
Select Destination
Press Compute
Receive Route


without:

Loading a GRIB
Selecting a polar
Understanding routing internals
Changing advanced settings

Primary Objectives
Objective 1

Increase first-route success rate.

Current

A new user may encounter:

Missing Weather
Missing Polar
Missing GRIB
Missing Current Data
Configuration Errors


before generating any route.

Future

A new user receives a route using:

Boat.xml
Climatology
Default settings


with no additional configuration.

Objective 2

Reduce UI complexity.

Current dialogs expose:

Beginner settings
Intermediate settings
Developer settings


simultaneously.

Future UI exposes only those controls appropriate to the current user experience level.

Objective 3

Transform WeatherRouting into a self-teaching application.

Users learn through:

Tooltips
Context help
Learning dialogs
Guided setup


rather than manuals alone.

Measurable Success Metrics
Metric 1: First Route Success
Target

95%+

of fresh installs should successfully generate a route using:

Start Position
Destination
Compute


assuming:

Climatology available
Boat.xml available

Metric 2: Basic Mode Complexity
Current

Estimated:

50+
visible controls

Target

Basic Mode:

10 or fewer visible controls

Metric 3: Support Reduction

Measure support requests involving:

Missing GRIB
Missing Polar
Route Won't Compute
Configuration Confusion

Target

Reduce by:

50%


within one release cycle.

Metric 4: Android Readiness
Target

100%

of Basic Mode functionality usable from:

Phone
Tablet
Touchscreen


without opening Advanced dialogs.

Metric 5: Context Help Coverage
Target

Every user-visible setting shall have:

Tooltip
Help Topic


Coverage goal:

100%

User Experience Levels
Level 1: First User
Objective

Generate a route successfully.

Visible controls:

Start
Destination
Departure
Boat.xml
Climatology
Compute


Hidden:

Polars
GRIB configuration
Wave limits
Current limits
Isochrones
Optimization settings
Diagnostics

Level 2: Intermediate
Objective

Customize routing behavior.

Additional controls:

Boat Polars
GRIB
Weather Sources
Wind Limits
Wave Limits
Current Limits
Statistics

Level 3: Advanced
Objective

Tune routing engine behavior.

Additional controls:

Isochrone Controls
Interpolation
Engine Diagnostics
Optimization Parameters
Debug Features
Logging

First User Profile Specification
Weather
Climatology = Enabled

GRIB = Optional

Forecast Required = No

Boat
Boat.xml = Mandatory Default

Constraints
Wind Limits = Disabled

Wave Limits = Disabled

Current Limits = Disabled

Departure
Now

Expected Result
Compute always succeeds


when:

Start defined
Destination defined
Boat.xml exists
Climatology available

First User Reset

A highly visible recovery mechanism.

Menu:

Help
 └── Reset to First User Defaults


Toolbar:

🏠 Reset


Purpose:

Restore computable configuration


at any time.

Progressive Help System

Every significant setting receives:

[ ? ]


support.

Hover

One sentence explanation.

Click

Detailed learning content.

Examples:

What is Climatology?
What is a GRIB?
What is a Polar?
What is VMG?

Android Strategy

The Android interface becomes:

Weather Routing Basic


effectively equivalent to:

UserExperienceLevel::FirstUser


Visible:

Start
Destination
Departure

Boat.xml

Climatology

Compute


Advanced functionality remains available through:

More Settings...

Prioritized Roadmap
Phase 1 (Highest Value)
First Route Success

Tickets:

WR-UX-001 User Experience Levels

WR-UX-002 First User Profile

WR-UX-003 First User Reset

WR-UX-004 Climatology-Only Validation

WR-UX-005 Preflight Compute Validation


Success Criteria:

User can generate a route after installation.

Phase 2
Simplified UI

Tickets:

WR-UI-001 Control Inventory

WR-UI-002 Control Mapping

WR-UI-003 Progressive Disclosure Framework

WR-UI-004 Basic Mode Dialog


Success Criteria:

Basic mode has fewer than 10 controls.

Phase 3
Learning System

Tickets:

WR-HELP-001 Tooltip Framework

WR-HELP-002 Context Help Topics

WR-HELP-003 Learning Dialogs

WR-HELP-004 First User Wizard


Success Criteria:

All visible controls have help.

Phase 4
Intermediate User Experience

Tickets:

WR-INT-001 Intermediate Settings

WR-INT-002 Weather Source Controls

WR-INT-003 Constraint Controls

Phase 5
Advanced User Experience

Tickets:

WR-ADV-001 Advanced Settings Dialog

WR-ADV-002 Optimization Controls

WR-ADV-003 Diagnostics

WR-ADV-004 Logging

Phase 6
Android UI

Tickets:

WR-AND-001 Android Basic Mode

WR-AND-002 Touch-Friendly Compute Flow

WR-AND-003 Mobile Help System

=========================================================================
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Success Criteria:

Android uses the First User experience by default.

Definition of Success

The project is successful when:

A sailor with no prior WeatherRouting experience can install the software, define a route, press Compute, obtain a route, understand why it was generated, and progressively learn more advanced capabilities only when they are ready to use them.


====================================================================================

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++



Vision Statement

WeatherRouting should enable every sailor to create a successful route first, and learn advanced routing later.

More formally:

WeatherRouting will provide a progressive user experience that guides sailors from their first successful route through increasingly sophisticated weather-routing capabilities, without requiring prior knowledge of GRIBs, polars, climatology, routing theory, or advanced configuration.

The key idea is:

Success First
Learning Second
Optimization Third


instead of:

Configuration First
Success Later

Executive One-Pager
Problem

WeatherRouting has accumulated significant capability over many years.

While powerful, the current interface presents many options simultaneously:

Weather source selection
Boat polars
Constraints
Routing settings
Advanced optimization controls
Diagnostics

This creates a steep learning curve for new users and increases support burden.

Many first-time users encounter configuration problems before successfully producing their first route.

Opportunity

Most sailors want to accomplish one thing initially:

Route from A to B.


They do not initially need:

Isochrone tuning
Interpolation controls
Engine diagnostics
Advanced constraints


By progressively exposing capability, WeatherRouting can:

Improve onboarding
Reduce support requests
Improve Android usability
Improve training and documentation
Increase successful adoption
Solution

Introduce three experience levels:

Basic / First User

Goal:

Create a route successfully.


Uses:

Boat.xml
Climatology
Safe defaults

Intermediate

Goal:

Use routing effectively.


Adds:

Boat polars
GRIB
Weather controls
Constraints

Advanced

Goal:

Optimize routing.


Exposes:

All engine controls
Diagnostics
Optimization parameters

Success Criteria

A fresh installation should allow:

Select Route
Press Compute
Get Route


without:

Loading GRIB data
Selecting a polar
Configuring constraints
Reading documentation

The Big UI Change

Today:

Configuration Dialog
 ├─ Everything
 ├─ Everything
 ├─ Everything
 └─ Everything


Proposed:

Basic
   ↓
Intermediate
   ↓
Advanced


Each level builds on the previous one.

How To Move First User Into A Very Simple Dialog

I actually think this should become a separate dialog rather than merely hiding controls.

Current mental model:

One giant configuration dialog


New mental model:

Simple Route Setup
     ↓
Advanced Configuration

First User Route Dialog
+--------------------------------------+
| Weather Routing                      |
+--------------------------------------+

Route
----------------------------------------

Start:
[____________________]

Destination:
[____________________]

Departure:
[ Now ▼ ]

----------------------------------------

Boat:
Boat.xml [?]

Weather:
Climatology [?]

----------------------------------------

[ Compute ]

[ Reset ]

[ Learn ]


That's it.

No:

Polars
Wave limits
Weather priorities
Time stepping
Isochrones

Learn Button

The Learn button becomes the user's first tutorial.

What is Weather Routing?
What is Climatology?
What is Boat.xml?


Three screens.

Maximum.

Compute Button

Press:

Compute


Preflight checks:

Start defined?
Destination defined?
Boat.xml present?
Climatology available?


If yes:

Compute Route


No weather dialogs.

No GRIB dialogs.

No advanced prompts.

Intermediate Transition

After users successfully create routes, offer:

Enable Intermediate Features


which reveals:

Boat Polars
GRIB
Wind Limits
Wave Limits
Current Limits


in the same dialog.

Advanced Transition

Then:

Enable Advanced Features


reveals:

Optimization
Diagnostics
Isochrones
Interpolation

Android Impact

This is where the design becomes especially powerful.

The Basic dialog is already nearly ideal for Android.

--------------------------------
Weather Routing
--------------------------------

Start

Destination

Departure

Boat.xml

Climatology

[ Compute ]


Everything fits naturally on:

phones
tablets
touch screens

while sharing the same architecture as desktop.

One Guiding Principle

If I had to put a single sentence at the top of the project charter, it would be:

Every WeatherRouting user should be able to generate a successful climatology-based route within minutes of installation, using Boat.xml and default settings, before they are asked to understand any advanced routing concepts.

That one requirement can drive almost every UX decision in the redesign.

