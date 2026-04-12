/**
 * @file CFlowX_Functions.cpp
 * @brief Tag function callbacks; handles controller click actions for HP, lineup, transfer, and more.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CFlowX_Functions.h"

#include "helpers.h"

/// @brief Opens a popup list of assignable holding points so the controller can assign one.
/// @param fp Currently selected flight plan.
/// @param Pt Screen position at which to anchor the popup.
void CFlowX_Functions::Func_AssignHp(EuroScopePlugIn::CFlightPlan& fp, POINT Pt)
{
    RECT area;
    area.left   = Pt.x;
    area.right  = Pt.x + 100;
    area.top    = Pt.y;
    area.bottom = Pt.y + 100;
    this->OpenPopupList(area, "Assign HP", 1);

    std::string dep = fp.GetFlightPlanData().GetOrigin();
    to_upper(dep);
    std::string rwy     = fp.GetFlightPlanData().GetDepartureRwy();
    auto        airport = this->airports.find(dep);
    auto        rwyIt   = airport->second.runways.find(rwy);
    if (rwyIt != airport->second.runways.end())
    {
        std::vector<const std::pair<const std::string, holdingPoint>*> sorted;
        for (auto& kv : rwyIt->second.holdingPoints)
            if (kv.second.assignable)
                sorted.push_back(&kv);
        std::ranges::sort(sorted, {}, [](auto* kv)
                          { return kv->second.order; });
        for (auto* kv : sorted)
            this->AddPopupListElement(kv->first.c_str(), "", TAG_FUNC_HP_LISTSELECT);
    }
}

/// @brief Appends the aircraft to the end of the departure queue for its runway.
/// If already queued, the existing position is replaced with max+1.
/// @param fp Currently selected flight plan.
void CFlowX_Functions::Func_AppendQueuePos(EuroScopePlugIn::CFlightPlan& fp)
{
    std::string callSign = fp.GetCallsign();
    std::string runway   = fp.GetFlightPlanData().GetDepartureRwy();
    RemoveFromDepartureQueue(callSign, runway);

    int maxPos = 0;
    for (const auto& [cs, pos] : this->dep_queuePos)
    {
        auto other = this->FlightPlanSelect(cs.c_str());
        if (other.IsValid() && std::string(other.GetFlightPlanData().GetDepartureRwy()) == runway)
            maxPos = std::max(maxPos, pos);
    }
    this->dep_queuePos[callSign] = maxPos + 1;
}

/// @brief Opens a dynamic popup listing positions 1 … (max queued on same runway) + 1.
/// Selecting a position inserts the aircraft at that slot, shifting others down.
/// @param fp Currently selected flight plan.
/// @param Pt Screen position at which to display the popup.
void CFlowX_Functions::Func_AssignQueuePos(EuroScopePlugIn::CFlightPlan& fp, POINT Pt)
{
    std::string callSign = fp.GetCallsign();
    std::string runway   = fp.GetFlightPlanData().GetDepartureRwy();

    int maxPos = 0;
    for (const auto& [cs, pos] : this->dep_queuePos)
    {
        if (cs == callSign)
            continue;
        auto other = this->FlightPlanSelect(cs.c_str());
        if (other.IsValid() && std::string(other.GetFlightPlanData().GetDepartureRwy()) == runway)
            maxPos = std::max(maxPos, pos);
    }
    int listSize = maxPos + 1;

    RECT area = {Pt.x, Pt.y, Pt.x + 50, Pt.y + listSize * 18 + 4};
    this->OpenPopupList(area, "Queue", 1);
    for (int i = 1; i <= listSize; ++i)
        this->AddPopupListElement(std::to_string(i).c_str(), "", TAG_FUNC_QUEUE_POS_LISTSELECT);
}

/// @brief Callback: inserts the aircraft at the selected queue position, shifting others down.
/// @param fp Currently selected flight plan.
/// @param sItemString Numeric string of the chosen position (e.g. "2").
void CFlowX_Functions::Func_QueuePosListselect(EuroScopePlugIn::CFlightPlan& fp, const char* sItemString)
{
    std::string callSign  = fp.GetCallsign();
    std::string runway    = fp.GetFlightPlanData().GetDepartureRwy();
    int         targetPos = std::stoi(sItemString);

    RemoveFromDepartureQueue(callSign, runway);

    for (auto& [cs, pos] : this->dep_queuePos)
    {
        auto other = this->FlightPlanSelect(cs.c_str());
        if (other.IsValid() && std::string(other.GetFlightPlanData().GetDepartureRwy()) == runway && pos >= targetPos)
            ++pos;
    }
    this->dep_queuePos[callSign] = targetPos;
}

/// @brief Removes an aircraft from the departure queue and shifts same-runway positions down.
/// @param callSign Callsign to remove.
/// @param runway Departure runway designator used to scope the shift.
void CFlowX_Functions::RemoveFromDepartureQueue(const std::string& callSign, const std::string& runway)
{
    auto it = this->dep_queuePos.find(callSign);
    if (it == this->dep_queuePos.end())
        return;
    int removedPos = it->second;
    this->dep_queuePos.erase(it);
    for (auto& [cs, pos] : this->dep_queuePos)
    {
        auto other = this->FlightPlanSelect(cs.c_str());
        if (other.IsValid() && std::string(other.GetFlightPlanData().GetDepartureRwy()) == runway && pos > removedPos)
            --pos;
    }
}

/// @brief Scans dep_queuePos and removes any aircraft already in LINEUP or DEPA ground state.
/// Handles the case where another controller sets the state without going through the local click handler.
void CFlowX_Functions::SyncQueueWithGroundState()
{
    if (this->dep_queuePos.empty())
        return;

    std::vector<std::pair<std::string, std::string>> toRemove;
    for (const auto& [callSign, pos] : this->dep_queuePos)
    {
        auto fp = this->FlightPlanSelect(callSign.c_str());
        if (!fp.IsValid())
        {
            continue;
        }
        std::string gs = fp.GetGroundState();
        if (gs == "LINEUP" || gs == "DEPA")
            toRemove.emplace_back(callSign, std::string(fp.GetFlightPlanData().GetDepartureRwy()));
    }
    for (const auto& [cs, rwy] : toRemove)
        this->RemoveFromDepartureQueue(cs, rwy);
}

/// @brief Clears the new-QNH flag from all aircraft that have it set in flight-strip annotation slot 8.
void CFlowX_Functions::DismissQnh()
{
    for (EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelectFirst(); fp.IsValid(); fp = this->FlightPlanSelectNext(fp))
    {
        std::string callSign = fp.GetCallsign();
        std::string ann      = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
        if (!ann.empty() && ann[0] == 'Q')
        {
            this->flightStripAnnotation[callSign] = ann;
            this->Func_ClearNewQnh(fp);
        }
    }
}

/// @brief Clears the 'Q' flag from flight-strip annotation slot 8 and syncs to other controllers.
/// @param fp Currently selected flight plan.
void CFlowX_Functions::Func_ClearNewQnh(EuroScopePlugIn::CFlightPlan& fp)
{
    std::string callSign = fp.GetCallsign();
    if (!this->flightStripAnnotation[callSign].empty())
    {
        this->flightStripAnnotation[callSign][0] = ' ';
        fp.GetControllerAssignedData().SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
        this->PushToOtherControllers(fp);
    }
}

/// @brief Records cleared-to-land state, drops tracking, and triggers TopSky's highlight function.
/// @param fp Currently selected flight plan.
/// @param radarScreenInstance Active radar screen used to call the TopSky tag function.
void CFlowX_Functions::Func_ClrdToLand(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance)
{
    std::string callSign = fp.GetCallsign();
    this->ttt_clearedToLand.insert(callSign);
    if (fp.GetTrackingControllerIsMe())
    {
        fp.EndTracking();
    }
    radarScreenInstance->StartTagFunction(callSign.c_str(), nullptr, 0, "S-Highlight", TOPSKY_PLUGIN_NAME, 4, POINT(), RECT());
}

/// @brief Writes the user-selected holding-point name from the popup into flight-strip annotation slot 8.
/// @param fp Currently selected flight plan.
/// @param sItemString The holding-point name (possibly starred) chosen from the popup.
void CFlowX_Functions::Func_HpListselect(EuroScopePlugIn::CFlightPlan& fp, const char* sItemString)
{
    std::string callSign = fp.GetCallsign();

    // Read old annotation before overwriting — needed for approval feedback below.
    std::string oldAnn = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);

    this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], sItemString);
    fp.GetControllerAssignedData().SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());

    // If the previous HP was a pending request for the same name, notify GND via scratchpad.
    if (oldAnn.size() > 7)
    {
        std::string oldHp = oldAnn.substr(7);
        if (!oldHp.empty() && oldHp.back() == '*')
        {
            std::string oldHpName = oldHp.substr(0, oldHp.size() - 1);
            std::string oldUpper  = oldHpName;
            std::string newUpper  = std::string(sItemString);
            to_upper(oldUpper);
            to_upper(newUpper);
            if (oldUpper == newUpper)
            {
                fp.GetControllerAssignedData().SetScratchPadString(("." + oldHpName + " ok").c_str());
                this->LogDebugMessage(callSign + " HP request approved via popup: " + oldHpName + " → scratchpad set to ." + oldHpName + " ok", "HP");
            }
            else
            {
                // Different HP assigned — clear the stale request from scratchpad.
                fp.GetControllerAssignedData().SetScratchPadString("");
                this->LogDebugMessage(callSign + " HP request overridden: " + oldHpName + " → " + std::string(sItemString) + ", scratchpad cleared", "HP");
            }
        }
    }

    this->PushToOtherControllers(fp);
}

/// @brief Sets the LINEUP ground state via a momentary scratch-pad toggle.
/// @param fp Currently selected flight plan.
void CFlowX_Functions::Func_LineUp(EuroScopePlugIn::CFlightPlan& fp)
{
    std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
    fp.GetControllerAssignedData().SetScratchPadString("LINEUP");
    fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());

    if (this->GetAutoScratchpadClear() && !scratchBackup.empty())
    {
        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);
        auto        aptIt        = this->airports.find(dep);
        std::string scratchUpper = scratchBackup;
        to_upper(scratchUpper);
        bool excluded = aptIt != this->airports.end() && std::ranges::any_of(aptIt->second.scratchpadClearExclusions, [&](const std::string& prefix)
                                                                             {
            std::string prefixUpper = prefix;
            to_upper(prefixUpper);
            return scratchUpper.starts_with(prefixUpper); });
        if (!excluded)
        {
            fp.GetControllerAssignedData().SetScratchPadString("");
        }
    }
}

/// @brief Handles a missed approach: immediately promotes the aircraft to go-around state in the TTT list, clears cleared-to-land, takes tracking, assigns 5000 ft, highlights in TopSky, and sets MISAP scratch.
/// @param fp Currently selected flight plan.
/// @param radarScreenInstance Active radar screen used to call the TopSky tag function.
void CFlowX_Functions::Func_MissedApp(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance)
{
    std::string callSign = fp.GetCallsign();
    this->ttt_clearedToLand.erase(callSign);

    // Promote to go-around state immediately (controller-initiated).
    // If already detected: confirm it (suppress the 60-second auto-removal timeout).
    // If not yet detected (or frozen): set goAroundTick now so the display switches to red immediately.
    auto inboundIt = this->ttt_inbound.find(callSign);
    if (inboundIt != this->ttt_inbound.end())
    {
        TTTInboundState& st = inboundIt->second;
        if (st.goAroundTick == 0)
        {
            st.goAroundTick   = GetTickCount64();
            st.frozenTick     = 0;
            st.frozenTttStr   = {};
            st.wasTrackedByMe = true; // we are about to take tracking below
        }
        st.goAroundConfirmed = true;
    }

    if (!fp.GetTrackingControllerIsMe())
    {
        fp.StartTracking();
    }
    fp.GetControllerAssignedData().SetClearedAltitude(5000);

    radarScreenInstance->StartTagFunction(callSign.c_str(), nullptr, 0, "S-Highlight", TOPSKY_PLUGIN_NAME, 4, POINT(), RECT());
    std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
    fp.GetControllerAssignedData().SetScratchPadString((scratchBackup + "MISAP_").c_str());
}

/// @brief Sets ONFREQ, ST-UP, or PUSH ground state depending on the aircraft's current stand position.
/// @param fp Currently selected flight plan.
/// @param rt Correlated radar target.
void CFlowX_Functions::Func_OnFreq(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
    auto tag = this->GetPushStartHelperTag(fp, rt);
    if (tag.color == TAG_COLOR_RED)
    {
        return;
    }

    std::string dep = fp.GetFlightPlanData().GetOrigin();
    to_upper(dep);
    auto airport = this->airports.find(dep);

    // Are we ground or higher?
    if (this->ControllerMyself().GetFacility() >= 3)
    {
        EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();

        bool isTaxiOut = false;
        for (auto& taxiOut : airport->second.taxiOutStands)
        {
            if (CFlowX_Functions::PointInsidePolygon(static_cast<int>(taxiOut.second.lat.size()), taxiOut.second.lon.data(), taxiOut.second.lat.data(), position.m_Longitude, position.m_Latitude))
            {
                isTaxiOut = true;
                continue;
            }
        }

        if (isTaxiOut)
        {
            std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
            fp.GetControllerAssignedData().SetScratchPadString("ST-UP");
            fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
        }
        else
        {
            // We could give PUSH here, but it's better to visually check, so let's just "pop" it up using ONFREQ
            std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
            fp.GetControllerAssignedData().SetScratchPadString("ONFREQ");
            fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
        }
    }
    else
    {
        // We are delivery set ONFREQ
        std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
        fp.GetControllerAssignedData().SetScratchPadString("ONFREQ");
        fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
    }
}

/// @brief Opens a popup list of assignable holding points with each entry starred to denote a request.
/// @param fp Currently selected flight plan.
/// @param Pt Screen position at which to anchor the popup.
void CFlowX_Functions::Func_RequestHp(EuroScopePlugIn::CFlightPlan& fp, POINT Pt)
{
    RECT area;
    area.left   = Pt.x;
    area.right  = Pt.x + 100;
    area.top    = Pt.y;
    area.bottom = Pt.y + 100;
    this->OpenPopupList(area, "Request HP", 1);

    std::string dep = fp.GetFlightPlanData().GetOrigin();
    to_upper(dep);
    std::string rwy     = fp.GetFlightPlanData().GetDepartureRwy();
    auto        airport = this->airports.find(dep);
    auto        rwyIt   = airport->second.runways.find(rwy);
    if (rwyIt != airport->second.runways.end())
    {
        std::vector<const std::pair<const std::string, holdingPoint>*> sorted;
        for (auto& kv : rwyIt->second.holdingPoints)
            if (kv.second.assignable)
                sorted.push_back(&kv);
        std::ranges::sort(sorted, {}, [](auto* kv)
                          { return kv->second.order; });
        for (auto* kv : sorted)
            this->AddPopupListElement((kv->first + "*").c_str(), "", TAG_FUNC_HP_LISTSELECT);
    }
}

/// @brief Reverts the ground state from LINEUP back to TAXI via a momentary scratch-pad toggle.
/// @param fp Currently selected flight plan.
void CFlowX_Functions::Func_RevertToTaxi(EuroScopePlugIn::CFlightPlan& fp)
{
    std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
    fp.GetControllerAssignedData().SetScratchPadString("TAXI");
    fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
}

/// @brief Triggers the Ground Radar plugin's automatic stand assignment function.
/// @param fp Currently selected flight plan.
/// @param radarScreenInstance Active radar screen used to call the Ground Radar tag function.
void CFlowX_Functions::Func_StandAuto(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance)
{
    std::string callSign = fp.GetCallsign();
    radarScreenInstance->StartTagFunction(callSign.c_str(), "GRplugin", 0, "   Auto   ", GROUNDRADAR_PLUGIN_NAME, 2, POINT(), RECT());
}

/// @brief Sets the DEPA ground state and starts tracking the flight plan.
/// @param fp Currently selected flight plan.
void CFlowX_Functions::Func_TakeOff(EuroScopePlugIn::CFlightPlan& fp)
{
    std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
    fp.GetControllerAssignedData().SetScratchPadString("DEPA");
    fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());

    if (this->GetAutoScratchpadClear() && !scratchBackup.empty())
    {
        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);
        auto        aptIt        = this->airports.find(dep);
        std::string scratchUpper = scratchBackup;
        to_upper(scratchUpper);
        bool excluded = aptIt != this->airports.end() && std::ranges::any_of(aptIt->second.scratchpadClearExclusions, [&](const std::string& prefix)
                                                                             {
            std::string prefixUpper = prefix;
            to_upper(prefixUpper);
            return scratchUpper.starts_with(prefixUpper); });
        if (!excluded)
        {
            fp.GetControllerAssignedData().SetScratchPadString("");
        }
    }

    fp.StartTracking();
}

/// @brief Initiates a handoff to the next controller and marks the flight strip as transferred.
/// @param fp Currently selected flight plan.
/// @note Tower controllers search for a SID-specific approach frequency first; falls back to first APP, then drops tracking.
void CFlowX_Functions::Func_TransferNext(EuroScopePlugIn::CFlightPlan& fp)
{
    std::string callSign = fp.GetCallsign();
    std::string dep      = fp.GetFlightPlanData().GetOrigin();
    to_upper(dep);

    // VFR and VFR-to-IFR traffic stays with tower — mark transferred to UNICOM and drop tracking
    {
        std::string planType = fp.GetFlightPlanData().GetPlanType();
        if (planType == "V" || planType == "Z")
        {
            auto& anno = this->flightStripAnnotation[callSign];
            anno.resize(std::max(anno.length(), static_cast<size_t>(7)), ' ');
            constexpr std::string_view unicom = "122800";
            for (int i = 0; i < 6; i++)
            {
                anno[1 + i] = unicom[i];
            }
            fp.GetControllerAssignedData().SetFlightStripAnnotation(8, anno.c_str());
            this->PushToOtherControllers(fp);
            if (fp.GetTrackingControllerIsMe())
            {
                fp.EndTracking();
            }
            return;
        }
    }

    // Determine the best station to hand off to, in priority order:
    //   1. Approach station: iterate appFreqFallbacks for the target frequency in order;
    //      for each frequency collect all online stations, sort callsigns ASC, pick first.
    //   2. Centre station: iterate ctrStations frequencies in order, same pick logic.
    //   3. EuroScope's coordinated next controller (fallback)
    //   4. End tracking
    // Returns the full EuroScope callsign of the winning station (selected by frequency lookup,
    // e.g. "LOWW_M_APP"), or an empty string if no station was found (cases 3 and 4 are handled by the caller).
    // Returns {callsign, frequency} of the winning station, or {"", ""} if none found.
    auto findStation = [&]() -> std::pair<std::string, std::string>
    {
        if (this->radarScreen == nullptr)
        {
            return {"", ""};
        }

        std::string                        targetFreq;
        decltype(this->airports)::iterator airport = this->airports.end();

        // Check for go-around first — arrival airport may differ from departure airport
        auto planIt = this->ttt_inbound.find(callSign);
        if (planIt != this->ttt_inbound.end() && planIt->second.goAroundTick != 0)
        {
            targetFreq = planIt->second.flightPlan.goAroundFreq;
            // Identify the arrival airport by matching runway designator and go-around frequency
            for (auto it = this->airports.begin(); it != this->airports.end(); ++it)
            {
                auto rwyIt = it->second.runways.find(planIt->second.flightPlan.designator);
                if (rwyIt != it->second.runways.end() && rwyIt->second.goAroundFreq == targetFreq)
                {
                    airport = it;
                    break;
                }
            }
            if (airport == this->airports.end())
            {
                return {"", ""};
            }
        }
        else
        {
            // Not a go-around: look up the departure airport and determine freq from SID
            airport = this->airports.find(dep);
            if (airport == this->airports.end())
            {
                return {"", ""};
            }
            std::string sid = fp.GetFlightPlanData().GetSidName();
            targetFreq      = airport->second.defaultAppFreq;
            for (auto& [f, sids] : airport->second.sidAppFreqs)
            {
                if (std::ranges::find(sids, sid) != sids.end())
                {
                    targetFreq = f;
                    break;
                }
            }
        }

        this->LogDebugMessage(callSign + " TransferNext: target=" + targetFreq, "Transfer");

        // Try approach frequencies in config-defined fallback order; sort online stations ASC, pick first
        {
            auto        fallbackIt = airport->second.appFreqFallbacks.find(targetFreq);
            const auto& freqsToTry = (fallbackIt != airport->second.appFreqFallbacks.end())
                                         ? fallbackIt->second
                                         : std::vector<std::string>{targetFreq};
            for (const auto& freq : freqsToTry)
            {
                std::vector<std::string> matches;
                for (const auto& online : this->radarScreen->approachStations)
                {
                    if (online.second == freq)
                    {
                        matches.push_back(online.first);
                    }
                }
                if (!matches.empty())
                {
                    std::sort(matches.begin(), matches.end());
                    this->LogDebugMessage(callSign + " TransferNext: " + targetFreq + " -> " + freq + " (" + matches.front() + ")", "Transfer");
                    return {matches.front(), freq};
                }
            }
        }

        // No approach station found: try centre frequencies in config-defined priority order
        for (const auto& ctrFreq : airport->second.ctrStations)
        {
            std::vector<std::string> matches;
            for (const auto& online : this->radarScreen->centerStations)
            {
                if (online.second == ctrFreq)
                {
                    matches.push_back(online.first);
                }
            }
            if (!matches.empty())
            {
                std::sort(matches.begin(), matches.end());
                this->LogDebugMessage(callSign + " TransferNext: " + targetFreq + " -> CTR " + ctrFreq + " (" + matches.front() + ")", "Transfer");
                return {matches.front(), ctrFreq};
            }
        }

        this->LogDebugMessage(callSign + " TransferNext: no station found for target=" + targetFreq, "Transfer");
        return {"", ""};
    };

    auto [station, stationFreq] = findStation();
    if (stationFreq.empty())
    {
        // No controller online — use UNICOM so the annotation is non-blank and the
        // tower tag's "transferred" check suppresses further orange/blue blinking.
        stationFreq = "122.800";
    }

    // Snapshot spacing data at the moment of transfer-of-communication
    this->RecordDepartureSpacingSnapshot(callSign);

    // Mark FP as transferred — store target frequency (dot removed, 3 dp normalised) at [1..6]
    std::string transferFreq = freqToAnnotation(stationFreq);
    auto&       anno         = this->flightStripAnnotation[callSign];
    anno.resize(std::max(anno.length(), static_cast<size_t>(7)), ' ');
    for (int i = 0; i < 6; i++)
    {
        anno[1 + i] = transferFreq[i];
    }
    fp.GetControllerAssignedData().SetFlightStripAnnotation(8, anno.c_str());
    this->PushToOtherControllers(fp);

    // Transfer is marked, to we also need to transfer the tag?
    if (!fp.GetTrackingControllerIsMe())
    {
        // Not tracking, so no transfer needed
        return;
    }

    if (!station.empty() && fp.InitiateHandoff(station.c_str()))
    {
        // Custom station handoff succeeded
    }
    else
    {
        // Fall back to EuroScope's coordinated next controller if available
        std::string targetController = fp.GetCoordinatedNextController();
        if (!targetController.empty() && fp.InitiateHandoff(targetController.c_str()))
        {
            // EuroScope fallback handoff succeeded
        }
        else
        {
            fp.EndTracking();
        }
    }
}

/// @brief Marks the flight strip annotation with the GND frequency, drops tracking, and removes the GND transfer square.
void CFlowX_Functions::Func_GndTransfer(const std::string& callSign)
{
    EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelect(callSign.c_str());
    if (!fp.IsValid())
    {
        return;
    }

    std::string arr       = fp.GetFlightPlanData().GetDestination();
    auto        airportIt = this->FindMyAirport(arr);
    if (airportIt == this->airports.end())
    {
        return;
    }

    // Resolve GND freq: check geo zones first (requires valid position), fall back to default
    std::string                   gndFreq = airportIt->second.gndFreq;
    EuroScopePlugIn::CRadarTarget rt      = this->RadarTargetSelect(callSign.c_str());
    if (rt.IsValid() && rt.GetPosition().IsValid())
    {
        EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();
        for (auto& [name, zone] : airportIt->second.geoGndFreq)
        {
            if (PointInsidePolygon(static_cast<int>(zone.lat.size()), zone.lon.data(), zone.lat.data(), position.m_Longitude, position.m_Latitude))
            {
                gndFreq = zone.freq;
                break;
            }
        }
    }

    // Write freq (dot removed, 3 dp normalised) to annotation slot 8 at positions [1..6]
    std::string transferFreq = freqToAnnotation(gndFreq);
    auto&       anno         = this->flightStripAnnotation[callSign];
    anno.resize(std::max(anno.length(), static_cast<size_t>(7)), ' ');
    for (int i = 0; i < 6; i++)
    {
        anno[1 + i] = transferFreq[i];
    }
    fp.GetControllerAssignedData().SetFlightStripAnnotation(8, anno.c_str());
    this->PushToOtherControllers(fp);

    // Drop tracking — GND controllers don't accept tag transfers
    if (fp.GetTrackingControllerIsMe())
    {
        fp.EndTracking();
    }

    // Remove from GND transfer tracking state
    this->gndTransfer_list.erase(callSign);
    this->gndTransfer_soundPlayed.erase(callSign);
    if (this->radarScreen)
    {
        this->radarScreen->gndTransferSquares.erase(callSign);
    }
    if (this->radarScreen)
    {
        this->radarScreen->gndTransferSquareTimes.erase(callSign);
    }
}

/// @brief Re-evaluates and re-sets the EuroScope clearance flag for all ground-based cleared aircraft,
///        then queues a throttled ground-status re-push for each cleared aircraft.
void CFlowX_Functions::RedoFlags()
{
    this->redoFlagQueue.clear();

    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid() || pos.GetReportedGS() > 40)
        {
            continue;
        }

        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe()))
        {
            continue;
        }

        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);
        std::string arr = fp.GetFlightPlanData().GetDestination();
        to_upper(arr);
        std::string cs = fp.GetCallsign();

        if (dep.empty() || arr.empty())
        {
            continue;
        }

        auto airport = this->airports.find(dep);
        if (airport == this->airports.end())
        {
            continue;
        }

        if (pos.GetPressureAltitude() >= airport->second.fieldElevation + 50)
        {
            continue;
        }

        if (!fp.GetClearenceFlag())
        {
            continue;
        }

        // Clearance flag pair: execute immediately (unchanged behaviour)
        if (this->radarScreen != nullptr)
        {
            this->radarScreen->StartTagFunction(cs.c_str(), nullptr, 0, cs.c_str(), nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
            this->radarScreen->StartTagFunction(cs.c_str(), nullptr, 0, cs.c_str(), nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
        }

        // Queue a ground-status re-push for throttled background dispatch
        auto it = this->groundStatus.find(cs);
        if (it != this->groundStatus.end() && !it->second.empty())
        {
            this->redoFlagQueue.push_back({cs, it->second});
        }
    }
}

/// @brief Drains one task from the redo-flag queue; called from OnTimer() at a 1 s cadence.
void CFlowX_Functions::DrainRedoFlagQueue()
{
    if (this->redoFlagQueue.empty())
    {
        return;
    }

    const RedoFlagTask task = this->redoFlagQueue.front();
    this->redoFlagQueue.erase(this->redoFlagQueue.begin());

    EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelect(task.callSign.c_str());
    if (fp.IsValid())
    {
        std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
        fp.GetControllerAssignedData().SetScratchPadString(task.groundState.c_str());
        fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
    }
}
