#pragma once

#include <string>
#include <vector>
#include "constants.h"
#include "tagInfo.h"

/// @brief Pre-calculated tag text and colour for all cached tag items of one aircraft.
/// Updated every second by CDelHelX_Tags::UpdateTagCache() and on each radar position
/// change (for TTT and NM) by CDelHelX_Tags::UpdatePositionDerivedTags().
struct CachedTagData {
    /// Shared computed values — calculated once per update, reused across multiple tag items.
    double distToRunwayThreshold = -1.0; ///< Distance (NM) to the departure runway threshold; shared by TWR_NEXT_FREQ and TWR_SORT
    bool   atHoldingPoint        = false; ///< True while the aircraft is inside any holding-point polygon; shared by TWR_NEXT_FREQ

    tagInfo sameSid;           ///< TAG_ITEM_SAMESID
    tagInfo takeoffSpacing;    ///< TAG_ITEM_TAKEOFF_SPACING
    tagInfo twrNextFreq;       ///< TAG_ITEM_TWR_NEXT_FREQ
    tagInfo twrSort;           ///< TAG_ITEM_TWR_SORT
    tagInfo holdingPoint;      ///< TAG_ITEM_HP
    tagInfo gndStateExpanded;  ///< TAG_ITEM_GND_STATE_EXPANDED
    tagInfo departureInfo;     ///< TAG_ITEM_DEPARTURE_INFO — without the overlay-only ",T" transfer suffix
    tagInfo assignedRunway;    ///< TAG_ITEM_ASSIGNED_RUNWAY
    tagInfo assignedArrRwy;    ///< TAG_ITEM_ASSIGNED_ARR_RUNWAY
    tagInfo ttt;               ///< TAG_ITEM_TTT — also refreshed on every position change
    tagInfo inboundNm;         ///< TAG_ITEM_INBOUND_NM — also refreshed on every position change
    tagInfo suggestedVacate;   ///< TAG_ITEM_SUGGESTED_VACATE
};

/// @brief Cached display data for one row in the DEP/H departure-rate window.
struct DepRateRowCache {
    std::string runway;
    std::string countStr;
    COLORREF    countColor   = TAG_COLOR_DEFAULT_GRAY;
    std::string spacingStr;
    COLORREF    spacingColor = TAG_COLOR_DEFAULT_GRAY;
};

/// @brief Cached display data for one row in the TWR Outbound custom window.
/// Rows are pre-sorted by sortKey ascending before being stored.
struct TwrOutboundRowCache {
    std::string callsign;
    COLORREF    callsignColor = TAG_COLOR_LIST_GRAY; ///< Callsign colour: gray/white/brown by tracking state
    char        wtc          = ' '; ///< Aircraft weight turbulence category character (L/M/H/J)
    tagInfo     status;             ///< TAG_ITEM_GND_STATE_EXPANDED
    tagInfo     depInfo;            ///< TAG_ITEM_DEPARTURE_INFO
    tagInfo     rwy;                ///< TAG_ITEM_ASSIGNED_RUNWAY
    tagInfo     sameSid;            ///< TAG_ITEM_SAMESID
    std::string aircraftType;       ///< Aircraft type string (e.g. "B738") from GetAircraftFPType()
    tagInfo     nextFreq;           ///< TAG_ITEM_TWR_NEXT_FREQ
    tagInfo     hp;                 ///< TAG_ITEM_HP
    tagInfo     spacing;            ///< TAG_ITEM_TAKEOFF_SPACING
    std::string sortKey;            ///< TAG_ITEM_TWR_SORT text used for row ordering (not displayed)
    bool        dimmed = false;              ///< True for departed+untracked aircraft — draw at reduced font size
    bool        groupSeparatorAbove = false; ///< True when a blank separator row should be drawn above this row
};

/// @brief Cached display data for one row in the TWR Inbound custom window.
/// Rows are pre-ordered by ttt_sortedByRunway (nearest inbound first per runway).
struct TwrInboundRowCache {
    std::string callsign;
    COLORREF    callsignColor = TAG_COLOR_LIST_GRAY; ///< Callsign colour: gray/white/brown by tracking state
    char        wtc          = ' '; ///< Aircraft weight turbulence category character
    int         groundSpeed  = 0;   ///< Current ground speed in knots
    std::string rwyGroup;           ///< Runway designator this row belongs to (for group separators)
    std::string sortKey;            ///< Full "rwy_mm:ss" string used for ordering (not displayed)
    tagInfo     ttt;                ///< TAG_ITEM_TTT — display text has the "rwy_" prefix stripped
    tagInfo     nm;                 ///< TAG_ITEM_INBOUND_NM
    std::string aircraftType;       ///< Aircraft type string (e.g. "B738") from GetAircraftFPType()
    std::string gate;               ///< Assigned stand/gate from standAssignment map
    tagInfo     vacate;             ///< TAG_ITEM_SUGGESTED_VACATE
    tagInfo     arrRwy;             ///< TAG_ITEM_ASSIGNED_ARR_RUNWAY
    bool        dimmed = false;     ///< True for non-closest aircraft per runway — draw at reduced font size
};
