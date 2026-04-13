/**
 * @file config.h
 * @brief Airport configuration structures loaded from config.json at startup.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <optional>
#include <string>
#include <map>
#include <vector>

#include "DifliModel.h"

/// @brief Geographic ground frequency zone: a named polygon with an associated frequency string.
struct geoGndFreq
{
    std::string         name;     ///< Zone identifier (e.g. "North")
    std::string         freq;     ///< VHF frequency string (e.g. "121.600")
    std::vector<double> lat = {}; ///< Polygon vertex latitudes
    std::vector<double> lon = {}; ///< Polygon vertex longitudes
};

/// @brief Taxi-out stand polygon used to distinguish push-back from direct taxi departures.
struct taxiOutStands
{
    std::string         name;     ///< Stand or apron zone name
    std::vector<double> lat = {}; ///< Polygon vertex latitudes
    std::vector<double> lon = {}; ///< Polygon vertex longitudes
};

/// @brief Configuration for the NAP (Noise Abatement Procedure) reminder alarm.
struct napReminder
{
    bool        enabled;   ///< Whether the reminder is active
    int         hour;      ///< Local hour at which to trigger the reminder
    int         minute;    ///< Local minute at which to trigger the reminder
    std::string tzone;     ///< IANA timezone name used to evaluate the trigger time
    bool        triggered; ///< True once the reminder has already fired this session
};

/// @brief A single named holding point with its position polygon and metadata.
struct holdingPoint
{
    std::string         name;               ///< Holding point name (e.g. "N1")
    bool                assignable = false; ///< Whether the point appears in the HP popup list
    std::string         sameAs;             ///< Name of another holding point considered physically equivalent
    std::vector<double> lat       = {};     ///< Polygon vertex latitudes
    std::vector<double> lon       = {};     ///< Polygon vertex longitudes
    double              centerLat = 0.0;    ///< Centroid latitude (mean of polygon vertices); computed at config load.
    double              centerLon = 0.0;    ///< Centroid longitude (mean of polygon vertices); computed at config load.
    int                 order     = 0;      ///< Insertion index from config.json (0 = first); preserves longest-to-shortest takeoff distance order.
};

/// @brief A single blocking relationship on a Ground Radar stand.
struct standBlock
{
    std::string standName;   ///< Stand designator that gets blocked
    double      minWingspan; ///< Minimum aircraft wingspan (m) to trigger this block; 0 = always blocked
};

/// @brief A stand polygon loaded from GRpluginStands.txt.
struct grStand
{
    std::string             icao;    ///< Airport ICAO code
    std::string             name;    ///< Stand designator (e.g. "B67")
    std::vector<double>     lat;     ///< Polygon vertex latitudes
    std::vector<double>     lon;     ///< Polygon vertex longitudes
    std::vector<standBlock> blocks;  ///< Stands blocked when this one is occupied
    std::optional<int>      heading; ///< Nose-in heading in degrees true, if specified in GRpluginStands.txt
};

/// @brief Routing override for an inbound stand — redirects to a holding point or another stand.
struct standRoutingTarget
{
    enum class Type
    {
        hp,   ///< Target is a holding-point label (resolved via HoldingPositionByLabel).
        stand ///< Target is another stand name (resolved via StandApproachPoint).
    };
    Type        type   = Type::hp; ///< Whether the target is a holding point or a stand.
    std::string target = {};       ///< Holding-point label or stand name.
};

/// @brief Suggested runway vacate point with a minimum gap requirement and associated stands.
struct suggestedVacatePoint
{
    double                   minGap = 0.0; ///< Minimum distance gap (NM) from the preceding inbound required for this vacate
    std::vector<std::string> stands = {};  ///< Stand names associated with this vacate point
};

/// @brief Allowed runway vacation exit with optional WTC restrictions and per-WTC ref exclusions.
struct vacateExit
{
    std::vector<char>                        excludeWtc = {}; ///< WTC categories that cannot use this exit (e.g. {'H','J'})
    std::map<char, std::vector<std::string>> excludeRef = {}; ///< Per-WTC refs excluded when vacating via this exit (e.g. 'H' → {"D"})
};

/// @brief A named approach fix for early TTT detection of non-straight-in RNP approaches.
struct approachFix
{
    std::string name;                           ///< Fix identifier for debugging (e.g. "WW008")
    double      lat               = 0.0;        ///< Fix latitude (decimal degrees)
    double      lon               = 0.0;        ///< Fix longitude (decimal degrees)
    int         altMinFt          = 0;          ///< Computed lower bound (ft MSL): altitude - altOffsetBelow. 0 = no lower gate.
    int         altMaxFt          = 0;          ///< Computed upper bound (ft MSL): altitude + altOffsetAbove. 0 = no upper gate.
    std::string legType           = "straight"; ///< Incoming leg type: "straight", "arcLeft", or "arcRight". IAF has no incoming leg (legLengthNm == 0).
    double      legLengthNm       = 0.0;        ///< Along-track length of the incoming leg in NM; 0 = IAF (no incoming leg).
    double      arcCenterLat      = 0.0;        ///< Derived arc centre latitude (decimal degrees); 0 for straight legs.
    double      arcCenterLon      = 0.0;        ///< Derived arc centre longitude (decimal degrees); 0 for straight legs.
    double      arcRadiusNm       = 0.0;        ///< Derived arc radius in NM; 0 for straight legs.
    double      detectionRadiusNm = 0.0;        ///< Proximity radius (NM) for initial TTT detection; 0 = this fix does not trigger detection.
    int         iafHeading        = 0;          ///< Expected inbound heading (degrees) at this IAF; 0 = no heading check.
};

/// @brief An ordered sequence of approach fixes belonging to a single named non-straight-in approach.
struct approachPath
{
    std::string              name;  ///< Approach identifier (e.g. "RNP N")
    std::vector<approachFix> fixes; ///< Ordered list of fixes along the approach path (IAF first).
};

/// @brief A directional flow rule for a single taxiway.
struct TaxiFlowRule
{
    std::string taxiway;               ///< Taxiway ref (e.g. "P", "M").
    std::string direction;             ///< Preferred direction: "N", "S", "E", or "W".
    double      againstFlowMult = 0.0; ///< Per-rule against-flow multiplier override; 0 = use global default.
};

/// @brief Tunable parameters for taxi graph construction, routing, and safety monitoring.
///
/// All fields default to the values previously hardcoded in taxi_graph.cpp / RadarScreen.cpp.
/// The entire section is optional in config.json; omitting it (or any sub-section) leaves
/// every parameter at its default, so existing airports need no changes.
struct TaxiNetworkConfig
{
    /// @brief Graph construction parameters.
    struct Graph
    {
        double configHoldingPointSnapM = 40.0; ///< Max snap radius (m) when promoting a config HP polygon centroid to a HoldingPoint node.
        double osmHoldingPositionSnapM = 25.0; ///< Max snap radius (m) when promoting an OSM stop-bar node to a HoldingPosition node.
        double subdivisionIntervalM    = 15.0; ///< Interval (m) at which long OSM way segments are subdivided into waypoint nodes.
    } graph;

    /// @brief Base edge-cost multipliers applied per aeroway type at build time.
    struct EdgeCosts
    {
        double multIntersection   = 1.1;  ///< Cost multiplier for taxiway-intersection edges (slight penalty).
        double multRunway         = 20.0; ///< Cost multiplier for runway edges (strongly discouraged; only used to vacate the runway).
        double multRunwayApproach = 18.0; ///< Cost multiplier applied to edges arriving at a HoldingPoint/HoldingPosition node (approaching the runway threshold); slightly below multRunway so vacating via the HP is still preferred over staying on the runway.
        double multTaxilane       = 3.0;  ///< Cost multiplier for stand-access taxilane edges (prefer main taxiways).
        double multWingspanAvoid  = 3.0;  ///< Cost multiplier applied to taxiWingspanAvoid refs when the aircraft wingspan fits the avoid threshold.
    } edgeCosts;

    /// @brief Bearing-difference thresholds for taxiway flow-rule enforcement.
    struct FlowRules
    {
        double againstFlowMinDeg = 135.0; ///< Bearing difference (deg) at or above which an edge is considered against the flow rule.
        double againstFlowMult   = 3.0;   ///< Additional cost multiplier applied to edges that go against an active flow rule.
        double withFlowMaxDeg    = 45.0;  ///< Bearing difference (deg) at or below which an edge is considered to follow the flow rule.
        double withFlowMult      = 0.9;   ///< Cost multiplier for edges following the flow direction (< 1.0 gives a slight preference over uncontrolled taxiways).
    } flowRules;

    /// @brief A* routing algorithm parameters.
    struct Routing
    {
        double backwardSnapM       = 300.0; ///< Radius (m) for searching backward start-node candidates (up to 2).
        double forwardSnapM        = 120.0; ///< Radius (m) for searching forward start-node candidates (up to 3).
        double hardTurnDeg         = 50.0;  ///< Bearing change (deg) above which an edge is hard-blocked during A*. Applies within the same wayRef (prevents kinks, OSM max ~28°) and between two non-intersection wayRefs (forces use of smooth intersection curves; LOWW M↔E junctions are ~47°).
        double wayrefChangePenalty = 200.0; ///< Cost added when leaving a named taxiway for a different wayRef (entering an intersection or switching to another taxiway). Exiting an intersection to a named taxiway is free so that A* correctly evaluates early vs. late taxiway switches.
        double heuristicWeight     = 1.0;   ///< Weight applied to the A* heuristic (W > 1.0 = more goal-directed but may close nodes suboptimally; 1.0 is correct for small graphs).
        int    maxNodeExpansions   = 5000;  ///< Maximum number of nodes A* expands before giving up; higher values find better routes but cost more CPU.
        double softTurnCostPerDeg  = 0.0;   ///< Cost added per degree of bearing change at each edge; 0 disables. Penalises winding routes and favours straight paths.
    } routing;

    /// @brief Cursor snap radii used during interactive taxi planning.
    struct Snapping
    {
        double holdingPointM   = 30.0;  ///< Snap radius (m) to holding-point / holding-position nodes (highest priority).
        double intersectionM   = 15.0;  ///< Snap radius (m) to intersection waypoint nodes (second priority).
        double suggestedRouteM = 20.0;  ///< Snap radius (m) to the suggested route polyline (third priority).
        double waypointM       = 40.0;  ///< Snap radius (m) to any waypoint node (lowest priority).
        double goalSnapM       = 170.0; ///< Snap radius (m) for searching goal-node candidates near the destination stand; must cover the longest taxilane between a stand centroid and the nearest graph node.
    } snapping;

    /// @brief Taxi safety-monitoring thresholds.
    struct Safety
    {
        double conflictDeltaS   = 30.0; ///< Time window (s) within which two aircraft at the same intersection are flagged as conflicting.
        double deviationThreshM = 40.0; ///< Distance (m) an aircraft may deviate from its assigned route before a warning is raised.
        double maxPredictS      = 60.0; ///< Maximum prediction horizon (s) used for conflict detection.
        double minSpeedKt       = 3.0;  ///< Minimum ground speed (kt) required before safety checks are evaluated.
        double sameDirDeg       = 45.0; ///< Bearing difference (deg) below which two conflicting paths are considered same-direction (suppresses alert).
    } safety;
};

/// @brief DIFLIS window layout configuration loaded from the single top-level "diflis" block in config.json.
/// @note All DIFLIS-related airport settings live under this one struct — no scattered keys elsewhere in config.h.
struct DifliAirportConfig
{
    std::vector<int>           columnWidths     = {28, 28, 28, 16};             ///< Per-column width percentages (one entry per column; should sum to ~100)
    COLORREF                   inboundBg        = RGB(176, 216, 255);           ///< Strip background for arrivals (HTML hex in config)
    COLORREF                   inboundBgDark    = RGB(128, 176, 224);           ///< Darker accent for arrivals (callsign/status cells)
    COLORREF                   inboundText      = RGB(0, 0, 0);                 ///< Main text color for arrivals
    COLORREF                   inboundTextDim   = RGB(90, 90, 90);               ///< Dimmed text color for arrivals (sid, runway)
    COLORREF                   outboundBg       = RGB(210, 210, 210);           ///< Strip background for departures
    COLORREF                   outboundBgDark   = RGB(160, 160, 160);           ///< Darker accent for departures
    COLORREF                   outboundText     = RGB(0, 0, 0);                 ///< Main text color for departures
    COLORREF                   outboundTextDim  = RGB(90, 90, 90);               ///< Dimmed text color for departures
    int                        fontSizeStatusBar     = 20;                      ///< Base font size for the bottom status bar (buttons + clock)
    int                        fontSizeGroupHeader   = 16;                      ///< Base font size for group title text (centred header)
    int                        fontSizeGroupSide     = 14;                      ///< Base font size for the right-side sub-header text (e.g. "ETA ^")
    int                        fontSizeStripLarge    = 30;                      ///< Base font size for the large strip text (callsign, runway, status button)
    int                        fontSizeStripMedium   = 20;                      ///< Base font size for the medium strip text (type, stand, squawk, adep/ades)
    int                        fontSizeStripSmall    = 16;                      ///< Base font size for the small strip text (reserved for future sub-cell use)
    std::vector<DifliGroupDef> groups           = {};                            ///< All group definitions in rendering order; column/heightWeight decide placement
};

/// @brief Configuration for a single runway including threshold, holding points, SID groups and vacate points.
struct runway
{
    std::vector<approachPath>                   gpsApproachPaths = {};      ///< Non-straight-in GPS approach paths for early TTT detection; empty = straight-in only.
    std::string                                 designator;                 ///< Runway designator (e.g. "11")
    int                                         headingNumber = -1;         ///< Numeric heading extracted from the designator (e.g. "11L" → 11); -1 if unparseable.
    std::string                                 opposite;                   ///< Designator of the reciprocal runway (used for go-around detection)
    double                                      thresholdLat = 0.0;         ///< Runway threshold latitude
    double                                      thresholdLon = 0.0;         ///< Runway threshold longitude
    std::string                                 twrFreq;                    ///< Tower frequency for this runway
    std::string                                 goAroundFreq;               ///< Go-around frequency for this runway
    int                                         thresholdElevationFt = 0;   ///< Threshold elevation in feet (overrides airport fieldElevation for TTT altitude gates; 0 = use fieldElevation)
    int                                         widthMeters          = 0;   ///< Runway width in metres (from config "width")
    std::string                                 estimateBarSide;            ///< Which side of the approach estimate bar this runway's aircraft appear on ("left" or "right"); empty = omit from bar
    std::map<std::string, holdingPoint>         holdingPoints         = {}; ///< Named holding points on this runway
    std::map<std::string, int>                  sidGroups             = {}; ///< SID key -> group number (built from config "sidGroups": { "1": [...sids] })
    std::map<std::string, std::string>          sidColors             = {}; ///< SID key -> colour name (built from config "sidColors": { "green": [...sids] })
    std::map<std::string, vacateExit>           vacatePoints          = {}; ///< Allowed runway vacation exits keyed by HP wayRef name; unlisted HP refs are excluded during routing
    std::map<std::string, suggestedVacatePoint> suggestedVacatePoints = {}; ///< Named suggested vacate points on this runway
};

/// @brief Full configuration for a single airport loaded from config.json.
struct airport
{
    std::string                                      icao;                           ///< ICAO code (e.g. "LOWW")
    std::string                                      gndFreq;                        ///< Default ground frequency string
    double                                           osmCenterLat            = 0.0;  ///< Centre latitude for the Overpass API bounding circle (decimal degrees)
    double                                           osmCenterLon            = 0.0;  ///< Centre longitude for the Overpass API bounding circle (decimal degrees)
    int                                              osmRadiusM              = 6500; ///< Radius in metres for the Overpass API bounding circle (default 6500)
    int                                              fieldElevation          = 0;    ///< Field elevation in feet (used to detect airborne state)
    int                                              airborneTransfer        = 0;    ///< Altitude (ft) above which the TWR next-freq tag changes colour
    int                                              airborneTransferWarning = 0;    ///< Altitude (ft) above which the TWR next-freq tag blinks orange
    std::map<std::string, geoGndFreq>                geoGndFreq              = {};   ///< Geographic ground frequency zones
    std::vector<std::string>                         ctrStations             = {};   ///< Centre frequencies in priority order (first online station on each freq wins)
    std::map<std::string, taxiOutStands>             taxiOnlyZones           = {};   ///< Apron/zone polygons that always force taxi planning mode regardless of stand or clearance state
    std::map<std::string, taxiOutStands>             taxiOutStands           = {};   ///< Taxi-out stand polygons
    napReminder                                      nap_reminder            = {};   ///< NAP reminder configuration
    std::string                                      defaultAppFreq;                 ///< Default approach frequency (used when no SID-specific one matches)
    std::map<std::string, std::string>               nightTimeSids             = {}; ///< Truncated night SID key -> full SID name prefix (filed name has last char dropped; display restores it and appends "*")
    std::map<std::string, std::vector<std::string>>  sidAppFreqs               = {}; ///< Approach frequency -> list of SIDs that use it
    std::map<std::string, std::vector<std::string>>  appFreqFallbacks          = {}; ///< Target approach frequency -> ordered list of approach frequencies to try (target first, then fallbacks)
    std::map<std::string, runway>                    runways                   = {}; ///< Runway configurations keyed by designator
    std::vector<std::string>                         taxiIntersections         = {}; ///< Intersection taxiway refs or prefix patterns (e.g. "Exit *") reclassified from taxiway to intersection type during OSM annotation.
    std::vector<std::string>                         scratchpadClearExclusions = {}; ///< Scratchpad prefixes exempt from auto-clear on LINEUP/DEPA click (e.g. ".cs", ".did"); comparison is case-insensitive
    std::vector<TaxiFlowRule>                        taxiFlowGeneric           = {}; ///< Taxiway direction rules always active regardless of runway configuration.
    std::map<std::string, standRoutingTarget>        standRoutingTargets       = {}; ///< Stand name → routing override; redirects inbound routing to a holding point or co-located stand (e.g. G16 → F16).
    std::map<std::string, std::vector<TaxiFlowRule>> taxiFlowConfigs           = {}; ///< Per-runway-config rules keyed by canonical "<dep>_<arr>" string (e.g. "16/29_16"); merged on top of taxiFlowGeneric at routing/render time.
    std::map<std::string, double>                    taxiWingspanMax           = {}; ///< Taxiway/taxilane ref -> maximum wingspan in metres (e.g. "P" -> 36.0); aircraft wider than the limit are hard-blocked.
    std::map<std::string, double>                    taxiWingspanAvoid         = {}; ///< Taxiway/taxilane ref -> max wingspan (m); aircraft at or below this size receive a soft routing penalty on this ref (prefer a parallel, narrower lane instead).
    std::vector<std::array<std::string, 2>>          taxiLaneSwingoverPairs    = {}; ///< Pairs of taxilane refs that allow free swingover (e.g. {"TL 40 \"Blue Line\"", "TL 40 \"Orange Line\""}).
    TaxiNetworkConfig                                taxiNetworkConfig         = {}; ///< Tunable taxi graph, routing, snapping, and safety parameters (all fields default when absent from config.json).
    DifliAirportConfig                               diflis                    = {}; ///< DIFLIS window configuration for this airport; loaded from the top-level "diflis" block in config.json.
};
