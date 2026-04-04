/**
 * @file cachedTagData.h
 * @brief Cache row structs for custom GDI windows (TWR Outbound, TWR Inbound, DEP/H, WX/ATIS).
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <string>
#include <vector>
#include "constants.h"
#include "tagInfo.h"

/// @brief Cached display data for one row in the DEP/H departure-rate window.
struct DepRateRowCache
{
    std::string runway;
    std::string countStr;
    COLORREF    countColor = TAG_COLOR_DEFAULT_GRAY;
    std::string spacingStr;
    COLORREF    spacingColor = TAG_COLOR_DEFAULT_GRAY;
};

/// @brief Cached display data for one row in the TWR Outbound custom window.
/// Rows are pre-sorted by sortKey ascending before being stored.
struct TwrOutboundRowCache
{
    std::string callsign;
    COLORREF    callsignColor = TAG_COLOR_LIST_GRAY; ///< Callsign colour: gray/white/brown by tracking state
    char        wtc           = ' ';                 ///< Aircraft weight turbulence category character (L/M/H/J)
    tagInfo     status;                              ///< TAG_ITEM_GND_STATE_EXPANDED
    tagInfo     depInfo;                             ///< TAG_ITEM_DEPARTURE_INFO
    tagInfo     rwy;                                 ///< TAG_ITEM_ASSIGNED_RUNWAY
    tagInfo     sameSid;                             ///< TAG_ITEM_SAMESID
    std::string aircraftType;                        ///< Aircraft type string (e.g. "B738") from GetAircraftFPType()
    tagInfo     nextFreq;                            ///< TAG_ITEM_TWR_NEXT_FREQ
    tagInfo     hp;                                  ///< TAG_ITEM_HP
    tagInfo     spacing;                             ///< TAG_ITEM_TAKEOFF_SPACING
    tagInfo     liveSpacing;                         ///< Live current distance to the previous departure, updated on every position report.
    tagInfo     timeSinceTakeoff;                    ///< Elapsed time since takeoff roll start in "M:SS" format; empty while waiting
    std::string sortKey;                             ///< TAG_ITEM_TWR_SORT text used for row ordering (not displayed)
    bool        dimmed              = false;         ///< True for departed+untracked or TAXI-state aircraft far from holding point — draw at reduced font size
    bool        groupSeparatorAbove = false;         ///< True when a blank separator row should be drawn above this row
};

/// @brief Cached display data for one airport section in the WX/ATIS window.
struct WeatherRowCache
{
    std::string icao;
    std::string wind;
    COLORREF    windColor = TAG_COLOR_WHITE;
    std::string qnh;
    COLORREF    qnhColor = TAG_COLOR_WHITE;
    std::string atis;
    COLORREF    atisColor = TAG_COLOR_DEFAULT_GRAY;
    std::string rvr; ///< Formatted RVR string; empty = not present, don't draw second line
    COLORREF    rvrColor = TAG_COLOR_DEFAULT_GRAY;
};

/// @brief Cached display data for one row in the TWR Inbound custom window.
/// Rows are pre-ordered by ttt_sortedByRunway (nearest inbound first per runway).
struct TwrInboundRowCache
{
    std::string callsign;
    COLORREF    callsignColor = TAG_COLOR_LIST_GRAY; ///< Callsign colour: gray (unrelated), brown (handover pending), white (tracking), turq (cleared to land)
    char        wtc           = ' ';                 ///< Aircraft weight turbulence category character
    int         groundSpeed   = 0;                   ///< Current ground speed in knots
    std::string rwyGroup;                            ///< Runway designator this row belongs to (for group separators)
    std::string sortKey;                             ///< Full "rwy_mm:ss" string used for ordering (not displayed)
    tagInfo     ttt;                                 ///< TAG_ITEM_TTT — display text has the "rwy_" prefix stripped
    tagInfo     nm;                                  ///< TAG_ITEM_INBOUND_NM
    std::string aircraftType;                        ///< Aircraft type string (e.g. "B738") from GetAircraftFPType()
    tagInfo     gate;                                ///< Assigned stand/gate from standAssignment map; red if stand is occupied
    tagInfo     vacate;                              ///< TAG_ITEM_SUGGESTED_VACATE
    tagInfo     arrRwy;                              ///< TAG_ITEM_ASSIGNED_ARR_RUNWAY
    bool        dimmed = false;                      ///< True for non-closest aircraft per runway — draw at reduced font size
};
