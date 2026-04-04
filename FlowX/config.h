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
    std::string name;          ///< Fix identifier for debugging (e.g. "WW008")
    double      lat   = 0.0;  ///< Fix latitude (decimal degrees)
    double      lon   = 0.0;  ///< Fix longitude (decimal degrees)
    int         altFt = 0;    ///< Published altitude constraint (ft MSL); 0 = no per-fix altitude gate
};

/// @brief Configuration for a single runway including threshold, holding points, SID groups and vacate points.
struct runway
{
    std::vector<approachFix>            approachFixes = {}; ///< Approach fixes for early non-straight-in RNP detection; empty = straight-in only.
    std::string                         designator;         ///< Runway designator (e.g. "11")
    std::string                         opposite;           ///< Designator of the reciprocal runway (used for go-around detection)
    double                              thresholdLat = 0.0; ///< Runway threshold latitude
    double                              thresholdLon = 0.0; ///< Runway threshold longitude
    std::string                         twrFreq;            ///< Tower frequency for this runway
    std::string                         goAroundFreq;       ///< Go-around frequency for this runway
    int                                 widthMeters   = 0;  ///< Runway width in metres (from config "width")
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
