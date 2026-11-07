// =======================================
// Tabs
// =======================================

function showTab(id, btn)
{
    document
        .querySelectorAll(".tab-content")
        .forEach(t => t.classList.remove("active"));

    document
        .querySelectorAll(".tab")
        .forEach(t => t.classList.remove("active"));

    document
        .getElementById(id)
        .classList.add("active");

    btn.classList.add("active");

    if(id === "learn")
    {
        resetLearnTab();
    }

    if(id === "cruise")
    {
        resetCruiseTab();
    }
}

function resetLearnTab()
{
    // Reset inputs

    document.getElementById(
        "departure").value = "";

    document.getElementById(
        "destination").value = "";

    document.getElementById(
        "boat").value = "";

    document.getElementById(
        "weather").value = "";

    // Clear route line

    document.getElementById(
        "routePath")
        .setAttribute(
            "d", ""
        );

    // Hide results

    document.getElementById(
        "resultCard")
        .style.display =
        "none";

    // Reset status system

    updateStatus();
	
	document.getElementById(
    "cruiseStatusHelp").innerHTML =
    "Start a new voyage by choosing a departure harbor, destination and departure time. Vessel and routing preferences have been preserved.";
	
}

function resetCruiseTab()
{
    // Reset voyage-specific fields

    document.getElementById(
        "cruiseDeparture").value = "";

    document.getElementById(
        "cruiseDestination").value = "";

    document.getElementById(
        "departureTime").value = "";

    // Leave user preferences alone:
    //
    // cruiseBoat
    // cruiseWeather
    // routingObjective
    // safetyMargin

    // Clear route display

    document.getElementById(
        "cruiseRoutePath")
        .setAttribute(
            "d", ""
        );

    // Hide previous results

    document.getElementById(
        "cruiseResultCard")
        .style.display =
        "none";

    // Re-run preflight

    updateCruiseStatus();
	
	document.getElementById(
    "cruiseStatusHelp")
    .innerHTML =
    "Start a new voyage by selecting a departure harbor, destination and departure time. Vessel and routing preferences have been preserved.";

	resetCruiseMarkers();
}

function restoreCruiseDefaults()
{
    localStorage.removeItem(
        "cruiseBoat");

    localStorage.removeItem(
        "cruiseWeather");

    localStorage.removeItem(
        "routingObjective");

    localStorage.removeItem(
        "safetyMargin");

    document.getElementById(
        "cruiseDeparture").value = "";

    document.getElementById(
        "cruiseDestination").value = "";

    document.getElementById(
        "departureTime").value = "";

    document.getElementById(
        "cruiseBoat").value = "";

    document.getElementById(
        "cruiseWeather").value = "";

    document.getElementById(
        "routingObjective").value = "";

    document.getElementById(
        "safetyMargin").value = "";

    document.getElementById(
        "cruiseRoutePath")
        .setAttribute(
            "d",
            ""
        );

    document.getElementById(
        "cruiseResultCard")
        .style.display =
        "none";

    updateCruiseStatus();

    document.getElementById(
        "cruiseStatusHelp")
        .innerHTML =
        "All voyage and preference settings have been restored to defaults.";
		
	resetCruiseMarkers();	
}


// =======================================
// Waypoints
// =======================================

const waypoints = [

    {
        name: "Mystic Harbor",
        x: 120,
        y: 300
    },

    {
        name: "Fishers Island",
        x: 240,
        y: 260
    },

    {
        name: "Shelter Harbor",
        x: 300,
        y: 240
    },

    {
        name: "Stonington",
        x: 180,
        y: 280
    },

    {
        name: "New London",
        x: 90,
        y: 260
    },

    {
        name: "Newport",
        x: 520,
        y: 170
    },

    {
        name: "Block Island",
        x: 650,
        y: 180
    }

];

// =======================================
// Status Panel
// =======================================

function updateStatus()
{
    const departure =
        document.getElementById(
            "departure").value;

    const destination =
        document.getElementById(
            "destination").value;

    const boat =
        document.getElementById(
            "boat").value;

    const weather =
        document.getElementById(
            "weather").value;

    const dot =
        document.getElementById(
            "routeStatusDot");

    const text =
        document.getElementById(
            "routeStatusText");

    const help =
        document.getElementById(
            "routeStatusHelp");

    const button =
        document.getElementById(
            "computeButton");

    let issues = [];

    if(!departure)
    {
        issues.push(
            "Select a departure harbor."
        );
    }

    if(!destination)
    {
        issues.push(
            "Select a destination harbor."
        );
    }

    if(departure &&
       destination &&
       departure === destination)
    {
        issues.push(
            "Departure and destination must be different."
        );
    }

    if(!boat)
    {
        issues.push(
            "Select a boat."
        );
    }

    if(!weather)
    {
        issues.push(
            "Select a weather source."
        );
    }

    if(issues.length > 0)
    {
        dot.className =
            "status-dot red";

        text.innerHTML =
            "Route Setup Incomplete";

        help.innerHTML =
            issues.join("<br>");

        button.disabled = true;
    }
    else
    {
        dot.className =
            "status-dot green";

        text.innerHTML =
            "Ready To Compute";

        help.innerHTML =
            "All route information has been entered. Compute the route and compare how boat selection and weather source affect the results.";

        button.disabled = false;
    }
}
// =======================================
// Route Math
// =======================================

function calculateDistance(start, end)
{
    const dx =
        end.x - start.x;

    const dy =
        end.y - start.y;

    const pixels =
        Math.sqrt(
            dx * dx +
            dy * dy
        );

    return Math.round(
        pixels * 0.07
    );
}

function calculateCourse(start, end)
{
    const dx =
        end.x - start.x;

    const dy =
        start.y - end.y;

    let angle =
        Math.atan2(dx, dy) *
        (180 / Math.PI);

    if(angle < 0)
    {
        angle += 360;
    }

    return Math.round(angle);
}

function calculateWind(weather)
{
    if(weather === "GRIB")
    {
        return 18;
    }

    return 14;
}

function calculateEta(
    distance,
    boat)
{
    const speed =
        getBoatSpeed(boat);

    const hours =
        distance / speed;

    const wholeHours =
        Math.floor(hours);

    const minutes =
        Math.round(
            (hours - wholeHours) * 60
        );

    return `${wholeHours}h ${minutes}m`;
}



function getBoatSpeed(boat)
{
    if(boat === "Sunfish")
        return 2.5;

    if(boat === "Ideal 18")
        return 4.5;

    if(boat === "Bristol 41.1")
        return 6.0;

    return 4.5;
}

// =======================================
// Draw Route
// =======================================

function drawRoute()
{
    const startName =
        document.getElementById(
            "departure").value;

    const endName =
        document.getElementById(
            "destination").value;

    const start =
        waypoints.find(
            w => w.name === startName);

    const end =
        waypoints.find(
            w => w.name === endName);

    if(!start || !end)
        return;

    document
        .getElementById(
            "routePath")
        .setAttribute(
            "d",
            `M${start.x} ${start.y}
             C${(start.x + end.x)/2}
             ${start.y - 80}
             ${(start.x + end.x)/2}
             ${end.y - 40}
             ${end.x}
             ${end.y}`
        );
}

// =======================================
// Compute Route
// =======================================

function computeRoute()
{
    const departure =
        document.getElementById(
            "departure").value;

    const destination =
        document.getElementById(
            "destination").value;

    if(departure === destination)
    {
        alert(
            "Departure and destination must be different."
        );
        return;
    }

    const boat =
        document.getElementById(
            "boat").value;

    const weather =
        document.getElementById(
            "weather").value;

    drawRoute();

    const start =
        waypoints.find(
            w => w.name === departure);

    const end =
        waypoints.find(
            w => w.name === destination);

    const distance =
        calculateDistance(
            start,
            end
        );

    const course =
        calculateCourse(
            start,
            end
        );

    const wind =
        calculateWind(
            weather
        );

    const eta =
        calculateEta(
            distance,
            boat
        );

    document.getElementById(
        "routeTitle").innerHTML =
        departure + " → " + destination;

    document.getElementById(
        "departureResult").innerHTML =
        departure;

    document.getElementById(
        "destinationResult").innerHTML =
        destination;

    document.getElementById(
        "boatResult").innerHTML =
        boat;

    document.getElementById(
        "weatherResult").innerHTML =
        weather;

    document.getElementById(
        "distanceValue").innerHTML =
        distance + " nm";

    document.getElementById(
        "etaValue").innerHTML =
        eta;

    document.getElementById(
        "windValue").innerHTML =
        wind + " kt";

    document.getElementById(
        "courseValue").innerHTML =
        course + "°";

    document.getElementById(
        "routeExplanation").innerHTML =
        "Route optimized using " +
        weather +
        " conditions and " +
        boat +
        " performance assumptions.";

    document.getElementById(
        "resultCard").style.display =
        "block";
}

// =======================================
// Startup
// =======================================

window.onload = function()
{
    loadCruisePreferences();

    updateStatus();
    updateCruiseStatus();
};

// ========================================
// =======================================
// Learn
// =======================================



function clearLearnResults()
{
    // Remove route from chart

    document.getElementById(
        "routePath")
        .setAttribute(
            "d",
            ""
        );

    // Hide summary

    document.getElementById(
        "resultCard")
        .style.display =
        "none";
}


// ========================================
// =======================================
// Cruise
// =======================================

function clearCruiseResults()
{
    // Remove route from chart

    document.getElementById(
        "cruiseRoutePath")
        .setAttribute(
            "d",
            ""
        );

    // Reset highlighted harbors

    resetCruiseMarkers();

    // Hide summary

    document.getElementById(
        "cruiseResultCard")
        .style.display =
        "none";
}

function computeCruiseRoute()
{
	const departureTime =
		new Date(
			document.getElementById(
				"departureTime").value
		);

		document.getElementById(
			"cruiseDepartureTimeResult")
			.innerHTML =
			departureTime.toLocaleString();

	const objective =
		document.getElementById(
			"routingObjective"
		).value;

	const margin =
		document.getElementById(
			"safetyMargin"
		).value;
    const departure =
        document.getElementById(
            "cruiseDeparture"
        ).value;

    const destination =
        document.getElementById(
            "cruiseDestination"
        ).value;

    const boat =
        document.getElementById(
            "cruiseBoat"
        ).value;

    const weather =
        document.getElementById(
            "cruiseWeather"
        ).value;

    if(!departure ||
       !destination ||
       !boat ||
       !weather)
    {
        return;
    }

    drawCruiseRoute();
	
	highlightCruiseMarkers(
		departure,
		destination
	);

    const start =
        waypoints.find(
            w => w.name === departure
        );

    const end =
        waypoints.find(
            w => w.name === destination
        );

    const distance =
        calculateDistance(
            start,
            end
        );

    const wind =
        calculateWind(
            weather
        );

    const eta =
        calculateEta(
            distance,
            boat
        );
		
	const speed =
		getBoatSpeed(
			boat
		);



	const course =
		calculateCourse(
			start,
			end
		);

	document.getElementById(
		"cruiseBoatSpeed"
	).innerHTML =
		speed + " kt";			

	document.getElementById(
		"cruiseCourse"
	).innerHTML =
		course + "°";		

	document.getElementById(
		"cruiseDepartureResult")
		.innerHTML =
		departure;

	document.getElementById(
		"cruiseDestinationResult")
		.innerHTML =
		destination;

	document.getElementById(
		"cruiseBoatResult")
		.innerHTML =
		boat;

	document.getElementById(
		"cruiseWeatherResult")
		.innerHTML =
		weather;

	document.getElementById(
		"cruiseObjectiveResult")
		.innerHTML =
		objective;

	document.getElementById(
		"cruiseMarginResult")
		.innerHTML =
		margin;

    document.getElementById(
        "cruiseDistance"
    ).innerHTML =
        distance + " nm";

    document.getElementById(
        "cruiseEta"
    ).innerHTML =
        eta;

    document.getElementById(
        "cruiseAvgWind"
    ).innerHTML =
        wind + " kt";

    document.getElementById(
        "cruiseMaxWind"
    ).innerHTML =
        (wind + 8) + " kt";

    document.getElementById(
        "cruiseResultCard"
    ).style.display =
        "block";
		
	let note =
		"Voyage planned from " +
		departure +
		" to " +
		destination +
		" using " +
		weather +
		" weather guidance.";

	if(objective ===
	   "Avoid Heavy Weather")
	{
		note +=
			" The route prioritizes avoiding stronger weather conditions over the shortest arrival time.";
	}

	if(objective ===
	   "Comfortable Passage")
	{
		note +=
			" The route prioritizes comfort and reduced exposure to adverse conditions.";
	}

	if(objective ===
	   "Fastest Arrival")
	{
		note +=
			" The route prioritizes earliest arrival time.";
	}

	if(objective ===
	   "Day Sailing")
	{
		note +=
			" The route is optimized for a typical day-sailing passage.";
	}
	
	if(margin ===
	   "Normal")
	{
		note +=
			" Standard routing safety margins have been applied.";
	}

	if(margin ===
	   "Conservative")
	{
		note +=
			" Additional routing safety margins have been applied.";
	}

	if(margin ===
	   "Very Conservative")
	{
		note +=
			" Maximum routing safety margins have been applied throughout the route.";
	}

	document.getElementById(
		"cruiseExplanation"
	).innerHTML =
		note;

}


function updateCruiseStatus()
{
	
    const departure =
        document.getElementById(
            "cruiseDeparture").value;

    const destination =
        document.getElementById(
            "cruiseDestination").value;

    const boat =
        document.getElementById(
            "cruiseBoat").value;

    const weather =
        document.getElementById(
            "cruiseWeather").value;

    const departureTime =
        document.getElementById(
            "departureTime").value;
				
    const objective =
        document.getElementById(
            "routingObjective").value;

    const margin =
        document.getElementById(
            "safetyMargin").value;

    const dot =
        document.getElementById(
            "cruiseStatusDot");

    const text =
        document.getElementById(
            "cruiseStatusText");

    const help =
        document.getElementById(
            "cruiseStatusHelp");

    const button =
        document.getElementById(
            "cruiseComputeButton");

    let issues = [];

    if(!departure)
        issues.push(
            "Select a departure harbor for this voyage."
        );

    if(!destination)
        issues.push(
            "Select a destination harbor."
        );

    if(departure &&
       destination &&
       departure === destination)
    {
        issues.push(
            "Departure and destination must be different."
        );
    }

    if(!boat)
        issues.push(
            "Select a vessel."
        );

    if(!departureTime)
        issues.push(
            "Select when you plan to depart."
        );

    if(!weather)
        issues.push(
            "Select a weather source."
        );

    if(!objective)
        issues.push(
            "Select a routing objective."
        );

    if(!margin)
        issues.push(
            "Select a safety margin."
        );

    if(issues.length > 0)
    {
        dot.className =
            "status-dot red";

        text.innerHTML =
            "Cruise Plan Incomplete";

        help.innerHTML =
            issues.join("<br>");

        button.disabled =
            true;
    }
    else
    {
        dot.className =
            "status-dot green";

        text.innerHTML =
            "Ready To Compute";

	help.innerHTML =
		"All voyage information has been entered. Compute a route to evaluate weather, timing, comfort and safety. Reset clears the current voyage while preserving your routing preferences.";
	
        button.disabled =
            false;
    }
}

function saveCruisePreferences()
{
    localStorage.setItem(
        "cruiseBoat",
        document.getElementById(
            "cruiseBoat").value
    );

    localStorage.setItem(
        "cruiseWeather",
        document.getElementById(
            "cruiseWeather").value
    );

    localStorage.setItem(
        "routingObjective",
        document.getElementById(
            "routingObjective").value
    );

    localStorage.setItem(
        "safetyMargin",
        document.getElementById(
            "safetyMargin").value
    );
}


function loadCruisePreferences()
{
    const boat =
        localStorage.getItem(
            "cruiseBoat");

    const weather =
        localStorage.getItem(
            "cruiseWeather");

    const objective =
        localStorage.getItem(
            "routingObjective");

    const margin =
        localStorage.getItem(
            "safetyMargin");

    if(boat)
    {
        document.getElementById(
            "cruiseBoat").value =
            boat;
    }

    if(weather)
    {
        document.getElementById(
            "cruiseWeather").value =
            weather;
    }

    if(objective)
    {
        document.getElementById(
            "routingObjective").value =
            objective;
    }

    if(margin)
    {
        document.getElementById(
            "safetyMargin").value =
            margin;
    }
}


function drawCruiseRoute()
{
    const startName =
        document.getElementById(
            "cruiseDeparture"
        ).value;

    const endName =
        document.getElementById(
            "cruiseDestination"
        ).value;

    const start =
        waypoints.find(
            w => w.name === startName
        );

    const end =
        waypoints.find(
            w => w.name === endName
        );

    if(!start || !end)
        return;

    document
        .getElementById(
            "cruiseRoutePath"
        )
        .setAttribute(
            "d",
            `M${start.x} ${start.y}
             C${(start.x+end.x)/2}
             ${start.y-80}
             ${(start.x+end.x)/2}
             ${end.y-40}
             ${end.x}
             ${end.y}`
        );
}



const cruiseMarkerIds = {

    "Mystic Harbor":
        "cruiseMysticHarbor",

    "Fishers Island":
        "cruiseFishersIsland",

    "Shelter Harbor":
        "cruiseShelterHarbor",

    "Stonington":
        "cruiseStonington",

    "New London":
        "cruiseNewLondon",

    "Newport":
        "cruiseNewport",

    "Block Island":
        "cruiseBlockIsland"
};

function highlightCruiseMarkers(
    departure,
    destination)
{

    resetCruiseMarkers();

    const departureId =
        cruiseMarkerIds[
            departure
        ];

    const destinationId =
        cruiseMarkerIds[
            destination
        ];

    if(departureId)
    {
        document
            .getElementById(
                departureId)
            .setAttribute(
                "fill",
                "#22c55e"
            );

        document
            .getElementById(
                departureId)
            .setAttribute(
                "r",
                "10"
            );
    }

    if(destinationId)
    {
        document
            .getElementById(
                destinationId)
            .setAttribute(
                "fill",
                "#ef4444"
            );

        document
            .getElementById(
                destinationId)
            .setAttribute(
                "r",
                "10"
            );
    }
}


function resetCruiseMarkers()
{
    document.getElementById(
        "cruiseMysticHarbor")
        .setAttribute("fill","#2563eb");

    document.getElementById(
        "cruiseMysticHarbor")
        .setAttribute("r","8");

    document.getElementById(
        "cruiseFishersIsland")
        .setAttribute("fill","#2563eb");

    document.getElementById(
        "cruiseFishersIsland")
        .setAttribute("r","6");

    document.getElementById(
        "cruiseShelterHarbor")
        .setAttribute("fill","#2563eb");

    document.getElementById(
        "cruiseShelterHarbor")
        .setAttribute("r","6");

    document.getElementById(
        "cruiseStonington")
        .setAttribute("fill","#2563eb");

    document.getElementById(
        "cruiseStonington")
        .setAttribute("r","6");

    document.getElementById(
        "cruiseNewLondon")
        .setAttribute("fill","#2563eb");

    document.getElementById(
        "cruiseNewLondon")
        .setAttribute("r","6");

    document.getElementById(
        "cruiseNewport")
        .setAttribute("fill","#2563eb");

    document.getElementById(
        "cruiseNewport")
        .setAttribute("r","6");

    document.getElementById(
        "cruiseBlockIsland")
        .setAttribute("fill","#2563eb");

    document.getElementById(
        "cruiseBlockIsland")
        .setAttribute("r","8");
}
