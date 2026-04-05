/**
 * @file config.h
 * @brief Airport configuration structures loaded from config.json at startup.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <string>
#include <map>
#include <vector>

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
    std::vector<double> lat = {};           ///< Polygon vertex latitudes
    std::vector<double> lon = {};           ///< Polygon vertex longitudes
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
    std::string              icao;   ///< Airport ICAO code
    std::string              name;   ///< Stand designator (e.g. "B67")
    std::vector<double>      lat;    ///< Polygon vertex latitudes
    std::vector<double>      lon;    ///< Polygon vertex longitudes
    std::vector<standBlock>  blocks; ///< Stands blocked when this one is occupied
};

/// @brief Suggested runway vacate point with a minimum gap requirement and associated stands.
struct vacatePoint
{
    double                   minGap = 0.0; ///< Minimum distance gap (NM) from the preceding inbound required for this vacate
    std::vector<std::string> stands = {};  ///< Stand names associated with this vacate point
};

/// @brief A named approach fix for early TTT detection of non-straight-in RNP approaches.
struct approachFix
{
    std::string name;                        ///< Fix identifier for debugging (e.g. "WW008")
    double      lat          = 0.0;          ///< Fix latitude (decimal degrees)
    double      lon          = 0.0;          ///< Fix longitude (decimal degrees)
    int         altMinFt     = 0;            ///< Computed lower bound (ft MSL): altitude - altOffsetBelow. 0 = no lower gate.
    int         altMaxFt     = 0;            ///< Computed upper bound (ft MSL): altitude + altOffsetAbove. 0 = no upper gate.
    std::string legType      = "straight";   ///< Incoming leg type: "straight", "arcLeft", or "arcRight". IAF has no incoming leg (legLengthNm == 0).
    double      legLengthNm  = 0.0;         ///< Along-track length of the incoming leg in NM; 0 = IAF (no incoming leg).
    double      arcCenterLat     = 0.0; ///< Derived arc centre latitude (decimal degrees); 0 for straight legs.
    double      arcCenterLon     = 0.0; ///< Derived arc centre longitude (decimal degrees); 0 for straight legs.
    double      arcRadiusNm      = 0.0; ///< Derived arc radius in NM; 0 for straight legs.
    double      detectionRadiusNm = 0.0; ///< Proximity radius (NM) for initial TTT detection; 0 = this fix does not trigger detection.
    int         iafHeading        = 0;   ///< Expected inbound heading (degrees) at this IAF; 0 = no heading check.
};

/// @brief An ordered sequence of approach fixes belonging to a single named non-straight-in approach.
struct approachPath
{
    std::string              name;  ///< Approach identifier (e.g. "RNP N")
    std::vector<approachFix> fixes; ///< Ordered list of fixes along the approach path (IAF first).
};

/// @brief Configuration for a single runway including threshold, holding points, SID groups and vacate points.
struct runway
{
    std::vector<approachPath>            gpsApproachPaths = {}; ///< Non-straight-in GPS approach paths for early TTT detection; empty = straight-in only.
    std::string                         designator;         ///< Runway designator (e.g. "11")
    std::string                         opposite;           ///< Designator of the reciprocal runway (used for go-around detection)
    double                              thresholdLat = 0.0; ///< Runway threshold latitude
    double                              thresholdLon = 0.0; ///< Runway threshold longitude
    std::string                         twrFreq;            ///< Tower frequency for this runway
    std::string                         goAroundFreq;       ///< Go-around frequency for this runway
    int                                 thresholdElevationFt = 0;  ///< Threshold elevation in feet (overrides airport fieldElevation for TTT altitude gates; 0 = use fieldElevation)
    int                                 widthMeters          = 0;  ///< Runway width in metres (from config "width")
    std::map<std::string, holdingPoint> holdingPoints = {}; ///< Named holding points on this runway
    std::map<std::string, int>          sidGroups     = {}; ///< SID key -> group number (built from config "sidGroups": { "1": [...sids] })
    std::map<std::string, std::string>  sidColors     = {}; ///< SID key -> colour name (built from config "sidColors": { "green": [...sids] })
    std::map<std::string, vacatePoint>  vacatePoints  = {}; ///< Named vacate points on this runway
};

/// @brief Full configuration for a single airport loaded from config.json.
struct airport
{
    std::string                                     icao;                         ///< ICAO code (e.g. "LOWW")
    std::string                                     gndFreq;                      ///< Default ground frequency string
    int                                             fieldElevation          = 0;  ///< Field elevation in feet (used to detect airborne state)
    int                                             airborneTransfer        = 0;  ///< Altitude (ft) above which the TWR next-freq tag changes colour
    int                                             airborneTransferWarning = 0;  ///< Altitude (ft) above which the TWR next-freq tag blinks orange
    std::map<std::string, geoGndFreq>               geoGndFreq              = {}; ///< Geographic ground frequency zones
    std::vector<std::string>                        ctrStations             = {}; ///< Centre frequencies in priority order (first online station on each freq wins)
    std::map<std::string, taxiOutStands>            taxiOutStands           = {}; ///< Taxi-out stand polygons
    napReminder                                     nap_reminder            = {}; ///< NAP reminder configuration
    std::string                                     defaultAppFreq;               ///< Default approach frequency (used when no SID-specific one matches)
    std::map<std::string, std::string>              nightTimeSids    = {};        ///< Truncated night SID key -> full SID name prefix (filed name has last char dropped; display restores it and appends "*")
    std::map<std::string, std::vector<std::string>> sidAppFreqs      = {};        ///< Approach frequency -> list of SIDs that use it
    std::map<std::string, std::vector<std::string>> appFreqFallbacks = {};        ///< Target approach frequency -> ordered list of approach frequencies to try (target first, then fallbacks)
    std::map<std::string, runway>                   runways          = {};        ///< Runway configurations keyed by designator
};
