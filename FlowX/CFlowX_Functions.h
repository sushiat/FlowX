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
    /// @brief Appends the aircraft to the end of the departure queue for its runway (right-click shortcut).
    /// @param fp Currently selected flight plan.
    void Func_AppendQueuePos(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Opens an "Assign HP" popup list of all assignable holding points for the selected flight plan.
    /// @param fp Currently selected flight plan.
    /// @param Pt Screen position at which to display the popup.
    void Func_AssignHp(EuroScopePlugIn::CFlightPlan& fp, POINT Pt);

    /// @brief Opens a dynamic popup (positions 1 … max+1) to assign or insert a departure queue position.
    /// @param fp Currently selected flight plan.
    /// @param Pt Screen position at which to display the popup.
    void Func_AssignQueuePos(EuroScopePlugIn::CFlightPlan& fp, POINT Pt);

    /// @brief Clears the new-QNH flag (character 'Q') from flight-strip annotation slot 8.
    /// @param fp Currently selected flight plan.
    void Func_ClearNewQnh(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Records cleared-to-land state, drops tracking, and triggers TopSky highlight.
    /// @param fp Currently selected flight plan.
    /// @param radarScreenInstance Active radar screen used to invoke the TopSky tag function.
    void Func_ClrdToLand(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance);

    /// @brief Callback invoked when the controller selects an item from the HP popup list.
    /// @param fp Currently selected flight plan.
    /// @param sItemString The holding-point name string chosen from the popup.
    void Func_HpListselect(EuroScopePlugIn::CFlightPlan& fp, const char* sItemString);

    /// @brief Callback invoked when the controller selects a position from the departure queue popup.
    /// @param fp Currently selected flight plan.
    /// @param sItemString The numeric position string chosen from the popup (e.g. "2").
    void Func_QueuePosListselect(EuroScopePlugIn::CFlightPlan& fp, const char* sItemString);

    /// @brief Sets the LINEUP ground state on the flight plan via a scratch-pad toggle.
    /// @param fp Currently selected flight plan.
    void Func_LineUp(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Handles a missed approach: immediately promotes the aircraft to go-around state in the TTT list, clears cleared-to-land, takes tracking, assigns 5000 ft, and triggers TopSky highlight.
    /// @param fp Currently selected flight plan.
    /// @param radarScreenInstance Active radar screen used to invoke the TopSky tag function.
    void Func_MissedApp(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance);

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
    void Func_TakeOff(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Transfers the flight plan to the next controller (approach, centre, or drops tracking).
    /// @param fp Currently selected flight plan.
    /// @note Picks the SID-specific approach frequency when available; falls back to UNICOM if no station is online.
    void Func_TransferNext(EuroScopePlugIn::CFlightPlan& fp);

    /// @brief Removes an aircraft from the departure queue and shifts all same-runway positions down.
    /// @param callSign Callsign to remove.
    /// @param runway Departure runway designator used to scope the shift.
    void RemoveFromDepartureQueue(const std::string& callSign, const std::string& runway);

    /// @brief Scans dep_queuePos and removes any aircraft already in LINEUP or DEPA state.
    /// @note Called periodically from OnTimer to handle queue updates from other controllers.
    void SyncQueueWithGroundState();

  public:
    /// @brief Clears the new-QNH flag from all aircraft that have it set in flight-strip annotation slot 8.
    /// @note Iterates all flight plans and calls Func_ClearNewQnh() for each with annotation slot 8 character 0 == 'Q'.
    void DismissQnh();

    /// @brief Marks the flight strip annotation with the GND frequency, drops tracking, and removes the GND transfer square.
    /// @param callSign Callsign of the landed aircraft being handed to ground.
    /// @note Resolves the correct GND frequency via geoGndFreq polygons, falling back to the airport default.
    void Func_GndTransfer(const std::string& callSign);

    /// @brief Drains one task per call from the redo-flag queue; must be called on the main thread.
    /// @note Called from OnTimer() once per second to throttle ground-status re-pushes.
    void DrainRedoFlagQueue();

    /// @brief Re-evaluates and re-sets the EuroScope clearance flag for all ground-based cleared aircraft,
    ///        then queues a throttled ground-status re-push for each cleared aircraft.
    /// @note Used to recover from flag/state corruption; operates on untracked and self-tracked aircraft only.
    void RedoFlags();
};
