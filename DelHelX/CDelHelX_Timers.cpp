#include "pch.h"
#include "CDelHelX_Timers.h"

#include <filesystem>
#include <set>
#include <sstream>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include "helpers.h"
#include "date/tz.h"

/// @brief Returns today's UTC date as a "YYYY-MM-DD" string, used for NAP dismissal tracking.
static std::string UtcDateString()
{
    auto now = std::chrono::system_clock::now();
    auto ymd = date::year_month_day{ date::floor<date::days>(now) };
    std::ostringstream ss;
    int  y = (int)ymd.year();
    auto m = (unsigned)ymd.month();
    auto d = (unsigned)ymd.day();
    ss << y << "-" << (m < 10 ? "0" : "") << m << "-" << (d < 10 ? "0" : "") << d;
    return ss.str();
}

/// @brief Detects which holding-point polygon a taxiing aircraft occupies and writes it to flight-strip slot 8.
void CDelHelX_Timers::AutoUpdateDepartureHoldingPoints()
{
    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
        std::string callSign = fp.GetCallsign();

        if (!pos.IsValid() || !fp.IsValid())
        {
            continue;
        }

        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);

        std::string arr = fp.GetFlightPlanData().GetDestination();
        to_upper(arr);

        // Skip aircraft without a valid flight plan (no departure/destination airport)
        if (dep.empty() || arr.empty())
        {
            continue;
        }

        auto airport = this->airports.find(dep);
        if (airport == this->airports.end())
        {
            continue;
        }

        EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
        std::string rwy = fpd.GetDepartureRwy();
        std::string groundState = fp.GetGroundState();
        auto pressAlt = pos.GetPressureAltitude();
        auto groundSpeed = pos.GetReportedGS();

        std::string before = this->flightStripAnnotation[callSign];
        int depElevation = airport->second.fieldElevation;
        if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < depElevation + 50 && groundSpeed < 30)
        {
            auto rwyIt = airport->second.runways.find(rwy);
            if (rwyIt != airport->second.runways.end())
            {
                for (auto& [hpName, hpData] : rwyIt->second.holdingPoints)
                {
                    u_int corners = static_cast<u_int>(hpData.lat.size());
                    double polyX[10], polyY[10];
                    std::copy(hpData.lon.begin(), hpData.lon.end(), polyX);
                    std::copy(hpData.lat.begin(), hpData.lat.end(), polyY);

                    if (CDelHelX_Timers::PointInsidePolygon(static_cast<int>(corners), polyX, polyY, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
                    {
                        this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], hpName);
                    }
                }
            }

            if (before != this->flightStripAnnotation[callSign])
            {
                fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
                this->PushToOtherControllers(fp);
            }
        }
    }
}

/// @brief Checks each airport's NAP reminder configuration and shows the custom reminder window when the time is reached.
/// The reminder is suppressed if it was already acknowledged today (UTC date comparison).
void CDelHelX_Timers::CheckAirportNAPReminder()
{
    for (auto& airport : this->airports)
    {
        if (airport.second.nap_reminder.enabled && !airport.second.nap_reminder.triggered)
        {
            try
            {
                std::ostringstream timeStream;
                timeStream << date::make_zoned(airport.second.nap_reminder.tzone, std::chrono::system_clock::now());
                std::string timeString = timeStream.str();

                std::vector<std::string> timeSplit = split(timeString, ' ');
                if (timeSplit.size() == 3)
                {
                    auto tod = timeSplit[1];
                    std::vector<std::string> todSplit = split(tod, ':');
                    if (todSplit.size() == 3)
                    {
                        int hours   = atoi(todSplit[0].c_str());
                        int minutes = atoi(todSplit[1].c_str());

                        if ((hours == airport.second.nap_reminder.hour && minutes >= airport.second.nap_reminder.minute) || hours > airport.second.nap_reminder.hour)
                        {
                            airport.second.nap_reminder.triggered = true;

                            // Suppress if already acknowledged today
                            if (this->napLastDismissedDate == UtcDateString()) { continue; }

                            std::filesystem::path wavPath = std::filesystem::path(GetPluginDirectory()) / "nap.wav";
                            PlaySoundA(wavPath.string().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
                            if (this->radarScreen != nullptr)
                            {
                                this->radarScreen->napReminderActive  = true;
                                this->radarScreen->napReminderAirport = airport.first;
                            }
                        }
                    }
                }
            }
            catch (std::exception& e)
            {
                this->LogMessage("Error processing NAP-reminder for airport " + airport.first + ". Error: " + std::string(e.what()), "Config");
            }
        }
    }
}

/// @brief Restores clearance flag and ground state for pilots who reconnect within 90 seconds with a matching flight plan.
void CDelHelX_Timers::CheckReconnects()
{
    if (!this->autoRestore || this->reconnect_pending.empty())
    {
        return;
    }

    constexpr ULONGLONG timeoutMs = 90000ULL;
    ULONGLONG now = GetTickCount64();

    // Expire snapshots older than 90 s and clean up their groundStatus entries
    for (auto it = this->reconnect_pending.begin(); it != this->reconnect_pending.end(); )
    {
        if ((now - it->second.disconnectTime) >= timeoutMs)
        {
            this->groundStatus.erase(it->first);
            it = this->reconnect_pending.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (this->reconnect_pending.empty())
    {
        return;
    }

    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid())
        {
            continue;
        }

        std::string callSign = fp.GetCallsign();
        auto pendingIt = this->reconnect_pending.find(callSign);
        if (pendingIt == this->reconnect_pending.end())
        {
            continue;
        }

        const reconnectSnapshot& snap = pendingIt->second;
        EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
        EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();

        std::string depAirport = fpd.GetOrigin();
        to_upper(depAirport);
        std::string destAirport = fpd.GetDestination();
        to_upper(destAirport);

        auto logMismatch = [&](const std::string& field, const std::string& got, const std::string& expected) {
            this->LogDebugMessage(callSign + " reconnect mismatch [" + field + "]: got=\"" + got + "\" expected=\"" + expected + "\"", "AutoRestore");
        };

        bool match = true;
        if (std::string(fp.GetPilotName())       != snap.pilotName)    { logMismatch("pilotName",    fp.GetPilotName(),        snap.pilotName);    match = false; }
        if (depAirport                               != snap.depAirport)   { logMismatch("depAirport",   depAirport,               snap.depAirport);   match = false; }
        if (destAirport                              != snap.destAirport)  { logMismatch("destAirport",  destAirport,              snap.destAirport);  match = false; }
        if (std::string(fpd.GetAircraftFPType()) != snap.aircraftType) { logMismatch("aircraftType", fpd.GetAircraftFPType(),  snap.aircraftType); match = false; }
        if (fpd.GetAircraftWtc()                     != snap.wtc)          { logMismatch("wtc",          std::string(1, fpd.GetAircraftWtc()), std::string(1, snap.wtc)); match = false; }
        if (std::string(fpd.GetPlanType())       != snap.planType)     { logMismatch("planType",     fpd.GetPlanType(),        snap.planType);     match = false; }
        {
            auto trimRoute = [](std::string s) -> std::string {
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
                return s;
            };
            if (trimRoute(std::string(fpd.GetRoute())) != trimRoute(snap.route)) { logMismatch("route", fpd.GetRoute(), snap.route); match = false; }
        }
        if (std::string(fpd.GetSidName())        != snap.sidName)      { logMismatch("sidName",      fpd.GetSidName(),         snap.sidName);      match = false; }
        if (std::string(fpcad.GetSquawk())       != snap.squawk)       { logMismatch("squawk",       fpcad.GetSquawk(),        snap.squawk); match = false; }

        if (match && snap.hasPosition)
        {
            EuroScopePlugIn::CPosition storedPos;
            storedPos.m_Latitude  = snap.lat;
            storedPos.m_Longitude = snap.lon;
            double dist = rt.GetPosition().GetPosition().DistanceTo(storedPos);
            if (dist > 1.0)
            {
                logMismatch("position", std::to_string(dist) + "nm", "<1nm");
                match = false;
            }
        }

        if (!match)
        {
            this->reconnect_pending.erase(pendingIt);
            continue;
        }

        // Restore clearance flag
        if (snap.clearanceFlag && !fp.GetClearenceFlag() && this->radarScreen != nullptr)
        {
            this->radarScreen->StartTagFunction(callSign.c_str(), nullptr, 0, callSign.c_str(), nullptr,
                EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
        }

        // Restore ground state via momentary scratch-pad toggle (same pattern as other state setters)
        if (!snap.savedGroundStatus.empty())
        {
            std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
            fp.GetControllerAssignedData().SetScratchPadString(snap.savedGroundStatus.c_str());
            fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
        }

        if (snap.clearanceFlag || !snap.savedGroundStatus.empty())
        {
            this->LogMessage("Auto-restored state for " + callSign + " (clearance=" +
                (snap.clearanceFlag ? "yes" : "no") + ", gnd=" + snap.savedGroundStatus + ")", "AutoRestore");
        }

        this->reconnect_pending.erase(pendingIt);
    }
}

/// @brief Called on every radar position update.
/// Phase 1 – takeoff roll: sets the roll tick in twrSameSID_flightPlans when the aircraft is on the
///   departure runway, GS ≥ 40 kt, and heading within 45° of the runway departure direction.
/// Phase 1b – rejected takeoff: resets the tick to 0 if GS drops below 30 kt before the aircraft
///   goes airborne, restoring depInfo display and the T+ timer for a subsequent attempt.
/// Phase 2 – airborne: when DEPA ground state and pressAlt ≥ fieldElev + 50 ft, assigns the departure
///   sequence number and computes spacing data relative to the previous departure on the same runway.
///   If Phase 1 never fired (e.g. no runway width configured), the tick is set here as a fallback.
void CDelHelX_Timers::DetectTakeoffState(EuroScopePlugIn::CRadarTarget rt)
{
    if (!rt.IsValid()) { return; }

    std::string callSign = rt.GetCallsign();
    auto mapIt = this->twrSameSID_flightPlans.find(callSign);
    if (mapIt == this->twrSameSID_flightPlans.end()) { return; }

    EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
    if (!fp.IsValid()) { return; }

    std::string dep = fp.GetFlightPlanData().GetOrigin();
    to_upper(dep);
    auto airportIt = this->airports.find(dep);
    if (airportIt == this->airports.end()) { return; }

    std::string depRwy = fp.GetFlightPlanData().GetDepartureRwy();
    auto rwyIt = airportIt->second.runways.find(depRwy);
    if (rwyIt == airportIt->second.runways.end()) { return; }

    const runway& rwy = rwyIt->second;
    auto pos    = rt.GetPosition();
    int pressAlt     = pos.GetPressureAltitude();
    int depElevation = airportIt->second.fieldElevation;

    // ── Phase 1b: Rejected takeoff ──
    // If the roll tick was set but the aircraft never went airborne and GS has fallen back below 30,
    // reset the tick so depInfo and T+ are restored for a subsequent attempt.
    if (mapIt->second != 0 && this->dep_sequenceNumber.count(callSign) == 0
        && pos.GetReportedGS() < 30)
    {
        mapIt->second = 0;
    }

    // ── Phase 1: Takeoff roll ──
    if (mapIt->second == 0 && pos.GetReportedGS() >= 40)
    {
        auto position = pos.GetPosition();
        if (IsPositionOnRunway(rwy, airportIt->second.runways, position))
        {
            auto oppIt = airportIt->second.runways.find(rwy.opposite);
            if (oppIt != airportIt->second.runways.end())
            {
                EuroScopePlugIn::CPosition oppThresh;
                oppThresh.m_Latitude  = oppIt->second.thresholdLat;
                oppThresh.m_Longitude = oppIt->second.thresholdLon;
                double rwyHdg  = DirectionFromRunwayThreshold(depRwy, oppThresh, airportIt->second.runways);
                double hdgDiff = std::abs(pos.GetReportedHeading() - rwyHdg);
                if (hdgDiff > 180.0) { hdgDiff = 360.0 - hdgDiff; }
                if (hdgDiff <= 45.0)
                {
                    mapIt->second = GetTickCount64();
                }
            }
        }
    }

    // ── Phase 2: Airborne ──
    // Sequence number assignment is gated here (not at roll) so the aircraft stays in group A/B
    // on the outbound list until it actually lifts off.
    // Tier 1 (50 ft): altitude close to liftoff — accurate for spacing — combined with a ground
    //   state check so a parked aircraft with a spurious reading cannot trigger it.
    //   Any of TAXI / LINEUP / DEPA confirms the controller gave the aircraft a clearance.
    // Tier 2 (200 ft): no ground state required — catches helicopters or aircraft where no
    //   TopSky state was ever set; spacing precision is slightly lower but still useful.
    std::string groundStatePh2 = fp.GetGroundState();
    bool airborneCondition = (pressAlt >= depElevation + 200)
                          || (pressAlt >= depElevation + 50
                              && (groundStatePh2 == "TAXI" || groundStatePh2 == "LINEUP"
                                  || groundStatePh2 == "DEPA"));
    if (this->dep_sequenceNumber.count(callSign) == 0 && airborneCondition)
    {
        if (mapIt->second == 0)
        {
            mapIt->second = GetTickCount64();  // fallback: roll wasn't detected
        }

        this->dep_sequenceNumber[callSign] = ++this->dep_sequenceCounter;

        {
            std::filesystem::path wavPath = std::filesystem::path(GetPluginDirectory()) / "airbourne.wav";
            PlaySoundA(wavPath.string().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        }

        auto prevDepIt = this->twrSameSID_lastDeparted.find(depRwy);
        if (prevDepIt != this->twrSameSID_lastDeparted.end())
        {
            const std::string& prevCallSign = prevDepIt->second;
            auto prevTakeoffIt = this->twrSameSID_flightPlans.find(prevCallSign);
            if (prevTakeoffIt != this->twrSameSID_flightPlans.end() && prevTakeoffIt->second > 0)
            {
                // dep_prevTakeoffOffset uses roll ticks for both aircraft → measures time between roll starts
                this->dep_prevTakeoffOffset[callSign] = (mapIt->second - prevTakeoffIt->second) / 1000;

                auto prevRt = this->RadarTargetSelect(prevCallSign.c_str());
                if (prevRt.IsValid())
                {
                    this->dep_prevDistanceAtTakeoff[callSign] = pos.GetPosition().DistanceTo(prevRt.GetPosition().GetPosition());
                    this->dep_prevWtc[callSign] = prevRt.GetCorrelatedFlightPlan().GetFlightPlanData().GetAircraftWtc();
                    this->dep_prevSid[callSign] = prevRt.GetCorrelatedFlightPlan().GetFlightPlanData().GetSidName();
                }

                int timeRequired = 120;
                this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
                auto prevFp = this->FlightPlanSelect(prevCallSign.c_str());
                if (prevFp.IsValid())
                {
                    this->flightStripAnnotation[prevCallSign] = prevFp.GetControllerAssignedData().GetFlightStripAnnotation(8);
                }
                if (this->flightStripAnnotation.find(prevCallSign) != this->flightStripAnnotation.end() &&
                    this->flightStripAnnotation[callSign].length() > 7 && this->flightStripAnnotation[prevCallSign].length() > 7)
                {
                    std::string prevHP = this->flightStripAnnotation[prevCallSign].substr(7);
                    std::string hp     = this->flightStripAnnotation[callSign].substr(7);
                    if (!IsSameHoldingPoint(prevHP, hp, airportIt->second.runways))
                    {
                        timeRequired += 60;
                    }
                }
                this->dep_timeRequired[callSign] = timeRequired;
            }
        }
        this->twrSameSID_lastDeparted[depRwy] = callSign;

        if (this->radarScreen != nullptr)
        {
            this->radarScreen->depRateLog[depRwy].push_back(mapIt->second);
        }
    }
}

/// @brief Fires a background VATSIM data fetch every 60 s and resolves the result into atisLetters.
/// Prefers _D_ callsigns when multiple ATIS stations match the same airport ICAO.
void CDelHelX_Timers::PollAtisLetters(int Counter)
{
    // Resolve a completed fetch
    if (this->atisFuture.valid() && this->atisFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        try
        {
            auto j = json::parse(this->atisFuture.get());
            for (auto& [icao, ap] : this->airports)
            {
                std::string icaoUpper = icao;
                to_upper(icaoUpper);

                std::string best;       // atis_code from best-matching station
                std::string bestCs;     // callsign of best-matching station

                for (auto& entry : j.at("atis"))
                {
                    std::string cs = entry.value("callsign", "");
                    to_upper(cs);

                    if (cs.find(icaoUpper) == std::string::npos)
                    {
                        continue;
                    }

                    std::string code = entry.value("atis_code", "");
                    if (code.empty())
                    {
                        continue;
                    }

                    // First match wins; a _D_ match overrides any earlier non-_D_ match
                    if (best.empty() || cs.find("_D_") != std::string::npos)
                    {
                        best   = code;
                        bestCs = cs;
                    }
                }

                auto it = this->atisLetters.find(icao);
                bool hadLetter = (it != this->atisLetters.end() && !it->second.empty());
                bool firstFetch = (it == this->atisLetters.end());

                if (!best.empty())
                {
                    if (!hadLetter || it->second != best)
                    {
                        this->LogDebugMessage("ATIS for " + icao + " changed to " + best + " (from " + bestCs + ")", "ATIS");
                        this->atisUnacked.insert(icao);
                    }
                    this->atisLetters[icao] = best;
                }
                else
                {
                    if (firstFetch)
                    {
                        this->LogDebugMessage("ATIS for " + icao + ": not available", "ATIS");
                    }
                    else if (hadLetter)
                    {
                        this->LogDebugMessage("ATIS for " + icao + ": no longer available", "ATIS");
                    }
                    this->atisLetters[icao] = "";
                }
            }
        }
        catch (std::exception& e)
        {
            this->LogMessage("ATIS fetch failed: " + std::string(e.what()), "ATIS");
        }

        this->atisFuture = std::future<std::string>();
    }

    // Launch a new fetch every 60 s; also fire at 15 s on first start when map is still empty
    if (!this->atisFuture.valid() && (Counter % 60 == 0 || (Counter == 15 && this->atisLetters.empty())))
    {
        this->atisFuture = std::async(std::launch::async, FetchVatsimData);
    }
}

/// @brief Applies any persisted window positions to a freshly created radar screen, then saves
/// positions back to disk whenever any window has been dragged to a new location.
void CDelHelX_Timers::SaveAndRestoreWindowLocations()
{
    if (this->radarScreen == nullptr) { return; }

    bool needsSave = false;

    auto syncWindow = [&](POINT& screenPos, int& savedX, int& savedY)
    {
        if (screenPos.x == -1 && savedX != -1 && savedY != -1)
        {
            // Restore persisted position before first auto-placement
            screenPos = { savedX, savedY };
        }
        else if (screenPos.x != -1 && (screenPos.x != savedX || screenPos.y != savedY))
        {
            savedX = screenPos.x;
            savedY = screenPos.y;
            needsSave = true;
        }
    };

    syncWindow(this->radarScreen->depRateWindowPos,     this->depRateWindowX,     this->depRateWindowY);
    syncWindow(this->radarScreen->twrOutboundWindowPos, this->twrOutboundWindowX, this->twrOutboundWindowY);
    syncWindow(this->radarScreen->twrInboundWindowPos,  this->twrInboundWindowX,  this->twrInboundWindowY);
    syncWindow(this->radarScreen->napWindowPos,          this->napWindowX,          this->napWindowY);
    syncWindow(this->radarScreen->weatherWindowPos,      this->weatherWindowX,      this->weatherWindowY);

    if (needsSave) { this->SaveWindowLocations(); }
}

/// @brief Rebuilds adesCache for all correlated flight plans departing from a configured airport.
/// For type-Y plans the tag shows the last IFR waypoint in turquoise;
/// all other plan types show the destination ICAO using the EuroScope default colour.
void CDelHelX_Timers::UpdateAdesCache()
{
    // Returns the last waypoint-like token before the first "VFR" token in the route string.
    // Returns empty string if no VFR marker is found or no valid waypoint precedes it.
    auto lastIfrWaypoint = [](const char* route) -> std::string {
        auto isWaypoint = [](const std::string& s) -> bool {
            if (s.empty() || s == "DCT" || s == "IFR" || s == "VFR") { return false; }
            // Strip speed/level suffix (e.g. WAYPOINT/N0450F350)
            std::string base = s.substr(0, s.find('/'));
            if (base.empty()) { return false; }
            if (!std::all_of(base.begin(), base.end(), ::isalnum)) { return false; }
            // Speed/level change group: N/K/M followed immediately by a digit
            if ((base[0] == 'N' || base[0] == 'K' || base[0] == 'M') && base.size() >= 2 && std::isdigit((unsigned char)base[1])) { return false; }
            return true;
        };

        std::string last;
        bool vfrFound  = false;
        bool firstToken = true;
        std::istringstream ss(route);
        std::string tok;
        while (ss >> tok)
        {
            if (tok == "VFR")
            {
                if (firstToken) { return ""; }  // VFR at front = type V or Z, leave unchanged
                vfrFound = true;
                break;
            }
            if (isWaypoint(tok)) { last = tok.substr(0, tok.find('/')); }
            firstToken = false;
        }
        return vfrFound ? last : "";  // no VFR token found = normal IFR, leave unchanged
    };

    std::map<std::string, tagInfo> newCache;
    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid()) { continue; }

        EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
        std::string dep = fpd.GetOrigin();
        to_upper(dep);
        if (this->airports.find(dep) == this->airports.end()) { continue; }

        std::string callSign = fp.GetCallsign();
        std::string fix = lastIfrWaypoint(fpd.GetRoute());

        tagInfo tag;
        if (!fix.empty())
        {
            tag.tag   = fix;
            tag.color = TAG_COLOR_TURQ;
        }
        else
        {
            tag.tag   = fpd.GetDestination();
            tag.color = TAG_COLOR_DEFAULT_NONE;
        }

        newCache[callSign] = tag;
    }
    this->adesCache = std::move(newCache);
}


/// @brief Updates or removes departure information overlays on the radar screen for taxiing aircraft.
/// Reads dep_info text/colour and SID colour from the pre-calculated tag cache instead of calling
/// OnGetTagItem, then appends the ",T" transfer indicator for GND controllers.
void CDelHelX_Timers::UpdateRadarTargetDepartureInfo()
{
    if (this->radarScreen == nullptr) { return; }

    if (this->GetConnectionType() == EuroScopePlugIn::CONNECTION_TYPE_NO)
    {
        this->radarScreen->radarTargetDepartureInfos.clear();
        return;
    }

    if (this->ControllerMyself().GetFacility() < 3)
    {
        this->radarScreen->radarTargetDepartureInfos.clear();
        return;
    }

    auto me = this->ControllerMyself();
    bool isGnd = me.IsController() && me.GetRating() > 1 && me.GetFacility() <= 3;

    // Determine which aircraft qualify for an overlay
    std::set<std::string> toShow;
    for (auto& [callSign, takeoffTick] : this->twrSameSID_flightPlans)
    {
        EuroScopePlugIn::CFlightPlan  fp = this->FlightPlanSelect(callSign.c_str());
        EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelect(callSign.c_str());
        if (!fp.IsValid() || !rt.IsValid() || !rt.GetPosition().IsValid()) { continue; }

        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);
        auto airportIt = this->airports.find(dep);
        if (airportIt == this->airports.end()) { continue; }

        std::string groundState = fp.GetGroundState();
        auto pressAlt   = rt.GetPosition().GetPressureAltitude();
        auto groundSpeed = rt.GetPosition().GetReportedGS();
        int  depElevation = airportIt->second.fieldElevation;

        if ((groundState == "TAXI" || groundState == "DEPA") &&
            pressAlt < depElevation + 50 && groundSpeed < 40)
        {
            toShow.insert(callSign);
        }
    }

    // Remove entries that no longer qualify
    for (auto it = this->radarScreen->radarTargetDepartureInfos.begin();
         it != this->radarScreen->radarTargetDepartureInfos.end(); )
    {
        if (toShow.find(it->first) == toShow.end())
        {
            it = this->radarScreen->radarTargetDepartureInfos.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Add/update entries from the outbound row cache
    for (const auto& callSign : toShow)
    {
        // Find the row in twrOutboundRowsCache for this callsign
        auto rowIt = std::find_if(this->radarScreen->twrOutboundRowsCache.begin(),
                                   this->radarScreen->twrOutboundRowsCache.end(),
                                   [&callSign](const TwrOutboundRowCache& r) { return r.callsign == callSign; });
        if (rowIt == this->radarScreen->twrOutboundRowsCache.end()) { continue; }
        const TwrOutboundRowCache& cachedRow = *rowIt;

        EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelect(callSign.c_str());
        if (!fp.IsValid()) { continue; }

        // Read annotation once for the GND transfer indicator check
        this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
        const auto& annotation = this->flightStripAnnotation[callSign];

        std::string dep_info = cachedRow.depInfo.tag;
        if (isGnd)
        {
            bool transferred = false;
            if (annotation.length() >= 7)
            {
                std::string storedFreq = annotation.substr(1, 6);
                if (storedFreq.find_first_not_of(' ') != std::string::npos)
                {
                    double myFreqDouble = me.GetPrimaryFrequency();
                    auto s = std::to_string(myFreqDouble);
                    std::string myFreq = s.substr(0, s.find('.') + 4);
                    myFreq.erase(std::remove(myFreq.begin(), myFreq.end(), '.'), myFreq.end());
                    transferred = (storedFreq != myFreq);
                }
            }
            if (!transferred) { dep_info += ",T"; }
        }

        auto findIt = this->radarScreen->radarTargetDepartureInfos.find(callSign);
        if (findIt == this->radarScreen->radarTargetDepartureInfos.end())
        {
            depInfo di;
            di.dep_info  = dep_info;
            di.dep_color = cachedRow.depInfo.color;
            di.pos       = { -1, -1 };
            di.dragX     = 0;
            di.dragY     = 0;
            di.lastDrag  = { -1, -1 };
            di.hp_info   = cachedRow.hp.tag;
            di.hp_color  = cachedRow.hp.color;
            di.sid_color = cachedRow.sameSid.color;
            this->radarScreen->radarTargetDepartureInfos.insert_or_assign(callSign, di);
        }
        else
        {
            findIt->second.dep_info  = dep_info;
            findIt->second.dep_color = cachedRow.depInfo.color;
            findIt->second.hp_info   = cachedRow.hp.tag;
            findIt->second.hp_color  = cachedRow.hp.color;
            findIt->second.sid_color = cachedRow.sameSid.color;
        }
    }
}

/// @brief Updates the TWR same-SID outbound list and records per-departure timing and sequencing data.
void CDelHelX_Timers::UpdateTowerSameSID()
{
    if (this->GetConnectionType() == EuroScopePlugIn::CONNECTION_TYPE_NO)
    {
        if (!this->twrSameSID_flightPlans.empty())
        {
            for (auto twr_fp : this->twrSameSID_flightPlans)
            {
                auto fp = this->FlightPlanSelect(twr_fp.first.c_str());
                this->twrSameSID.RemoveFpFromTheList(fp);
            }

            this->twrSameSID_flightPlans.clear();
        }

        return;
    }

    // Prune departure rate log entries older than 1 hour
    if (this->radarScreen != nullptr && !this->radarScreen->depRateLog.empty())
    {
        ULONGLONG now = GetTickCount64();
        for (auto& kv : this->radarScreen->depRateLog)
        {
            auto& ts = kv.second;
            ts.erase(std::remove_if(ts.begin(), ts.end(),
                [now](ULONGLONG t) { return (now - t) > 3600000ULL; }), ts.end());
        }
    }

    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        std::string callSign = fp.GetCallsign();

        if (!pos.IsValid() || !fp.IsValid())
        {
            if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
            {
                this->twrSameSID.RemoveFpFromTheList(fp);
                this->twrSameSID_flightPlans.erase(callSign);
            }

            continue;
        }

        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);

        std::string arr = fp.GetFlightPlanData().GetDestination();
        to_upper(arr);

        // Skip aircraft without a valid flight plan (no departure/destination airport)
        if (dep.empty() || arr.empty()) {
            if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
            {
                this->twrSameSID.RemoveFpFromTheList(fp);
                this->twrSameSID_flightPlans.erase(callSign);
                this->dep_prevWtc.erase(callSign);
                this->dep_prevSid.erase(callSign);
                this->dep_prevTakeoffOffset.erase(callSign);
                this->dep_prevDistanceAtTakeoff.erase(callSign);
                this->dep_timeRequired.erase(callSign);
                this->dep_sequenceNumber.erase(callSign);
            }

            continue;
        }

        auto airport = this->airports.find(dep);
        if (airport == this->airports.end())
        {
            // Airport not in config
            if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
            {
                this->twrSameSID.RemoveFpFromTheList(fp);
                this->twrSameSID_flightPlans.erase(callSign);
                this->dep_prevWtc.erase(callSign);
                this->dep_prevSid.erase(callSign);
                this->dep_prevTakeoffOffset.erase(callSign);
                this->dep_prevDistanceAtTakeoff.erase(callSign);
                this->dep_timeRequired.erase(callSign);
                this->dep_sequenceNumber.erase(callSign);
            }

            continue;
        }

        // Check if the flight plan needs to be added to the list
        std::string groundState = fp.GetGroundState();
        auto pressAlt = pos.GetPressureAltitude();
        int depElevation = airport->second.fieldElevation;
        if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < depElevation + 50 && this->twrSameSID_flightPlans.find(callSign) == this->twrSameSID_flightPlans.end())
        {
            this->twrSameSID.AddFpToTheList(fp);
            this->twrSameSID_flightPlans.emplace(callSign, 0);
        }

        // Check if we need to remove the flight plan because of ground state
        if (!(groundState == "TAXI" || groundState == "DEPA") && this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
        {
            this->twrSameSID.RemoveFpFromTheList(fp);
            this->twrSameSID_flightPlans.erase(callSign);
            this->dep_prevWtc.erase(callSign);
            this->dep_prevSid.erase(callSign);
            this->dep_prevTakeoffOffset.erase(callSign);
            this->dep_prevDistanceAtTakeoff.erase(callSign);
            this->dep_timeRequired.erase(callSign);
            this->dep_sequenceNumber.erase(callSign);
        }

        // Check if the aircraft has departed and is further than 15nm away or more than 4 minutes have passed since takeoff
        if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
        {
            if (this->twrSameSID_flightPlans.at(callSign) > 0)
            {
                ULONGLONG now = GetTickCount64();
                auto seconds = (now - this->twrSameSID_flightPlans.at(callSign)) / 1000;
                if (seconds > 4 * 60)
                {
                    this->flightStripAnnotation.erase(callSign);
                    fp.GetControllerAssignedData().SetFlightStripAnnotation(8, "");
                    this->PushToOtherControllers(fp);
                    this->twrSameSID.RemoveFpFromTheList(fp);
                    this->twrSameSID_flightPlans.erase(callSign);
                    this->dep_prevWtc.erase(callSign);
                    this->dep_prevSid.erase(callSign);
                    this->dep_prevTakeoffOffset.erase(callSign);
                    this->dep_prevDistanceAtTakeoff.erase(callSign);
                    this->dep_timeRequired.erase(callSign);
                    this->dep_sequenceNumber.erase(callSign);
                    continue;
                }
            }

            EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
            std::string rwy = fpd.GetDepartureRwy();
            auto position = pos.GetPosition();
            auto distance = DistanceFromRunwayThreshold(rwy, position, airport->second.runways);

            if (distance >= 20)
            {
                this->flightStripAnnotation.erase(callSign);
                fp.GetControllerAssignedData().SetFlightStripAnnotation(8, "");
                this->PushToOtherControllers(fp);
                this->twrSameSID.RemoveFpFromTheList(fp);
                this->twrSameSID_flightPlans.erase(callSign);
                this->dep_prevWtc.erase(callSign);
                this->dep_prevSid.erase(callSign);
                this->dep_prevTakeoffOffset.erase(callSign);
                this->dep_prevDistanceAtTakeoff.erase(callSign);
                this->dep_timeRequired.erase(callSign);
                this->dep_sequenceNumber.erase(callSign);
            }
        }
    }
}

/// @brief Updates the TTT inbound list: detects new inbounds, removes departed aircraft, and handles go-arounds.
void CDelHelX_Timers::UpdateTTTInbounds()
{
    if (this->GetConnectionType() == EuroScopePlugIn::CONNECTION_TYPE_NO)
    {
        if (!this->ttt_flightPlans.empty())
        {
            for (auto ttt_fp : this->ttt_flightPlans)
            {
                auto fp = this->FlightPlanSelect(ttt_fp.first.c_str());
                this->tttInbound.RemoveFpFromTheList(fp);
            }

            this->ttt_flightPlans.clear();
            this->ttt_goAround.clear();
            this->ttt_recentlyRemoved.clear();
        }

        return;
    }

    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        std::string callSign = fp.GetCallsign();

        if (!pos.IsValid())
        {
            this->tttInbound.RemoveFpFromTheList(fp);
            continue;
        }

        for (auto airport = this->airports.begin(); airport != this->airports.end(); ++airport)
        {
            for (auto rwy = airport->second.runways.begin(); rwy != airport->second.runways.end(); ++rwy)
            {
                std::string rwyCallsign = callSign + rwy->second.designator;
                std::string arrRwyDigits = rwy->second.designator;
                arrRwyDigits.erase(std::remove_if(arrRwyDigits.begin(), arrRwyDigits.end(),
                    [](char c) { return !std::isdigit(c); }), arrRwyDigits.end());
                int arrRwyHdg = arrRwyDigits.empty() ? -1 : std::stoi(arrRwyDigits);

                // If we can't determine the arrival runway heading, we can't determine if it's an inbound or not, so skip it
                if (arrRwyHdg == -1)
                {
                    if (this->ttt_flightPlans.find(rwyCallsign) != this->ttt_flightPlans.end())
                    {
                        this->tttInbound.RemoveFpFromTheList(fp);
                        this->ttt_flightPlans.erase(rwyCallsign);
                        this->ttt_recentlyRemoved[rwyCallsign] = GetTickCount64();
                    }
                    continue;
                }

                // Check if the flight plan needs to be added to the list
                auto position = pos.GetPosition();
                auto distance = DistanceFromRunwayThreshold(rwy->second.designator, position, airport->second.runways);
                auto direction = DirectionFromRunwayThreshold(rwy->second.designator, position, airport->second.runways);
                auto pressAlt = pos.GetPressureAltitude();
                auto heading = pos.GetReportedHeading();
                int hdgDiff = std::abs(heading - arrRwyHdg * 10);
                if (hdgDiff > 180) hdgDiff = 360 - hdgDiff;
                double approachDir = std::fmod(arrRwyHdg * 10 + 180.0, 360.0);
                double dirDiff = std::abs(direction - approachDir);
                if (dirDiff > 180.0) dirDiff = 360.0 - dirDiff;

                int depElevation = airport->second.fieldElevation;
                if (pressAlt > depElevation + 50 && pressAlt < depElevation + 50 + 7000 && hdgDiff <= 30 && distance < 20 && dirDiff <= 5.0)
                {
                    this->ttt_distanceToRunway[rwyCallsign] = distance;
                    std::string trackingControllerId = fp.GetTrackingControllerId();

                    if ((fp.GetTrackingControllerIsMe() || trackingControllerId.empty()) && this->standAssignment.find(callSign) == this->standAssignment.end())
                    {
                        // Trigger auto stand assignment in GroundRadar plugin
                        this->radarScreen->StartTagFunction(callSign.c_str(), "GRplugin", 0, "   Auto   ", GROUNDRADAR_PLUGIN_NAME, 2, POINT(), RECT());
                    }

                    if (this->ttt_flightPlans.find(rwyCallsign) == this->ttt_flightPlans.end())
                    {
                        this->tttInbound.AddFpToTheList(fp);
                        this->ttt_flightPlans.emplace(rwyCallsign, rwy->second);
                    }
                }
                else
                {
                    // Only remove normal inbounds; go-arounds are handled in the separate pass below
                    if (this->ttt_flightPlans.find(rwyCallsign) != this->ttt_flightPlans.end()
                        && this->ttt_goAround.find(rwyCallsign) == this->ttt_goAround.end())
                    {
                        this->tttInbound.RemoveFpFromTheList(fp);
                        this->ttt_flightPlans.erase(rwyCallsign);
                        this->ttt_distanceToRunway.erase(rwyCallsign);
                        this->ttt_recentlyRemoved[rwyCallsign] = GetTickCount64();
                    }
                }
            }
        }
    }

    // Go-around detection and lifecycle pass
    // Prune stale recently-removed entries (>60s with no active go-around)
    {
        ULONGLONG now = GetTickCount64();
        for (auto it = this->ttt_recentlyRemoved.begin(); it != this->ttt_recentlyRemoved.end(); )
        {
            if ((now - it->second) / 1000 > 60)
            {
                it = this->ttt_recentlyRemoved.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        std::string callSign = fp.GetCallsign();

        if (!pos.IsValid())
        {
            continue;
        }

        for (auto airport = this->airports.begin(); airport != this->airports.end(); ++airport)
        {
            for (auto rwy = airport->second.runways.begin(); rwy != airport->second.runways.end(); ++rwy)
            {
                std::string rwyCallsign = callSign + rwy->second.designator;
                bool isVfrTrackedByMe = fp.IsValid() && fp.GetFlightPlanData().GetPlanType() == std::string("V") && fp.GetTrackingControllerIsMe();
                bool isGoAround = !isVfrTrackedByMe && this->ttt_goAround.find(rwyCallsign) != this->ttt_goAround.end();
                bool isRecentlyRemoved = !isGoAround && !isVfrTrackedByMe && this->ttt_recentlyRemoved.find(rwyCallsign) != this->ttt_recentlyRemoved.end();

                if (!isGoAround && !isRecentlyRemoved)
                {
                    continue;
                }

                const std::string& opp = rwy->second.opposite;
                auto position = pos.GetPosition();
                auto pressAlt = pos.GetPressureAltitude();
                int depElevation = airport->second.fieldElevation;
                double distance = DistanceFromRunwayThreshold(rwy->second.designator, position, airport->second.runways);
                auto heading = pos.GetReportedHeading();
                std::string rwyDigits = rwy->second.designator;
                rwyDigits.erase(std::remove_if(rwyDigits.begin(), rwyDigits.end(),
                    [](char c) { return !std::isdigit(c); }), rwyDigits.end());
                int arrRwyHdg = rwyDigits.empty() ? -1 : std::stoi(rwyDigits);
                int hdgDiff = (arrRwyHdg == -1) ? 0 : std::abs(heading - arrRwyHdg * 10);
                if (hdgDiff > 180) { hdgDiff = 360 - hdgDiff; }

                if (isGoAround)
                {
                    // Active go-around: check removal conditions, otherwise keep distance updated
                    if (distance > 5.0 || pressAlt > depElevation + 3000 || pressAlt < depElevation + 100 || hdgDiff > 30)
                    {
                        this->tttInbound.RemoveFpFromTheList(fp);
                        this->ttt_flightPlans.erase(rwyCallsign);
                        this->ttt_goAround.erase(rwyCallsign);
                        this->ttt_distanceToRunway.erase(rwyCallsign);
                    }
                    else
                    {
                        this->ttt_distanceToRunway[rwyCallsign] = distance;
                    }
                }
                else if (!opp.empty())
                {
                    // Check for go-around: recently removed from normal list, now near opposite threshold and airborne
                    double distFromOpp = DistanceFromRunwayThreshold(opp, position, airport->second.runways);
                    if (distFromOpp < 5.0 && pressAlt > depElevation + 100)
                    {
                        this->ttt_goAround[rwyCallsign] = GetTickCount64();
                        this->ttt_distanceToRunway[rwyCallsign] = distance;
                        this->ttt_flightPlans.emplace(rwyCallsign, rwy->second);
                        this->tttInbound.AddFpToTheList(fp);
                        this->ttt_recentlyRemoved.erase(rwyCallsign);
                    }
                }
            }
        }
    }

    // Rebuild sorted-by-runway index
    this->ttt_sortedByRunway.clear();
    for (auto& [key, dist] : this->ttt_distanceToRunway)
    {
        auto planIt = this->ttt_flightPlans.find(key);
        if (planIt != this->ttt_flightPlans.end())
        {
            this->ttt_sortedByRunway[planIt->second.designator].push_back(key);
        }
    }
    for (auto& [designator, keys] : this->ttt_sortedByRunway)
    {
        std::sort(keys.begin(), keys.end(), [this](const std::string& a, const std::string& b) {
            return this->ttt_distanceToRunway.at(a) < this->ttt_distanceToRunway.at(b);
            });
    }
}

/// @brief Records today's UTC date as the last NAP acknowledgement date and persists it to windowLocations.json.
void CDelHelX_Timers::AckNapReminder()
{
    this->napLastDismissedDate = UtcDateString();
    this->SaveWindowLocations();
}

/// @brief Clears all unacknowledged change flags for the given airport.
void CDelHelX_Timers::AckWeather(const std::string& icao)
{
    this->windUnacked.erase(icao);
    this->qnhUnacked.erase(icao);
    this->atisUnacked.erase(icao);
    this->rvrUnacked.erase(icao);
}