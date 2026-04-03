/**
 * @file CFlowX_Functions.h
 * @brief Declaration of CFlowX_Functions, the tag function callback layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include "CFlowX_CustomTags.h"

/// @brief Plugin layer that implements all tag-function callbacks triggered by controller clicks.
class CFlowX_Functions : public CFlowX_CustomTags
{
  protected:
    /// @brief Opens an "Assign HP" popup list of all assignable holding points for the selected flight plan.
    /// @param fp Currently selected flight plan.
    /// @param Pt Screen position at which to display the popup.
    void Func_AssignHp(EuroScopePlugIn::CFlightPlan& fp, POINT Pt);

    /// @brief Clears the new-QNH flag (character 'Q') from flight-strip annotation slot 8.
    /// @param fp Currently selected flight plan.
    void Func_ClearNewQnh(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Clears the inbound for landing, drops tracking, and triggers TopSky highlight.
    /// @param fp Currently selected flight plan.
    /// @param radarScreenInstance Active radar screen used to invoke the TopSky tag function.
    static void Func_ClrdToLand(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance);

    /// @brief Callback invoked when the controller selects an item from the HP popup list.
    /// @param fp Currently selected flight plan.
    /// @param sItemString The holding-point name string chosen from the popup.
    void Func_HpListselect(EuroScopePlugIn::CFlightPlan& fp, const char* sItemString);

    /// @brief Sets the LINEUP ground state on the flight plan via a scratch-pad toggle.
    /// @param fp Currently selected flight plan.
    static void Func_LineUp(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Handles a missed approach: takes tracking, assigns 5000 ft, and triggers TopSky highlight.
    /// @param fp Currently selected flight plan.
    /// @param radarScreenInstance Active radar screen used to invoke the TopSky tag function.
    static void Func_MissedApp(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance);

    /// @brief Handles the ONFREQ / ST-UP / PUSH function: sets the appropriate ground state based on position.
    /// @param fp Currently selected flight plan.
    /// @param rt Correlated radar target.
    void Func_OnFreq(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);

    /// @brief Opens a "Request HP" popup list of assignable holding points (each entry appended with '*').
    /// @param fp Currently selected flight plan.
    /// @param Pt Screen position at which to display the popup.
    void Func_RequestHp(EuroScopePlugIn::CFlightPlan& fp, POINT Pt);

    /// @brief Reverts the ground state from LINEUP back to TAXI via a scratch-pad toggle.
    /// @param fp Currently selected flight plan.
    static void Func_RevertToTaxi(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Triggers automatic stand assignment via the Ground Radar plugin.
    /// @param fp Currently selected flight plan.
    /// @param radarScreenInstance Active radar screen used to invoke the Ground Radar tag function.
    static void Func_StandAuto(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance);

    /// @brief Sets the DEPA ground state and starts tracking the flight plan.
    /// @param fp Currently selected flight plan.
    static void Func_TakeOff(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Transfers the flight plan to the next controller (approach, centre, or drops tracking).
    /// @param fp Currently selected flight plan.
    /// @note Picks the SID-specific approach frequency when available; falls back to UNICOM if no station is online.
    void Func_TransferNext(EuroScopePlugIn::CFlightPlan& fp);
};
