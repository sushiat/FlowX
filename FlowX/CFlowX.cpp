/**
 * @file CFlowX.cpp
 * @brief Top-level plugin implementation; dispatches EuroScope events and handles chat commands.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CFlowX.h"

#include <fstream>
#include <iostream>

#include "helpers.h"
#include "date/tz.h"

/// @brief Constructs the main plugin object and initialises override flags to false.
CFlowX::CFlowX()
{
}

/// @brief Called when the runway activity dialog is confirmed; refreshes active runway sets.
void CFlowX::OnAirportRunwayActivityChanged()
{
    this->RefreshActiveRunways();
}

/// @brief Processes `.flowx` chat commands.
/// @param sCommandLine Full command line string.
/// @return True if the command was handled; false otherwise.
bool CFlowX::OnCompileCommand(const char* sCommandLine)
{
    try
    {
        std::vector<std::string> args = split(sCommandLine);

        if (!args.empty() && starts_with(args[0], ".flowx"))
        {
            if (args.size() == 1)
            {
                std::ostringstream msg;
                msg << "Version " << PLUGIN_VERSION << " loaded. Available commands: debugstats";

                this->LogMessage(msg.str(), "Init");

                return true;
            }

            if (args[1] == "debugstats")
            {
                this->LogMessage(
                    std::format("posUpd={} (inbound={} outbound={}) | tagItem={} | timer={} | standLaunch={} standSkip={}",
                                this->dbg_positionCalls, this->dbg_positionInbound, this->dbg_positionOutbound,
                                this->dbg_tagItemCalls, this->dbg_timerTicks, this->dbg_standLaunches, this->dbg_standSkips),
                    "DebugStats");

                return true;
            }
        }
    }
    catch (const std::exception& e)
    {
        this->LogException("OnCompileCommand", e.what());
    }
    catch (...)
    {
        this->LogException("OnCompileCommand", "unknown exception");
    }

    return false;
}

/// @brief Monitors scratch-pad and ground-state changes to keep stand assignment and ground-status maps current.
/// @param fp Updated flight plan.
/// @param dataType EuroScope constant indicating which assigned-data field changed.
void CFlowX::OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan fp, int dataType)
{
    try
    {
        if (dataType == EuroScopePlugIn::CTR_DATA_TYPE_SCRATCH_PAD_STRING)
        {
            std::string callSign = fp.GetCallsign();
            std::string scratch  = fp.GetControllerAssignedData().GetScratchPadString();

            // if (!scratch.empty())
            //{
            //   OutputDebugStringA(("[FlowX] " + callSign + " scratch: " + scratch + "\n").c_str());
            // }

            // Check for stand assignment
            size_t pos = scratch.find("GRP/S/");
            if (pos != std::string::npos)
            {
                std::string stand = scratch.substr(pos + 6);

                this->standAssignment[callSign] = stand;
            }

            // Check for ground status
            static const std::vector<std::string> groundStatuses = {"PUSH", "ST-UP", "ONFREQ", "TAXI", "TXIN", "LINEUP", "DEPA", "PARK"};
            for (const auto& status : groundStatuses)
            {
                if (scratch.contains(status))
                {
                    this->groundStatus[callSign] = status;
                    if (status == "TAXI" && this->radarScreen)
                        this->radarScreen->pushTracked.erase(callSign);
                    break;
                }
            }

            // HP shortcut (TWR only): .NAME → assign confirmed and clear pad; .NAME? → register request, leave pad.
            auto me = this->ControllerMyself();
            if (this->GetHpAutoScratch() && me.IsValid() && me.GetFacility() == 4 && scratch.size() >= 2 && scratch[0] == '.')
            {
                std::string hpInput   = scratch.substr(1);
                bool        isReqMark = (!hpInput.empty() && hpInput.back() == '?');
                if (isReqMark)
                    hpInput.pop_back();

                // Reject inputs with spaces (e.g. ".a4 ok") to prevent infinite re-entry.
                if (!hpInput.empty() && hpInput.find(' ') == std::string::npos)
                {
                    std::string hpUpper = hpInput;
                    to_upper(hpUpper);

                    // Find canonical HP name (case-insensitive search across all runways).
                    std::string foundHpName;
                    if (!this->GetAirports().empty())
                    {
                        const auto& ap = this->GetAirports().begin()->second;
                        for (const auto& [rwyDes, rwy] : ap.runways)
                        {
                            auto it = rwy.holdingPoints.find(hpUpper);
                            if (it != rwy.holdingPoints.end())
                            {
                                foundHpName = it->first;
                                break;
                            }
                        }
                    }

                    if (!foundHpName.empty())
                    {
                        const std::string newHp = foundHpName + (isReqMark ? "*" : "");
                        this->flightStripAnnotation[callSign] =
                            AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], newHp);
                        fp.GetControllerAssignedData().SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
                        if (!isReqMark)
                        {
                            fp.GetControllerAssignedData().SetScratchPadString(("." + foundHpName + ".").c_str()); // confirmed: mark as processed (trailing dot prevents re-trigger)
                            this->LogDebugMessage(callSign + " HP assigned via scratchpad: " + foundHpName, "HP");
                        }
                        else
                        {
                            this->LogDebugMessage(callSign + " HP request registered via scratchpad: " + foundHpName, "HP");
                        }
                        this->PushToOtherControllers(fp);
                    }
                }
            }
        }

        if (dataType == EuroScopePlugIn::CTR_DATA_TYPE_GROUND_STATE)
        {
            std::string callSign         = fp.GetCallsign();
            std::string groundState      = fp.GetGroundState();
            this->groundStatus[callSign] = groundState;
            if (groundState == "TAXI" && this->radarScreen)
                this->radarScreen->pushTracked.erase(callSign);
        }
    }
    catch (const std::exception& e)
    {
        this->LogException("OnFlightPlanControllerAssignedDataUpdate", e.what());
    }
    catch (...)
    {
        this->LogException("OnFlightPlanControllerAssignedDataUpdate", "unknown exception");
    }
}

/// @brief Removes the disconnecting aircraft from all departure and inbound state maps.
/// @param FlightPlan The disconnecting flight plan.
void CFlowX::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
    try
    {
        std::string callSign = FlightPlan.GetCallsign();
        std::string depRwy   = FlightPlan.GetFlightPlanData().GetDepartureRwy();

        // Capture a snapshot for auto-restore if the pilot reconnects within 90 seconds
        if (this->autoRestore)
        {
            reconnectSnapshot                snap;
            EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
            snap.pilotName                       = FlightPlan.GetPilotName();
            snap.depAirport                      = fpd.GetOrigin();
            to_upper(snap.depAirport);
            snap.destAirport = fpd.GetDestination();
            to_upper(snap.destAirport);
            snap.aircraftType   = fpd.GetAircraftFPType();
            snap.wtc            = fpd.GetAircraftWtc();
            snap.planType       = fpd.GetPlanType();
            snap.route          = fpd.GetRoute();
            snap.sidName        = fpd.GetSidName();
            snap.squawk         = FlightPlan.GetControllerAssignedData().GetSquawk();
            snap.clearanceFlag  = FlightPlan.GetClearenceFlag();
            snap.disconnectTime = GetTickCount64();

            auto gsIt = this->groundStatus.find(callSign);
            if (gsIt != this->groundStatus.end())
            {
                snap.savedGroundStatus = gsIt->second;
            }

            EuroScopePlugIn::CRadarTarget rt = FlightPlan.GetCorrelatedRadarTarget();
            if (rt.IsValid() && rt.GetPosition().IsValid())
            {
                auto pos         = rt.GetPosition().GetPosition();
                snap.lat         = pos.m_Latitude;
                snap.lon         = pos.m_Longitude;
                snap.hasPosition = true;
            }

            this->reconnect_pending[callSign] = snap;
            // groundStatus is kept alive and cleaned up by CheckReconnects after 90 s or on successful match
        }
        if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
        {
            this->twrSameSID.RemoveFpFromTheList(FlightPlan);
            this->twrSameSID_flightPlans.erase(callSign);
        }

        {
            auto inboundIt = this->ttt_inbound.find(callSign);
            if (inboundIt != this->ttt_inbound.end())
            {
                this->LogDebugMessage(callSign + " removed from TTT (disconnect) rwy=" + inboundIt->second.flightPlan.designator, "TTT");
                this->tttInbound.RemoveFpFromTheList(FlightPlan);
                this->ttt_inbound.erase(inboundIt);
            }
        }
        this->ttt_callSigns.erase(callSign);
        this->ttt_clearedToLand.erase(callSign);

        for (auto it = this->ttt_recentlyRemoved.begin(); it != this->ttt_recentlyRemoved.end();)
        {
            it = (it->first.rfind(callSign, 0) == 0) ? this->ttt_recentlyRemoved.erase(it) : ++it;
        }

        this->dep_liveSpacing.erase(callSign);
        this->dep_sequenceNumber.erase(callSign);
        this->RemoveFromDepartureQueue(callSign, depRwy);

        this->gndTransfer_list.erase(callSign);
        this->gndTransfer_soundPlayed.erase(callSign);
        this->readyTakeoff_wasWaiting.erase(callSign);
        this->readyTakeoff_okTick.erase(callSign);
        this->readyTakeoff_soundPlayed.erase(callSign);
        if (this->radarScreen)
            this->radarScreen->gndTransferSquares.erase(callSign);
        if (this->radarScreen)
            this->radarScreen->gndTransferSquareTimes.erase(callSign);
        if (this->radarScreen)
            this->radarScreen->pushTracked.erase(callSign);

        this->standAssignment.erase(callSign);
    }
    catch (const std::exception& e)
    {
        this->LogException("OnFlightPlanDisconnect", e.what());
    }
    catch (...)
    {
        this->LogException("OnFlightPlanDisconnect", "unknown exception");
    }
}

/// @brief Dispatches tag function callbacks to the appropriate Func_* method.
/// @param FunctionId TAG_FUNC_* constant identifying the action.
/// @param sItemString Current text of the clicked tag cell.
/// @param Pt Screen position of the click.
/// @param Area Bounding rectangle of the tag cell.
void CFlowX::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
    try
    {
        EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelectASEL();
        if (!fp.IsValid())
        {
            return;
        }

        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);

        static const std::vector<int> noDepartureAirportCheckRequired = {TAG_FUNC_CLRD_TO_LAND, TAG_FUNC_MISSED_APP, TAG_FUNC_STAND_AUTO};
        if (!this->airports.contains(dep) && std::ranges::find(noDepartureAirportCheckRequired, FunctionId) == noDepartureAirportCheckRequired.end())
        {
            return;
        }

        EuroScopePlugIn::CRadarTarget rt = fp.GetCorrelatedRadarTarget();

        if (FunctionId == TAG_FUNC_ON_FREQ)
        {
            this->Func_OnFreq(fp, rt);
        }
        else if (FunctionId == TAG_FUNC_CLEAR_NEWQNH)
        {
            this->Func_ClearNewQnh(fp);
        }
        else if (FunctionId == TAG_FUNC_ASSIGN_HP)
        {
            this->Func_AssignHp(fp, Pt);
        }
        else if (FunctionId == TAG_FUNC_REQUEST_HP)
        {
            this->Func_RequestHp(fp, Pt);
        }
        else if (FunctionId == TAG_FUNC_HP_LISTSELECT)
        {
            this->Func_HpListselect(fp, sItemString);
        }
        else if (FunctionId == TAG_FUNC_ASSIGN_QUEUE_POS)
        {
            this->Func_AssignQueuePos(fp, Pt);
        }
        else if (FunctionId == TAG_FUNC_QUEUE_POS_LISTSELECT)
        {
            this->Func_QueuePosListselect(fp, sItemString);
        }
        else if (FunctionId == TAG_FUNC_APPEND_QUEUE_POS)
        {
            this->Func_AppendQueuePos(fp);
        }
        else if (FunctionId == TAG_FUNC_LINE_UP)
        {
            Func_LineUp(fp);
            this->RemoveFromDepartureQueue(fp.GetCallsign(), fp.GetFlightPlanData().GetDepartureRwy());
        }
        else if (FunctionId == TAG_FUNC_REVERT_TO_TAXI)
        {
            Func_RevertToTaxi(fp);
        }
        else if (FunctionId == TAG_FUNC_TAKE_OFF)
        {
            Func_TakeOff(fp);
            this->RemoveFromDepartureQueue(fp.GetCallsign(), fp.GetFlightPlanData().GetDepartureRwy());
            // Clear taxi route on takeoff clearance.
            if (this->radarScreen)
            {
                const std::string cs = fp.GetCallsign();
                this->radarScreen->taxiTracked.erase(cs);
                this->radarScreen->taxiAssigned.erase(cs);
                this->radarScreen->taxiAssignedTimes.erase(cs);
                this->radarScreen->taxiAssignedPos.erase(cs);
                this->radarScreen->taxiSuggested.erase(cs);
            }
        }
        else if (FunctionId == TAG_FUNC_TRANSFER_NEXT)
        {
            this->Func_TransferNext(fp);
        }
        else if (FunctionId == TAG_FUNC_CLRD_TO_LAND)
        {
            Func_ClrdToLand(fp, this->radarScreen);
        }
        else if (FunctionId == TAG_FUNC_MISSED_APP)
        {
            Func_MissedApp(fp, this->radarScreen);
        }
        else if (FunctionId == TAG_FUNC_STAND_AUTO)
        {
            Func_StandAuto(fp, this->radarScreen);
        }
    }
    catch (const std::exception& e)
    {
        this->LogException("OnFunctionCall", e.what());
    }
    catch (...)
    {
        this->LogException("OnFunctionCall", "unknown exception");
    }
}

/// @brief Dispatches tag item rendering to the appropriate Get*Tag method.
/// @param FlightPlan Flight plan for the tag row.
/// @param RadarTarget Correlated radar target.
/// @param ItemCode TAG_ITEM_* constant identifying the column.
/// @param TagData Additional EuroScope tag data (unused).
/// @param sItemString Output buffer for the tag text (max 15 chars + NUL).
/// @param pColorCode Output for the EuroScope colour mode flag.
/// @param pRGB Output for the RGB colour value.
/// @param pFontSize Output for an optional font size override.
void CFlowX::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
    try
    {
        this->dbg_tagItemCalls++;

        if (!FlightPlan.IsValid())
        {
            return;
        }

        tagInfo tag;
        if (ItemCode == TAG_ITEM_PS_HELPER)
        {
            tag = this->GetPushStartHelperTag(FlightPlan, RadarTarget);
        }
        else if (ItemCode == TAG_ITEM_TAXIOUT)
        {
            tag = this->GetTaxiOutTag(FlightPlan, RadarTarget);
        }
        else if (ItemCode == TAG_ITEM_NEWQNH)
        {
            tag = this->GetNewQnhTag(FlightPlan);
        }
        else if (ItemCode == TAG_ITEM_SAMESID)
        {
            tag = this->GetSameSidTag(FlightPlan);
        }
        else if (ItemCode == TAG_ITEM_ADES)
        {
            tag = this->GetAdesTag(FlightPlan);
        }
        else
        {
            return;
        } // all others displayed in custom windows only

        strcpy_s(sItemString, 16, tag.tag.c_str());
        if (tag.color == TAG_COLOR_DEFAULT_NONE)
        {
            *pColorCode = EuroScopePlugIn::TAG_COLOR_DEFAULT;
        }
        else
        {
            *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
            *pRGB       = tag.color;
        }
    }
    catch (const std::exception& e)
    {
        this->LogException("OnGetTagItem", e.what());
    }
    catch (...)
    {
        this->LogException("OnGetTagItem", "unknown exception");
    }
}

/// @brief Parses an incoming METAR for QNH changes and flags cleared ground aircraft at that airport.
/// @param sStation ICAO station identifier.
/// @param sFullMetar Full METAR string.
void CFlowX::OnNewMetarReceived(const char* sStation, const char* sFullMetar)
{
    try
    {
        std::string station = sStation;
        to_upper(station);

        auto& storedMetar = this->lastMetar[station];
        if (storedMetar == sFullMetar)
        {
            return;
        }
        storedMetar = sFullMetar;

        this->LogDebugMessage("New METAR for station " + station + ": " + sFullMetar, "Metar");

        auto airport = this->airports.find(station);
        if (airport == this->airports.end())
        {
            // Station not in airport config, so ignore it
            return;
        }

        std::vector<std::string> metarElements = split(sFullMetar);
        std::vector<std::string> rvrTokens;
        bool                     inForecastGroup = false;
        for (std::string metarElement : metarElements)
        {
            if (metarElement == "BECMG" || metarElement == "TEMPO" || metarElement == "NOSIG")
                inForecastGroup = true;

            static const std::regex windRx(R"((?:[0-9]{3}|VRB)[0-9]{2}(?:G[0-9]{2,3})?(?:KT|MPS))");
            static const std::regex qnh(R"(Q[0-9]{4})");
            static const std::regex alt(R"(A[0-9]{4})");
            static const std::regex rvrRx(R"(R([0-9]{2}[LCR]?)\/([MP]?)([0-9]{4})(?:V([0-9]{4}))?([UDN])?(?:FT)?)");

            if (!inForecastGroup && std::regex_match(metarElement, windRx))
            {
                auto existingWind = this->airportWind.find(station);
                if (existingWind == this->airportWind.end())
                {
                    this->LogDebugMessage("First wind value for airport " + station + " is " + metarElement, "Metar");
                    this->airportWind.emplace(station, metarElement);
                }
                else if (existingWind->second != metarElement)
                {
                    this->LogDebugMessage("New wind value for airport " + station + " is " + metarElement, "Metar");
                    this->airportWind[station] = metarElement;
                    this->windUnacked.insert(station);
                }
            }

            std::smatch rvrMatch;
            if (std::regex_match(metarElement, rvrMatch, rvrRx))
            {
                std::string rwy      = rvrMatch[1].str();
                std::string modifier = rvrMatch[2].str();
                int         value    = std::stoi(rvrMatch[3].str());
                std::string varUpper = rvrMatch[4].str();
                std::string trend    = rvrMatch[5].str();

                std::string token = std::format("R{}/{}{}", rwy, modifier, value);
                if (!varUpper.empty())
                {
                    token += std::format("V{}", std::stoi(varUpper));
                }
                if (!trend.empty())
                {
                    token += trend;
                }

                rvrTokens.push_back(token);
            }

            if (std::regex_match(metarElement, qnh) || std::regex_match(metarElement, alt))
            {
                // Check if existing QNH and if that is now different
                auto existingQNH = this->airportQNH.find(station);
                if (existingQNH == this->airportQNH.end())
                {
                    this->LogDebugMessage("First QNH value for airport " + station + " is " + metarElement, "Metar");

                    // No existing QNH, add it
                    this->airportQNH.emplace(station, metarElement);
                }
                else
                {
                    if (existingQNH->second != metarElement)
                    {
                        this->LogDebugMessage("New QNH value for airport " + station + " is " + metarElement, "Metar");

                        // Save new QNH
                        this->airportQNH[station] = metarElement;
                        this->qnhUnacked.insert(station);

                        // Set flight strip annotation on aircraft on the ground at that airport
                        for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
                        {
                            EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();

                            // Skip aircraft is not on the ground
                            auto stationAp        = this->airports.find(station);
                            int  stationElevation = stationAp != this->airports.end() ? stationAp->second.fieldElevation : 0;
                            if (!pos.IsValid() || pos.GetPressureAltitude() >= stationElevation + 50)
                            {
                                continue;
                            }

                            EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
                            // Skip aircraft is tracked (except aircraft tracked by current controller)
                            if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe()))
                            {
                                continue;
                            }

                            std::string callSign = fp.GetCallsign();

                            if (this->ControllerMyself().GetFacility() == 4)
                            {
                                auto        gsIt = this->groundStatus.find(callSign);
                                std::string gs   = (gsIt != this->groundStatus.end()) ? gsIt->second : fp.GetGroundState();
                                if (gs != "TAXI" && gs != "LINEUP" && gs != "DEPA")
                                {
                                    continue;
                                }
                            }

                            std::string dep = fp.GetFlightPlanData().GetOrigin();
                            to_upper(dep);

                            if (dep == station && fp.GetClearenceFlag())
                            {
                                EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
                                if (this->flightStripAnnotation[callSign].empty())
                                {
                                    this->flightStripAnnotation[callSign].append("Q");
                                }
                                else
                                {
                                    this->flightStripAnnotation[callSign][0] = 'Q';
                                }
                                fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
                                this->PushToOtherControllers(fp);
                            }
                        }
                    }
                }
            }
        }

        // Sort RVR tokens by runway designator (numeric part), then join
        std::sort(rvrTokens.begin(), rvrTokens.end(), [](const std::string& a, const std::string& b)
                  {
        auto rwyNum = [](const std::string& s) -> int {
            size_t slash = s.find('/');
            return (slash != std::string::npos && slash > 1) ? std::stoi(s.substr(1, slash - 1)) : 0;
        };
        return rwyNum(a) < rwyNum(b); });
        std::string newRVR;
        for (const auto& t : rvrTokens)
        {
            if (!newRVR.empty())
            {
                newRVR += " ";
            }
            newRVR += t;
        }

        // Update RVR after processing all elements (may be empty if none present)
        auto existingRVR = this->airportRVR.find(station);
        if (existingRVR == this->airportRVR.end() || existingRVR->second != newRVR)
        {
            if (!newRVR.empty())
            {
                this->LogDebugMessage("New RVR for airport " + station + ": " + newRVR, "Metar");
            }
            this->airportRVR[station] = newRVR;
            if (!newRVR.empty())
            {
                this->rvrUnacked.insert(station);
            }
        }
    }
    catch (const std::exception& e)
    {
        this->LogException("OnNewMetarReceived", e.what());
    }
    catch (...)
    {
        this->LogException("OnNewMetarReceived", "unknown exception");
    }
}

/// @brief Creates the RadarScreen and immediately applies any persisted window positions so the
///        first OnRefresh draw sees the correct locations rather than triggering auto-placement.
EuroScopePlugIn::CRadarScreen* CFlowX::OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated)
{
    try
    {
        CFlowX_Base::OnRadarScreenCreated(sDisplayName, NeedRadarContent, GeoReferenced, CanBeSaved, CanBeCreated);
        if (this->depRateWindowX != -1)
        {
            this->radarScreen->depRateWindowPos = {this->depRateWindowX, this->depRateWindowY};
        }
        if (this->twrOutboundWindowX != -1)
        {
            this->radarScreen->twrOutboundWindowPos = {this->twrOutboundWindowX, this->twrOutboundWindowY};
        }
        if (this->twrInboundWindowX != -1)
        {
            this->radarScreen->twrInboundWindowPos = {this->twrInboundWindowX, this->twrInboundWindowY};
        }
        if (this->napWindowX != -1)
        {
            this->radarScreen->napWindowPos = {this->napWindowX, this->napWindowY};
        }
        if (this->weatherWindowX != -1)
        {
            this->radarScreen->weatherWindowPos = {this->weatherWindowX, this->weatherWindowY};
        }
        if (this->approachEstWindowX != -1)
        {
            this->radarScreen->approachEstWindowPos = {this->approachEstWindowX, this->approachEstWindowY};
            if (this->approachEstWindowW > 0)
            {
                this->radarScreen->approachEstWindowW = this->approachEstWindowW;
            }
            if (this->approachEstWindowH > 0)
            {
                this->radarScreen->approachEstWindowH = this->approachEstWindowH;
            }
        }
        return this->radarScreen;
    }
    catch (const std::exception& e)
    {
        this->LogException("OnRadarScreenCreated", e.what());
    }
    catch (...)
    {
        this->LogException("OnRadarScreenCreated", "unknown exception");
    }
    return this->radarScreen;
}

/// @brief Drives periodic updates: blinking, update check, NAP reminder, and state-map refreshes.
/// @param Counter EuroScope second counter.
/// @note State maps update every 2 s; tag cache and departure overlays refresh every second;
///       NAP check every 10 s; window positions saved every 5 s.
void CFlowX::OnTimer(int Counter)
{
    try
    {
        this->dbg_timerTicks++;
        this->blinking = !this->blinking;

        if (this->updateCheck && this->latestVersion.valid() && this->latestVersion.wait_for(0ms) == std::future_status::ready)
        {
            this->CheckForUpdate();
        }

        // Detect connection transitions and trigger RefreshActiveRunways 5 s after connect.
        {
            const int curType = this->GetConnectionType();
            if (curType != EuroScopePlugIn::CONNECTION_TYPE_NO &&
                this->connectedType == EuroScopePlugIn::CONNECTION_TYPE_NO)
            {
                // Just connected (or switched from no-connection to a live/playback session).
                this->connectedTickMs          = GetTickCount64();
                this->connectedRunwayRefreshed = false;
            }
            else if (curType == EuroScopePlugIn::CONNECTION_TYPE_NO)
            {
                // Disconnected — reset so next connect fires again.
                this->connectedRunwayRefreshed = false;
                this->connectedTickMs          = 0;
            }
            this->connectedType = curType;

            if (!this->connectedRunwayRefreshed && this->connectedTickMs > 0 &&
                GetTickCount64() - this->connectedTickMs >= 5000)
            {
                this->RefreshActiveRunways();
                this->connectedRunwayRefreshed = true;
            }
        }

        if (Counter > 0 && Counter % 10 == 0)
        {
            this->CheckAirportNAPReminder();
        }

        if (Counter > 0 && Counter % 2 == 0)
        {
            this->UpdateTWROutbound();
            this->UpdateTWRInbound();
            this->CheckReconnects();
            this->SyncQueueWithGroundState();
        }

        if (Counter > 0 && Counter % 4 == 0)
        {
            this->UpdateOccupiedStands();
            this->CheckArrivedAtStand();
        }

        // Rebuild tag cache and departure overlays every second (after state maps are current)
        if (Counter > 0)
        {
            this->UpdateTagCache();
            this->UpdateRadarTargetDepartureInfo();
            this->DrainRedoFlagQueue();
        }

        if (Counter > 0 && Counter % 5 == 0)
        {
            this->UpdateAdesCache();
        }

        this->PollAtisLetters(Counter);
        this->PollOsmFuture();
        this->PollGraphFuture();
    }
    catch (const std::exception& e)
    {
        this->LogException("OnTimer", e.what());
    }
    catch (...)
    {
        this->LogException("OnTimer", "unknown exception");
    }
}

/// Singleton plugin instance owned by the DLL.
static CFlowX* pPlugin;

/// @brief DLL export called by EuroScope to load the plugin; creates the CFlowX singleton.
/// @param ppPlugInInstance Output pointer that receives the newly created plugin instance.
void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    try
    {
        *ppPlugInInstance = pPlugin = new CFlowX();
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("EuroScopePlugInInit", e.what());
        throw;
    }
    catch (...)
    {
        WriteExceptionToLog("EuroScopePlugInInit", "unknown exception");
        throw;
    }
}

/// @brief DLL export called by EuroScope when the plugin is unloaded; deletes the singleton.
void __declspec(dllexport) EuroScopePlugInExit(void)
{
    try
    {
        delete pPlugin;
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("EuroScopePlugInExit", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("EuroScopePlugInExit", "unknown exception");
    }
}