#include "pch.h"
#include "CDelHelX.h"

#include <fstream>
#include <iostream>

#include "helpers.h"
#include "date/tz.h"

CDelHelX::CDelHelX()
{
    this->groundOverride = false;
    this->towerOverride = false;
    this->noChecks = false;
}

/// @brief Processes `.delhelx` chat commands.
/// @param sCommandLine Full command line string.
/// @return True if the command was handled; false otherwise.
bool CDelHelX::OnCompileCommand(const char* sCommandLine)
{
    std::vector<std::string> args = split(sCommandLine);

    if (starts_with(args[0], ".delhelx"))
    {
        if (args.size() == 1)
        {
            std::ostringstream msg;
            msg << "Version " << PLUGIN_VERSION << " loaded. Available commands: gnd, twr, nocheck, reset, update, flash, redoflags, autorestore";

            this->LogMessage(msg.str(), "Init");

            return true;
        }

        if (args[1] == "debug") {
            if (this->debug) {
                this->LogMessage("Disabling debug mode", "Debug");
            }
            else {
                this->LogMessage("Enabling debug mode", "Debug");
            }

            this->debug = !this->debug;
            if (this->radarScreen != nullptr)
            {
                this->radarScreen->debug = this->debug;
            }

            this->SaveSettings();

            return true;
        }
        else if (args[1] == "update")
        {
            if (this->updateCheck)
            {
                this->LogMessage("Disabling update check", "Update");
            }
            else {
                this->LogMessage("Enabling update check", "Update");
            }

            this->updateCheck = !this->updateCheck;

            this->SaveSettings();

            return true;
        }
        else if (args[1] == "flash")
        {
            if (this->flashOnMessage)
            {
                this->LogMessage("No longer flashing on DelHelX message", "Config");
            }
            else {
                this->LogMessage("Flashing on DelHelX message", "Config");
            }

            this->flashOnMessage = !this->flashOnMessage;

            this->SaveSettings();

            return true;
        }
        else if (args[1] == "gnd")
        {
            if (this->groundOverride)
            {
                this->LogMessage("GND freq override OFF", "GND");
            }
            else {
                this->LogMessage("GND freq override ON", "GND");
            }

            this->groundOverride = !this->groundOverride;

            return true;
        }
        else if (args[1] == "twr")
        {
            if (this->towerOverride)
            {
                this->LogMessage("TWR freq override OFF", "TWR");
            }
            else {
                this->LogMessage("TWR freq override ON", "TWR");
            }

            this->towerOverride = !this->towerOverride;

            return true;
        }
        else if (args[1] == "nocheck")
        {
            if (this->noChecks)
            {
                this->LogMessage("Flight plan checks turned ON", "Checks");
            }
            else {
                this->LogMessage("Flight plan checks turned OFF, use only for testing!!!", "Checks");
            }

            this->noChecks = !this->noChecks;

            return true;
        }
        else if (args[1] == "reset")
        {
            this->LogMessage("Resetting DelHelX plugin to defaults", "Defaults");
            this->updateCheck = false;
            this->flashOnMessage = false;
            this->groundOverride = false;
            this->towerOverride = false;
            this->noChecks = false;
            this->autoRestore = false;

            this->SaveSettings();

            return true;
        }
        else if (args[1] == "redoflags")
        {
            this->LogMessage("Redoing clearance flags...", "Flags");
            this->RedoFlags();

            return true;
        }
        else if (args[1] == "autorestore")
        {
            this->autoRestore = !this->autoRestore;
            this->LogMessage(std::string("Auto-restore on reconnect: ") + (this->autoRestore ? "ON" : "OFF"), "AutoRestore");
            this->SaveSettings();

            return true;
        }
    }

    return false;
}

/// @brief Re-evaluates the EuroScope clearance flag for all on-ground cleared aircraft.
void CDelHelX::RedoFlags()
{
    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        // Skip if aircraft is not on the ground (currently using ground speed threshold)
        if (!pos.IsValid() || pos.GetReportedGS() > 40) {
            continue;
        }

        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        // Skip if aircraft is tracked (except for aircraft tracked by current controller)
        if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe())) {
            continue;
        }

        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);

        std::string arr = fp.GetFlightPlanData().GetDestination();
        to_upper(arr);

        std::string cs = fp.GetCallsign();

        // Skip aircraft without a valid flight plan (no departure/destination airport)
        if (dep.empty() || arr.empty()) {
            continue;
        }

        auto airport = this->airports.find(dep);
        if (airport == this->airports.end())
        {
            // Airport not in config
            continue;
        }

        int depElevation = airport->second.fieldElevation;
        if (pos.GetPressureAltitude() >= depElevation + 50) {
            continue;
        }

        if (fp.GetClearenceFlag() && this->radarScreen != nullptr)
        {
            // Toggle off and back on
            this->radarScreen->StartTagFunction(cs.c_str(), nullptr, 0, cs.c_str(), nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
            this->radarScreen->StartTagFunction(cs.c_str(), nullptr, 0, cs.c_str(), nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
        }
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
void CDelHelX::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
    if (!FlightPlan.IsValid()) { return; }

    tagInfo tag;
    if      (ItemCode == TAG_ITEM_PS_HELPER) { tag = this->GetPushStartHelperTag(FlightPlan, RadarTarget); }
    else if (ItemCode == TAG_ITEM_TAXIOUT)   { tag = this->GetTaxiOutTag(FlightPlan, RadarTarget); }
    else if (ItemCode == TAG_ITEM_NEWQNH)    { tag = this->GetNewQnhTag(FlightPlan); }
    else if (ItemCode == TAG_ITEM_SAMESID)   { tag = this->GetSameSidTag(FlightPlan); }
    else                                     { return; }  // all others displayed in custom windows only

    *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
    strcpy_s(sItemString, 16, tag.tag.c_str());
    *pRGB = tag.color;
}

/// @brief Dispatches tag function callbacks to the appropriate Func_* method.
/// @param FunctionId TAG_FUNC_* constant identifying the action.
/// @param sItemString Current text of the clicked tag cell.
/// @param Pt Screen position of the click.
/// @param Area Bounding rectangle of the tag cell.
void CDelHelX::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
    EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelectASEL();
    if (!fp.IsValid())
    {
        return;
    }

    std::string dep = fp.GetFlightPlanData().GetOrigin();
    to_upper(dep);

    static const std::vector<int> noDepartureAirportCheckRequired = { TAG_FUNC_CLRD_TO_LAND, TAG_FUNC_MISSED_APP, TAG_FUNC_STAND_AUTO };
    if (this->airports.find(dep) == this->airports.end()
        && std::find(noDepartureAirportCheckRequired.begin(), noDepartureAirportCheckRequired.end(), FunctionId) == noDepartureAirportCheckRequired.end())
    {
        return;
    }

    EuroScopePlugIn::CRadarTarget rt = fp.GetCorrelatedRadarTarget();

    if (FunctionId == TAG_FUNC_ON_FREQ)             { this->Func_OnFreq(fp, rt); }
    else if (FunctionId == TAG_FUNC_CLEAR_NEWQNH)   { this->Func_ClearNewQnh(fp); }
    else if (FunctionId == TAG_FUNC_ASSIGN_HP)      { this->Func_AssignHp(fp, Pt); }
    else if (FunctionId == TAG_FUNC_REQUEST_HP)     { this->Func_RequestHp(fp, Pt); }
    else if (FunctionId == TAG_FUNC_HP_LISTSELECT)  { this->Func_HpListselect(fp, sItemString); }
    else if (FunctionId == TAG_FUNC_LINE_UP)        { Func_LineUp(fp); }
    else if (FunctionId == TAG_FUNC_TAKE_OFF)       { Func_TakeOff(fp); }
    else if (FunctionId == TAG_FUNC_TRANSFER_NEXT)  { this->Func_TransferNext(fp); }
    else if (FunctionId == TAG_FUNC_CLRD_TO_LAND)   { Func_ClrdToLand(fp, this->radarScreen); }
    else if (FunctionId == TAG_FUNC_MISSED_APP)     { Func_MissedApp(fp, this->radarScreen); }
    else if (FunctionId == TAG_FUNC_STAND_AUTO)     { Func_StandAuto(fp, this->radarScreen); }
}

/// @brief Creates the RadarScreen and immediately applies any persisted window positions so the
///        first OnRefresh draw sees the correct locations rather than triggering auto-placement.
EuroScopePlugIn::CRadarScreen* CDelHelX::OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated)
{
    CDelHelX_Base::OnRadarScreenCreated(sDisplayName, NeedRadarContent, GeoReferenced, CanBeSaved, CanBeCreated);
    if (this->depRateWindowX     != -1) { this->radarScreen->depRateWindowPos     = { this->depRateWindowX,     this->depRateWindowY     }; }
    if (this->twrOutboundWindowX != -1) { this->radarScreen->twrOutboundWindowPos = { this->twrOutboundWindowX, this->twrOutboundWindowY }; }
    if (this->twrInboundWindowX  != -1) { this->radarScreen->twrInboundWindowPos  = { this->twrInboundWindowX,  this->twrInboundWindowY  }; }
    if (this->napWindowX         != -1) { this->radarScreen->napWindowPos          = { this->napWindowX,         this->napWindowY         }; }
    return this->radarScreen;
}

/// @brief Drives periodic updates: blinking, update check, NAP reminder, and state-map refreshes.
/// @param Counter EuroScope second counter.
/// @note State maps update every 2 s; tag cache and departure overlays refresh every second;
///       NAP check every 10 s; window positions saved every 5 s.
void CDelHelX::OnTimer(int Counter)
{
    this->blinking = !this->blinking;

    if (this->updateCheck && this->latestVersion.valid() && this->latestVersion.wait_for(0ms) == std::future_status::ready) {
        this->CheckForUpdate();
    }

    if (Counter > 0 && Counter % 10 == 0)
    {
        this->CheckAirportNAPReminder();
    }

    if (Counter > 0 && Counter % 2 == 0)
    {
        this->UpdateTowerSameSID();
        this->AutoUpdateDepartureHoldingPoints();
        this->UpdateTTTInbounds();
        this->CheckReconnects();
    }

    // Rebuild tag cache and departure overlays every second (after state maps are current)
    if (Counter > 0)
    {
        this->UpdateTagCache();
        this->UpdateRadarTargetDepartureInfo();
    }

    if (Counter > 0 && Counter % 5 == 0)
    {
        this->SaveAndRestoreWindowLocations();
    }
}

/// @brief Removes the disconnecting aircraft from all departure and inbound state maps.
/// @param FlightPlan The disconnecting flight plan.
void CDelHelX::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
    std::string callSign = FlightPlan.GetCallsign();

    // Capture a snapshot for auto-restore if the pilot reconnects within 90 seconds
    if (this->autoRestore)
    {
        reconnectSnapshot snap;
        EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
        snap.pilotName    = FlightPlan.GetPilotName();
        snap.depAirport   = fpd.GetOrigin();   to_upper(snap.depAirport);
        snap.destAirport  = fpd.GetDestination(); to_upper(snap.destAirport);
        snap.aircraftType = fpd.GetAircraftFPType();
        snap.wtc          = fpd.GetAircraftWtc();
        snap.planType     = fpd.GetPlanType();
        snap.route        = fpd.GetRoute();
        snap.sidName      = fpd.GetSidName();
        snap.squawk       = FlightPlan.GetControllerAssignedData().GetSquawk();
        snap.clearanceFlag = FlightPlan.GetClearenceFlag();
        snap.disconnectTime = GetTickCount64();

        auto gsIt = this->groundStatus.find(callSign);
        if (gsIt != this->groundStatus.end())
        {
            snap.savedGroundStatus = gsIt->second;
        }

        EuroScopePlugIn::CRadarTarget rt = FlightPlan.GetCorrelatedRadarTarget();
        if (rt.IsValid() && rt.GetPosition().IsValid())
        {
            auto pos = rt.GetPosition().GetPosition();
            snap.lat = pos.m_Latitude;
            snap.lon = pos.m_Longitude;
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

    for (auto it = this->ttt_flightPlans.begin(); it != this->ttt_flightPlans.end(); )
    {
        if (it->first.substr(0, callSign.size()) == callSign)
        {
            this->tttInbound.RemoveFpFromTheList(FlightPlan);
            this->ttt_goAround.erase(it->first);
            it = this->ttt_flightPlans.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = this->ttt_distanceToRunway.begin(); it != this->ttt_distanceToRunway.end(); )
    {
        if (it->first.substr(0, callSign.size()) == callSign)
        {
            it = this->ttt_distanceToRunway.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = this->ttt_recentlyRemoved.begin(); it != this->ttt_recentlyRemoved.end(); )
    {
        if (it->first.substr(0, callSign.size()) == callSign)
        {
            it = this->ttt_recentlyRemoved.erase(it);
        }
        else
        {
            ++it;
        }
    }

    this->dep_prevWtc.erase(callSign);
    this->dep_prevSid.erase(callSign);
    this->dep_prevTakeoffOffset.erase(callSign);
    this->dep_prevDistanceAtTakeoff.erase(callSign);
    this->dep_timeRequired.erase(callSign);
    this->dep_sequenceNumber.erase(callSign);
}

/// @brief Parses an incoming METAR for QNH changes and flags cleared ground aircraft at that airport.
/// @param sStation ICAO station identifier.
/// @param sFullMetar Full METAR string.
void CDelHelX::OnNewMetarReceived(const char* sStation, const char* sFullMetar)
{
    std::string station = sStation;
    to_upper(station);

    auto& storedMetar = this->lastMetar[station];
    if (storedMetar == sFullMetar) { return; }
    storedMetar = sFullMetar;

    this->LogDebugMessage("New METAR for station " + station + ": " + sFullMetar, "Metar");

    auto airport = this->airports.find(station);
    if (airport == this->airports.end())
    {
        // Station not in airport config, so ignore it
        return;
    }

    std::vector<std::string> metarElements = split(sFullMetar);
    for (std::string metarElement : metarElements)
    {
        static const std::regex qnh(R"(Q[0-9]{4})");
        static const std::regex alt(R"(A[0-9]{4})");

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

                    // Set flight strip annotation on aircraft on the ground at that airport
                    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
                        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();

                        // Skip aircraft is not on the ground
                        auto stationAp = this->airports.find(station);
                        int stationElevation = stationAp != this->airports.end() ? stationAp->second.fieldElevation : 0;
                        if (!pos.IsValid() || pos.GetPressureAltitude() >= stationElevation + 50) {
                            continue;
                        }

                        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
                        // Skip aircraft is tracked (except aircraft tracked by current controller)
                        if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe())) {
                            continue;
                        }

                        std::string callSign = fp.GetCallsign();
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
}

/// @brief Monitors scratch-pad and ground-state changes to keep stand assignment and ground-status maps current.
/// @param fp Updated flight plan.
/// @param dataType EuroScope constant indicating which assigned-data field changed.
void CDelHelX::OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan fp, int dataType)
{
    if (dataType == EuroScopePlugIn::CTR_DATA_TYPE_SCRATCH_PAD_STRING)
    {
        std::string callSign = fp.GetCallsign();
        std::string scratch = fp.GetControllerAssignedData().GetScratchPadString();

        //if (!scratch.empty())
        //{
        //  OutputDebugStringA(("[DelHelX] " + callSign + " scratch: " + scratch + "\n").c_str());
        //}
        
        // Check for stand assignment
        size_t pos = scratch.find("GRP/S/");
        if (pos != std::string::npos) {
            std::string stand = scratch.substr(pos + 6);
            
            this->standAssignment[callSign] = stand;
        }

        // Check for ground status
        static const std::vector<std::string> groundStatuses = { "PUSH", "ST-UP", "ONFREQ", "TAXI", "LINEUP", "DEPA" };
        for (const auto& status : groundStatuses)
        {
            if (scratch.find(status) != std::string::npos)
            {
                this->groundStatus[callSign] = status;
                break;
            }
        }
    }

    if (dataType == EuroScopePlugIn::CTR_DATA_TYPE_GROUND_STATE)
    {
        std::string callSign = fp.GetCallsign();
        std::string groundState = fp.GetGroundState();
        this->groundStatus[callSign] = groundState;
    }
}

/// Singleton plugin instance owned by the DLL.
static CDelHelX* pPlugin;

/// @brief DLL export called by EuroScope to load the plugin; creates the CDelHelX singleton.
/// @param ppPlugInInstance Output pointer that receives the newly created plugin instance.
void __declspec (dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    *ppPlugInInstance = pPlugin = new CDelHelX();
}

/// @brief DLL export called by EuroScope when the plugin is unloaded; deletes the singleton.
void __declspec (dllexport) EuroScopePlugInExit(void)
{
    delete pPlugin;
}