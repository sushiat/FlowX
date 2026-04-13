/**
 * @file DifliModel.h
 * @brief Data model for the DIFLIS (Digital Flight Strip) window.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <ctime>
#include <deque>
#include <string>
#include <vector>
#include <windows.h>

/// @brief Visual variant in which a single flight strip is rendered.
/// @note Variants are chosen per group: preferred first, fallback picked when contents overflow.
enum class DifliStripVariant
{
    Collapsed,  ///< Single-row compact strip (callsign, type, stand, sid, rwy, status)
    Expanded    ///< Two-row strip with more fields (runways, taxi, apron, standing-by)
};

/// @brief Optional ordering applied to strips inside a group before rendering.
enum class DifliSortMode
{
    None,    ///< Insertion / derivation order
    EtaAsc,  ///< Nearest ETA first (arrivals)
    EobtAsc  ///< Earliest EOBT first (departures)
};

/// @brief Named strip background colour keys used by group definitions and strip cache entries.
/// Actual COLORREF values are resolved in DrawDifliWindow from a palette in constants.h.
enum class DifliBgColor
{
    NeutralGrey,
    Blue,
    Yellow,
    Green,
    Orange,
    Pink,
    White
};

/// @brief Declarative definition of one group (section) loaded from config.json under "diflis".
struct DifliGroupDef
{
    std::string       id;                                             ///< Stable group identifier (e.g. "ARRIVALS", "RWY_16_34")
    std::string       title;                                          ///< All-caps title rendered with spaced letters
    std::string       rightHeaderText;                                ///< Optional right-aligned header annotation (e.g. "MANUAL", "ETA ^")
    int               columnIndex        = 0;                         ///< 0-based column index (0..3)
    float             heightWeight       = 1.0f;                      ///< Initial percent within its column (flex groups rebalance dynamically)
    bool              collapseWhenEmpty  = false;                     ///< Skip entirely from layout when no strips are assigned
    bool              stackBottom        = false;                     ///< Stack strips upward from the bottom of the group area
    bool              acceptsDrop        = true;                      ///< False for auto-only groups (e.g. LOCAL, CLEARANCE_DELIVERED)
    DifliStripVariant preferredVariant   = DifliStripVariant::Collapsed;
    DifliStripVariant fallbackVariant    = DifliStripVariant::Collapsed;
    bool              paginates          = false;                     ///< If true, overflow paginates instead of downgrading variant
    DifliSortMode     sort               = DifliSortMode::None;
};

/// @brief Cached display data for one flight strip, rebuilt every tick by UpdateTagCache.
struct DifliStripCache
{
    std::string  callsign;                                             ///< Aircraft callsign (unique key within the cache)
    char         wtc             = ' ';                                ///< Wake turbulence category (L/M/H/J)
    std::string  acType;                                               ///< ICAO aircraft type (e.g. "B738")
    std::string  stand;                                                ///< Assigned stand / gate (if known)
    std::string  rwy;                                                  ///< Assigned runway (e.g. "16", "29")
    std::string  squawk;                                               ///< Assigned SSR code
    std::string  status;                                               ///< Short status label (ground state or airborne indicator)
    std::string  originOrDest;                                         ///< ADEP for arrivals, ADES for departures
    std::string  sidOrStar;                                            ///< STAR for arrivals, SID for departures
    int          etaMinutes      = -1;                                 ///< Minutes to touchdown (arrivals sorted by this)
    std::time_t  eobt            = 0;                                  ///< UTC EOBT (departures sorted by this)
    COLORREF     bg              = RGB(210, 210, 210);                 ///< Strip base background color (from airport config)
    COLORREF     bgDark          = RGB(160, 160, 160);                 ///< Strip darker accent (callsign + status cells)
    COLORREF     text            = RGB(0, 0, 0);                       ///< Main text color
    COLORREF     textDim          = RGB(90, 90, 90);                    ///< Dimmed text color (sid, runway)
    std::string  resolvedGroupId;                                      ///< Group this strip was placed into after override resolution
    bool         isInbound       = false;
};

/// @brief Single field mutation recorded for a manual strip move; used by UNDO to restore precise state.
enum class DifliMutationField
{
    DifliOverride,        ///< Entry in the DIFLIS per-callsign override map
    GroundRadarStatus,    ///< Ground Radar "status" scratchpad field
    TopskyClearanceFlag,  ///< TopSky clearance flag
    AssignedRunway        ///< Flight plan assigned runway
};

/// @brief Before-state for one field touched by a manual DIFLIS move.
struct DifliMutation
{
    DifliMutationField field       = DifliMutationField::DifliOverride;
    std::string        prevValue;                                      ///< Previous value; meaningful only when prevExisted
    bool               prevExisted = false;                            ///< Distinguishes "was unset" from "was empty string"
};

/// @brief One undoable manual move. May record several field mutations for a single drop.
struct DifliUndoEntry
{
    std::string                callsign;
    std::string                fromGroup;
    std::string                toGroup;
    std::vector<DifliMutation> mutations;
};
