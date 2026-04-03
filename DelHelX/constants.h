#pragma once

#include <regex>

/// Plugin display name shown in EuroScope
#define PLUGIN_NAME    "DelHelX"
/// Current plugin version string (semver)
#define PLUGIN_VERSION "0.6.0"
/// Plugin author name
#define PLUGIN_AUTHOR  "Markus Korbel"
/// Plugin license string
#define PLUGIN_LICENSE "(c) 2026, MIT License"
/// URL to the plain-text file containing the latest released version number
#define PLUGIN_LATEST_VERSION_URL "https://raw.githubusercontent.com/sushiat/DelHelX/master/version.txt"
/// URL to the GitHub releases page for downloading the latest build
#define PLUGIN_LATEST_DOWNLOAD_URL "https://github.com/sushiat/DelHelX/releases/latest"
/// URL to the VATSIM v3 data feed (updates every ~15 s)
#define VATSIM_DATA_URL "https://data.vatsim.net/v3/vatsim-data.json"

/// Delimiter character used when serialising plugin settings to EuroScope storage
const char SETTINGS_DELIMITER = '|';

/// @defgroup TagItemTypes Tag item type IDs registered with EuroScope
/// @{
const int TAG_ITEM_PS_HELPER           = 1;  ///< Push+Start helper column
const int TAG_ITEM_TAXIOUT             = 2;  ///< Taxi-out stand indicator (P/T)
const int TAG_ITEM_NEWQNH              = 3;  ///< New QNH orange "X" marker
const int TAG_ITEM_SAMESID             = 4;  ///< Same-SID tracker column
const int TAG_ITEM_ADES                = 5;  ///< Destination airport (or last IFR fix for type-Y plans)
// 6, 7 unused (were HP1, HP2, HP3 tag item slots)
const int TAG_ITEM_HP                  = 8;  ///< Holding point (popup-assigned)
const int TAG_ITEM_ASSIGNED_ARR_RUNWAY = 9;  ///< Assigned arrival runway
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
// 102–107 unused (were Assign/Request HP1, HP2, HP3 functions)
const int TAG_FUNC_ASSIGN_HP       = 108; ///< Open popup to assign a holding point
const int TAG_FUNC_REQUEST_HP      = 109; ///< Open popup to request a holding point (appends '*')
const int TAG_FUNC_HP_LISTSELECT   = 110; ///< Callback when user selects an item from the HP popup list
const int TAG_FUNC_LINE_UP         = 111; ///< Set LINEUP ground state
const int TAG_FUNC_TAKE_OFF        = 112; ///< Set DEPA ground state and start tracking
const int TAG_FUNC_TRANSFER_NEXT   = 113; ///< Initiate handoff to the next controller
const int TAG_FUNC_CLRD_TO_LAND    = 114; ///< Clear the inbound for landing and highlight in TopSky
const int TAG_FUNC_MISSED_APP      = 115; ///< Handle missed approach: assign altitude and highlight
const int TAG_FUNC_STAND_AUTO      = 116; ///< Trigger automatic stand assignment via Ground Radar
const int TAG_FUNC_REVERT_TO_TAXI  = 117; ///< Revert ground state from LINEUP back to TAXI
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
const COLORREF TAG_COLOR_BROWN        = RGB(200, 160, 100); ///< Light brown (transfer-from-me initiated)
const COLORREF TAG_COLOR_LIST_GRAY    = RGB(180, 175, 165); ///< Slightly brighter grey for custom list windows
const COLORREF TAG_COLOR_DEFAULT_NONE = RGB(  1,   2,   3); ///< Sentinel: let EuroScope apply its default tag colour (not rendered)
/// @}

/// @defgroup ScreenObjects Screen object type IDs used with AddScreenObject
/// @{
const int SCREEN_OBJECT_DEP_TAG      = 6681; ///< Per-aircraft departure info custom tag (drag handle)
const int SCREEN_OBJECT_DEPRATE_WIN  = 6682; ///< Departure rate window header (drag handle)
const int SCREEN_OBJECT_TWR_OUT_WIN  = 6683; ///< TWR Outbound window header (drag handle)
const int SCREEN_OBJECT_TWR_IN_WIN   = 6684; ///< TWR Inbound window header (drag handle)
const int SCREEN_OBJECT_NAP_WIN      = 6685; ///< NAP reminder window header (drag handle)
const int SCREEN_OBJECT_NAP_ACK      = 6686; ///< NAP reminder ACK button
const int SCREEN_OBJECT_TWR_OUT_CELL = 6687; ///< Clickable cell in the TWR Outbound custom window
const int SCREEN_OBJECT_TWR_IN_CELL  = 6688; ///< Clickable cell in the TWR Inbound custom window
const int SCREEN_OBJECT_WEATHER_WIN  = 6689; ///< WX/ATIS window title bar (drag handle)
const int SCREEN_OBJECT_WEATHER_ROW  = 6690; ///< Clickable airport section in the WX/ATIS window (ack)
/// @}

/// @defgroup TopSkyIntegration Constants for TopSky plugin interop
/// @{
constexpr auto TOPSKY_PLUGIN_NAME              = "TopSky plugin"; ///< EuroScope name of the TopSky plugin
const int      TOPSKY_TAG_FUNC_SET_CLEARANCE_FLAG = 667;          ///< TopSky function ID to toggle the clearance flag
const int      TOPSKY_TAG_TYPE_ATYP               = 10028;        ///< TopSky tag item type for aircraft type
/// @}

/// @defgroup GroundRadarIntegration Constants for Ground Radar plugin interop
/// @{
constexpr auto GROUNDRADAR_PLUGIN_NAME                  = "Ground Radar plugin"; ///< EuroScope name of the Ground Radar plugin
const int      GROUNDRADAR_TAG_TYPE_GROUNDSTATUS          = 3;                   ///< Ground Radar tag item type for ground status
const int      GROUNDRADAR_TAG_TYPE_ASSIGNED_STAND        = 2;                   ///< Ground Radar tag item type for assigned stand
const int      GROUNDRADAR_TAG_FUNC_STAND_MENU            = 1;                   ///< Ground Radar function ID to open the stand assignment menu
/// @}
