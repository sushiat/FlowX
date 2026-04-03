/**
 * @file CDelHelX_Tags.h
 * @brief Declaration of CDelHelX_Tags, the EuroScope tag item rendering layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include "CDelHelX_Timers.h"
#include "tagInfo.h"

/// @brief Plugin layer providing the 4 tag item columns still served via EuroScope's tag item mechanism.
class CDelHelX_Tags : public CDelHelX_Timers
{
  protected:
    bool groundOverride = false; ///< When true, behaves as if a ground station is online (for testing)
    bool noChecks       = false; ///< When true, skips flight-plan validation checks (offline testing only)
    bool towerOverride  = false; ///< When true, behaves as if a tower station is online (for testing)

    /// @brief Builds the ADES tag: destination ICAO for normal plans, last IFR waypoint (turquoise) for type-Y.
    /// Returns from adesCache; colour TAG_COLOR_DEFAULT_NONE signals that EuroScope's default colour should be used.
    /// @param fp Flight plan being evaluated.
    /// @return tagInfo with the destination or last IFR fix and appropriate colour.
    tagInfo GetAdesTag(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Builds the new-QNH tag showing an orange "X" when a QNH change was received.
    /// @param fp Flight plan being evaluated.
    /// @return tagInfo with "X" in orange, or empty.
    tagInfo GetNewQnhTag(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Builds the Push+Start helper tag showing the next applicable frequency or a validation error.
    /// @param fp Flight plan being evaluated.
    /// @param rt Correlated radar target.
    /// @return tagInfo with frequency string and colour, or an error code in red.
    tagInfo GetPushStartHelperTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);

    /// @brief Builds the same-SID tag showing the SID name colour-coded by group.
    /// @param fp Flight plan being evaluated.
    /// @return tagInfo with the SID name and its configured colour.
    tagInfo GetSameSidTag(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Builds the taxi-out indicator tag ("T" for taxi-out stand, "P" for push stand).
    /// @param fp Flight plan being evaluated.
    /// @param rt Correlated radar target.
    /// @return tagInfo with "T", "P", or empty text.
    tagInfo GetTaxiOutTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);
};
