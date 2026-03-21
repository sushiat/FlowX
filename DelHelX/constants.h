#pragma once

#include <regex>

/// Plugin display name shown in EuroScope
#define PLUGIN_NAME    "DelHelX"
/// Current plugin version string (semver)
#define PLUGIN_VERSION "0.5.0"
/// Plugin author name
#define PLUGIN_AUTHOR  "Markus Korbel"
/// Plugin license string
#define PLUGIN_LICENSE "(c) 2026, MIT License"
/// URL to the plain-text file containing the latest released version number
#define PLUGIN_LATEST_VERSION_URL "https://raw.githubusercontent.com/sushiat/DelHelX/master/version.txt"
/// URL to the GitHub releases page for downloading the latest build
#define PLUGIN_LATEST_DOWNLOAD_URL "https://github.com/sushiat/DelHelX/releases/latest"

/// Delimiter character used when serialising plugin settings to EuroScope storage
const char SETTINGS_DELIMITER = '|';

/// @defgroup TagItemTypes Tag item type IDs registered with EuroScope
/// @{
const int TAG_ITEM_PS_HELPER           = 1;  ///< Push+Start helper column
const int TAG_ITEM_TAXIOUT             = 2;  ///< Taxi-out stand indicator (P/T)
const int TAG_ITEM_NEWQNH              = 3;  ///< New QNH orange "X" marker
const int TAG_ITEM_SAMESID             = 4;  ///< Same-SID tracker column
const int TAG_ITEM_HP1                 = 5;  ///< Holding point slot 1
const int TAG_ITEM_HP2                 = 6;  ///< Holding point slot 2
const int TAG_ITEM_HP3                 = 7;  ///< Holding point slot 3
const int TAG_ITEM_HPO                 = 8;  ///< Holding point "other" (free-text) slot
// 9 unused
const int TAG_ITEM_TAKEOFF_SPACING     = 10; ///< Takeoff spacing / time-separation indicator
const int TAG_ITEM_ASSIGNED_RUNWAY     = 11; ///< Assigned departure runway
const int TAG_ITEM_DEPARTURE_INFO      = 12; ///< Departure information overlay (SID, T-flag)
const int TAG_ITEM_TTT                 = 13; ///< Time-to-touchdown for inbounds
const int TAG_ITEM_INBOUND_NM          = 14; ///< Distance to runway threshold for inbounds
const int TAG_ITEM_SUGGESTED_VACATE    = 15; ///< Suggested runway vacate point
const int TAG_ITEM_TWR_NEXT_FREQ       = 16; ///< Next frequency for tower handoff
const int TAG_ITEM_TWR_SORT            = 17; ///< Tower departure list sort key
const int TAG_ITEM_GND_STATE_EXPANDED  = 18; ///< Expanded ground-state label
/// @}

/// @defgroup TagFunctions Tag function IDs registered with EuroScope
/// @{
const int TAG_FUNC_ON_FREQ         = 100; ///< Set ONFREQ / ST-UP / PUSH ground state
const int TAG_FUNC_CLEAR_NEWQNH    = 101; ///< Clear the new-QNH annotation flag
const int TAG_FUNC_ASSIGN_HP1      = 102; ///< Assign holding point index 1
const int TAG_FUNC_ASSIGN_HP2      = 103; ///< Assign holding point index 2
const int TAG_FUNC_ASSIGN_HP3      = 104; ///< Assign holding point index 3
const int TAG_FUNC_REQUEST_HP1     = 105; ///< Request (starred) holding point index 1
const int TAG_FUNC_REQUEST_HP2     = 106; ///< Request (starred) holding point index 2
const int TAG_FUNC_REQUEST_HP3     = 107; ///< Request (starred) holding point index 3
const int TAG_FUNC_ASSIGN_HPO      = 108; ///< Open popup to assign a free-text holding point
const int TAG_FUNC_REQUEST_HPO     = 109; ///< Open popup to request a free-text holding point
const int TAG_FUNC_HPO_LISTSELECT  = 110; ///< Callback when user selects an item from the HP popup list
const int TAG_FUNC_LINE_UP         = 111; ///< Set LINEUP ground state
const int TAG_FUNC_TAKE_OFF        = 112; ///< Set DEPA ground state and start tracking
const int TAG_FUNC_TRANSFER_NEXT   = 113; ///< Initiate handoff to the next controller
const int TAG_FUNC_CLRD_TO_LAND    = 114; ///< Clear the inbound for landing and highlight in TopSky
const int TAG_FUNC_MISSED_APP      = 115; ///< Handle missed approach: assign altitude and highlight
const int TAG_FUNC_STAND_AUTO      = 116; ///< Trigger automatic stand assignment via Ground Radar
/// @}

/// @defgroup TagColors COLORREF constants used for tag colouring
/// @{
const COLORREF TAG_COLOR_DEFAULT_GRAY = RGB(135, 128, 118); ///< Default neutral grey
const COLORREF TAG_COLOR_RED          = RGB(255,   9,   9); ///< Error / warning red
const COLORREF TAG_COLOR_ORANGE       = RGB(255, 165,   0); ///< Caution orange
const COLORREF TAG_COLOR_GREEN        = RGB(  0, 200,   0); ///< OK / ready green
const COLORREF TAG_COLOR_TURQ         = RGB( 64, 224, 208); ///< Turquoise (airborne transfer)
const COLORREF TAG_COLOR_PURPLE       = RGB(219, 163, 250); ///< Purple accent
const COLORREF TAG_COLOR_WHITE        = RGB(255, 255, 255); ///< White (active / attention)
const COLORREF TAG_COLOR_DARKGREY     = RGB(135, 128, 118); ///< Dark grey (departed aircraft)
const COLORREF TAG_COLOR_YELLOW       = RGB(255, 201,  14); ///< Yellow (blinking urgent alert)
/// @}

/// @defgroup TopSkyIntegration Constants for TopSky plugin interop
/// @{
constexpr auto TOPSKY_PLUGIN_NAME              = "TopSky plugin"; ///< EuroScope name of the TopSky plugin
const int      TOPSKY_TAG_FUNC_SET_CLEARANCE_FLAG = 667;          ///< TopSky function ID to toggle the clearance flag
const int      TOPSKY_TAG_TYPE_ATYP               = 10028;        ///< TopSky tag item type for aircraft type
/// @}

/// @defgroup GroundRadarIntegration Constants for Ground Radar plugin interop
/// @{
constexpr auto GROUNDRADAR_PLUGIN_NAME         = "Ground Radar plugin"; ///< EuroScope name of the Ground Radar plugin
const int      GROUNDRADAR_TAG_TYPE_GROUNDSTATUS = 3;                   ///< Ground Radar tag item type for ground status
const int      GROUNDRADAR_ASSIGNED_STAND        = 2;                   ///< Ground Radar tag item type for assigned stand
/// @}
