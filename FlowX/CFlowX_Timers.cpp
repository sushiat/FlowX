/**
 * @file CFlowX_Timers.cpp
 * @brief Timer-driven state management; TTT tracking, same-SID detection, and reconnect restore.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CFlowX_Timers.h"

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
    auto               now = std::chrono::system_clock::now();
    auto               ymd = date::year_month_day{date::floor<date::days>(now)};
    std::ostringstream ss;
    int                y = (int)ymd.year();
    auto               m = (unsigned)ymd.month();
    auto               d = (unsigned)ymd.day();
    ss << y << "-" << (m < 10 ? "0" : "") << m << "-" << (d < 10 ? "0" : "") << d;
    return ss.str();
}

/// @brief Checks each airport's NAP reminder configuration and shows the custom reminder window when the time is reached.
/// The reminder is suppressed if it was already acknowledged today (UTC date comparison).
void CFlowX_Timers::CheckAirportNAPReminder()
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
                    auto                     tod      = timeSplit[1];
                    std::vector<std::string> todSplit = split(tod, ':');
                    if (todSplit.size() == 3)
                    {
                        int hours   = atoi(todSplit[0].c_str());
                        int minutes = atoi(todSplit[1].c_str());

                        if ((hours == airport.second.nap_reminder.hour && minutes >= airport.second.nap_reminder.minute) || hours > airport.second.nap_reminder.hour)
                        {
                            airport.second.nap_reminder.triggered = true;

                            // Suppress if already acknowledged today
                            if (this->napLastDismissedDate == UtcDateString())
                            {
                                continue;
                            }

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
void CFlowX_Timers::CheckReconnects()
{
    if (!this->autoRestore || this->reconnect_pending.empty())
    {
        return;
    }

    constexpr ULONGLONG timeoutMs = 90000ULL;
    ULONGLONG           now       = GetTickCount64();

    // Expire snapshots older than 90 s and clean up their groundStatus entries
    for (auto it = this->reconnect_pending.begin(); it != this->reconnect_pending.end();)
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

        std::string callSign  = fp.GetCallsign();
        auto        pendingIt = this->reconnect_pending.find(callSign);
        if (pendingIt == this->reconnect_pending.end())
        {
            continue;
        }

        const reconnectSnapshot&                           snap  = pendingIt->second;
        EuroScopePlugIn::CFlightPlanData                   fpd   = fp.GetFlightPlanData();
        EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();

        std::string depAirport = fpd.GetOrigin();
        to_upper(depAirport);
        std::string destAirport = fpd.GetDestination();
        to_upper(destAirport);

        auto logMismatch = [&](const std::string& field, const std::string& got, const std::string& expected)
        {
            this->LogDebugMessage(callSign + " reconnect mismatch [" + field + "]: got=\"" + got + "\" expected=\"" + expected + "\"", "AutoRestore");
        };

        bool match = true;
        if (std::string(fp.GetPilotName()) != snap.pilotName)
        {
            logMismatch("pilotName", fp.GetPilotName(), snap.pilotName);
            match = false;
        }
        if (depAirport != snap.depAirport)
        {
            logMismatch("depAirport", depAirport, snap.depAirport);
            match = false;
        }
        if (destAirport != snap.destAirport)
        {
            logMismatch("destAirport", destAirport, snap.destAirport);
            match = false;
        }
        if (std::string(fpd.GetAircraftFPType()) != snap.aircraftType)
        {
            logMismatch("aircraftType", fpd.GetAircraftFPType(), snap.aircraftType);
            match = false;
        }
        if (fpd.GetAircraftWtc() != snap.wtc)
        {
            logMismatch("wtc", std::string(1, fpd.GetAircraftWtc()), std::string(1, snap.wtc));
            match = false;
        }
        if (std::string(fpd.GetPlanType()) != snap.planType)
        {
            logMismatch("planType", fpd.GetPlanType(), snap.planType);
            match = false;
        }
        {
            auto trimRoute = [](std::string s) -> std::string
            {
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
                return s;
            };
            if (trimRoute(std::string(fpd.GetRoute())) != trimRoute(snap.route))
            {
                logMismatch("route", fpd.GetRoute(), snap.route);
                match = false;
            }
        }
        if (std::string(fpd.GetSidName()) != snap.sidName)
        {
            logMismatch("sidName", fpd.GetSidName(), snap.sidName);
            match = false;
        }
        if (std::string(fpcad.GetSquawk()) != snap.squawk)
        {
            logMismatch("squawk", fpcad.GetSquawk(), snap.squawk);
            match = false;
        }

        if (match && snap.hasPosition)
        {
            EuroScopePlugIn::CPosition storedPos;
            storedPos.m_Latitude  = snap.lat;
            storedPos.m_Longitude = snap.lon;
            double dist           = rt.GetPosition().GetPosition().DistanceTo(storedPos);
            if (dist > 1.0)
            {
                logMismatch("position", std::format("{}nm", dist), "<1nm");
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
                                 (snap.clearanceFlag ? "yes" : "no") + ", gnd=" + snap.savedGroundStatus + ")",
                             "AutoRestore");
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
void CFlowX_Timers::DetectTakeoffState(EuroScopePlugIn::CRadarTarget rt)
{
    if (!rt.IsValid())
    {
        return;
    }

    std::string callSign = rt.GetCallsign();
    auto        mapIt    = this->twrSameSID_flightPlans.find(callSign);
    if (mapIt == this->twrSameSID_flightPlans.end())
    {
        return;
    }

    EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
    if (!fp.IsValid())
    {
        return;
    }

    std::string dep = fp.GetFlightPlanData().GetOrigin();
    to_upper(dep);
    auto airportIt = this->airports.find(dep);
    if (airportIt == this->airports.end())
    {
        return;
    }

    std::string depRwy = fp.GetFlightPlanData().GetDepartureRwy();
    auto        rwyIt  = airportIt->second.runways.find(depRwy);
    if (rwyIt == airportIt->second.runways.end())
    {
        return;
    }

    const runway& rwy          = rwyIt->second;
    auto          pos          = rt.GetPosition();
    int           pressAlt     = pos.GetPressureAltitude();
    int           depElevation = airportIt->second.fieldElevation;

    // ── Phase 1b: Rejected takeoff ──
    // If the roll tick was set but the aircraft never went airborne and GS has fallen back below 30,
    // reset the tick so depInfo and T+ are restored for a subsequent attempt.
    if (mapIt->second != 0 && !this->dep_sequenceNumber.contains(callSign) && pos.GetReportedGS() < 30)
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
                double rwyHdg         = DirectionFromRunwayThreshold(depRwy, oppThresh, airportIt->second.runways);
                double hdgDiff        = std::abs(pos.GetReportedHeading() - rwyHdg);
                if (hdgDiff > 180.0)
                {
                    hdgDiff = 360.0 - hdgDiff;
                }
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
    std::string groundStatePh2    = fp.GetGroundState();
    bool        airborneCondition = (pressAlt >= depElevation + 200) || (pressAlt >= depElevation + 50 && (groundStatePh2 == "TAXI" || groundStatePh2 == "LINEUP" || groundStatePh2 == "DEPA"));
    if (!this->dep_sequenceNumber.contains(callSign) && airborneCondition)
    {
        if (mapIt->second == 0)
        {
            mapIt->second = GetTickCount64(); // fallback: roll wasn't detected
        }

        this->dep_sequenceNumber[callSign] = ++this->dep_sequenceCounter;

        if (this->GetSoundAirborne() && this->ControllerMyself().GetFacility() >= 4)
        {
            std::filesystem::path wavPath = std::filesystem::path(GetPluginDirectory()) / "airbourne.wav";
            PlaySoundA(wavPath.string().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        }

        auto prevDepIt = this->twrSameSID_lastDeparted.find(depRwy);
        if (prevDepIt != this->twrSameSID_lastDeparted.end())
        {
            const std::string& prevCallSign  = prevDepIt->second;
            auto               prevTakeoffIt = this->twrSameSID_flightPlans.find(prevCallSign);
            if (prevTakeoffIt != this->twrSameSID_flightPlans.end() && prevTakeoffIt->second > 0)
            {
                // Create the spacing entry now so the live-spacing column and the
                // spacing snapshot (recorded later at transfer-of-communication) can use it.
                this->dep_liveSpacing[callSign].prevCallSign = prevCallSign;
            }
        }
        this->twrSameSID_lastDeparted[depRwy] = callSign;

        if (this->radarScreen != nullptr)
        {
            this->radarScreen->depRateLog[depRwy].push_back(mapIt->second);
        }
    }
}

/// @brief Snapshots distance, WTC, SID, and time-offset data for the given departing aircraft.
/// Called at transfer-of-communication time (Func_TransferNext) or by the timer failsafe.
/// Idempotent: does nothing if the snapshot has already been recorded.
void CFlowX_Timers::RecordDepartureSpacingSnapshot(const std::string& callSign)
{
    // Already snapshotted — nothing to do
    auto spacingIt = this->dep_liveSpacing.find(callSign);
    if (spacingIt == this->dep_liveSpacing.end() || spacingIt->second.snapshotTaken)
    {
        return;
    }
    DepartureLiveSpacing& spacing      = spacingIt->second;
    const std::string&    prevCallSign = spacing.prevCallSign;

    // Both roll ticks must be available to compute the time offset
    auto thisTickIt = this->twrSameSID_flightPlans.find(callSign);
    auto prevTickIt = this->twrSameSID_flightPlans.find(prevCallSign);
    if (thisTickIt == this->twrSameSID_flightPlans.end() || thisTickIt->second == 0 ||
        prevTickIt == this->twrSameSID_flightPlans.end() || prevTickIt->second == 0)
    {
        return;
    }

    spacing.takeoffTimeOffset = (thisTickIt->second - prevTickIt->second) / 1000;

    auto thisRt = this->RadarTargetSelect(callSign.c_str());
    auto prevRt = this->RadarTargetSelect(prevCallSign.c_str());
    if (thisRt.IsValid() && prevRt.IsValid())
    {
        spacing.distanceAtTakeoff = thisRt.GetPosition().GetPosition().DistanceTo(prevRt.GetPosition().GetPosition());
        spacing.prevWtc           = prevRt.GetCorrelatedFlightPlan().GetFlightPlanData().GetAircraftWtc();
        spacing.prevSid           = prevRt.GetCorrelatedFlightPlan().GetFlightPlanData().GetSidName();
    }

    int  timeRequired = 120;
    auto thisFp       = this->FlightPlanSelect(callSign.c_str());
    auto prevFp       = this->FlightPlanSelect(prevCallSign.c_str());
    if (thisFp.IsValid() && prevFp.IsValid())
    {
        this->flightStripAnnotation[callSign]     = thisFp.GetControllerAssignedData().GetFlightStripAnnotation(8);
        this->flightStripAnnotation[prevCallSign] = prevFp.GetControllerAssignedData().GetFlightStripAnnotation(8);

        std::string depIcao = thisFp.GetFlightPlanData().GetOrigin();
        to_upper(depIcao);
        auto airportIt = this->airports.find(depIcao);
        if (airportIt != this->airports.end() &&
            this->flightStripAnnotation[callSign].length() > 7 &&
            this->flightStripAnnotation[prevCallSign].length() > 7)
        {
            std::string prevHP = this->flightStripAnnotation[prevCallSign].substr(7);
            std::string hp     = this->flightStripAnnotation[callSign].substr(7);
            if (!IsSameHoldingPoint(prevHP, hp, airportIt->second.runways))
            {
                timeRequired += 60;
            }
        }
    }
    spacing.timeRequired  = timeRequired;
    spacing.snapshotTaken = true;
}

/// @brief Fires a background VATSIM data fetch every 60 s and resolves the result into atisLetters.
/// Prefers _D_ callsigns when multiple ATIS stations match the same airport ICAO.
void CFlowX_Timers::PollAtisLetters(int Counter)
{
    // Resolve a completed fetch — JSON was already parsed on the worker thread
    if (this->atisFuture.valid() && this->atisFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        try
        {
            auto result = this->atisFuture.get(); // map<ICAO, atis_code>, built on worker

            for (auto& [icao, ap] : this->airports)
            {
                std::string icaoUpper = icao;
                to_upper(icaoUpper);

                auto it         = this->atisLetters.find(icao);
                bool hadLetter  = (it != this->atisLetters.end() && !it->second.empty());
                bool firstFetch = (it == this->atisLetters.end());

                auto resIt = result.find(icaoUpper);
                if (resIt != result.end() && !resIt->second.empty())
                {
                    const std::string& best = resIt->second;
                    if (!hadLetter || it->second != best)
                    {
                        this->LogDebugMessage("ATIS for " + icao + " changed to " + best, "ATIS");
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

        this->atisFuture = {};
    }

    // Launch a new fetch every 60 s; also fire at 15 s on first start when map is still empty
    if (!this->atisFuture.valid() && (Counter % 60 == 0 || (Counter == 15 && this->atisLetters.empty())))
    {
        std::vector<std::string> airportKeys;
        for (auto& [icao, _] : this->airports)
        {
            std::string k = icao;
            to_upper(k);
            airportKeys.push_back(k);
        }
        this->atisFuture = std::async(std::launch::async, FetchAtisData, std::move(airportKeys));
    }
}

/// @brief Rebuilds adesCache for all correlated flight plans departing from a configured airport.
/// For type-Y plans the tag shows the last IFR waypoint in turquoise;
/// all other plan types show the destination ICAO using the EuroScope default colour.
void CFlowX_Timers::UpdateAdesCache()
{
    // Returns the last waypoint-like token before the first "VFR" token in the route string.
    // Returns empty string if no VFR marker is found or no valid waypoint precedes it.
    auto lastIfrWaypoint = [](const char* route) -> std::string
    {
        auto isWaypoint = [](const std::string& s) -> bool
        {
            if (s.empty() || s == "DCT" || s == "IFR" || s == "VFR")
            {
                return false;
            }
            // Strip speed/level suffix (e.g. WAYPOINT/N0450F350)
            std::string base = s.substr(0, s.find('/'));
            if (base.empty())
            {
                return false;
            }
            if (!std::all_of(base.begin(), base.end(), [](unsigned char c)
                             { return std::isalnum(c); }))
            {
                return false;
            }
            // Speed/level change group: N/K/M followed immediately by a digit
            if ((base[0] == 'N' || base[0] == 'K' || base[0] == 'M') && base.size() >= 2 && std::isdigit((unsigned char)base[1]))
            {
                return false;
            }
            return true;
        };

        std::string        last;
        bool               vfrFound   = false;
        bool               firstToken = true;
        std::istringstream ss(route);
        std::string        tok;
        while (ss >> tok)
        {
            if (tok == "VFR")
            {
                if (firstToken)
                {
                    return "";
                } // VFR at front = type V or Z, leave unchanged
                vfrFound = true;
                break;
            }
            if (isWaypoint(tok))
            {
                last = tok.substr(0, tok.find('/'));
            }
            firstToken = false;
        }
        return vfrFound ? last : ""; // no VFR token found = normal IFR, leave unchanged
    };

    std::map<std::string, tagInfo> newCache;
    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid())
        {
            continue;
        }

        EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
        std::string                      dep = fpd.GetOrigin();
        to_upper(dep);
        if (this->airports.find(dep) == this->airports.end())
        {
            continue;
        }

        std::string callSign = fp.GetCallsign();
        std::string fix      = lastIfrWaypoint(fpd.GetRoute());

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
void CFlowX_Timers::UpdateRadarTargetDepartureInfo()
{
    if (this->radarScreen == nullptr)
    {
        return;
    }

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

    auto me    = this->ControllerMyself();
    bool isGnd = me.IsController() && me.GetRating() > 1 && me.GetFacility() <= 3;

    // Determine which aircraft qualify for an overlay
    std::set<std::string> toShow;
    for (auto& [callSign, takeoffTick] : this->twrSameSID_flightPlans)
    {
        EuroScopePlugIn::CFlightPlan  fp = this->FlightPlanSelect(callSign.c_str());
        EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelect(callSign.c_str());
        if (!fp.IsValid() || !rt.IsValid() || !rt.GetPosition().IsValid())
        {
            continue;
        }

        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);
        auto airportIt = this->airports.find(dep);
        if (airportIt == this->airports.end())
        {
            continue;
        }

        std::string groundState  = fp.GetGroundState();
        auto        pressAlt     = rt.GetPosition().GetPressureAltitude();
        auto        groundSpeed  = rt.GetPosition().GetReportedGS();
        int         depElevation = airportIt->second.fieldElevation;

        if ((groundState == "TAXI" || groundState == "DEPA") &&
            pressAlt < depElevation + 50 && groundSpeed < 40)
        {
            toShow.insert(callSign);
        }
    }

    // Remove entries that no longer qualify
    for (auto it = this->radarScreen->radarTargetDepartureInfos.begin();
         it != this->radarScreen->radarTargetDepartureInfos.end();)
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
        auto rowIt = std::ranges::find_if(this->radarScreen->twrOutboundRowsCache,
                                          [&callSign](const TwrOutboundRowCache& r)
                                          { return r.callsign == callSign; });
        if (rowIt == this->radarScreen->twrOutboundRowsCache.end())
        {
            continue;
        }
        const TwrOutboundRowCache& cachedRow = *rowIt;

        EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelect(callSign.c_str());
        if (!fp.IsValid())
        {
            continue;
        }

        // Read annotation once for the GND transfer indicator check
        this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
        const auto& annotation                = this->flightStripAnnotation[callSign];

        std::string dep_info = cachedRow.depInfo.tag;
        int         queuePos = 0;
        {
            auto queueIt = this->dep_queuePos.find(callSign);
            if (queueIt != this->dep_queuePos.end())
                queuePos = queueIt->second;
        }
        if (isGnd)
        {
            bool transferred = false;
            if (annotation.length() >= 7)
            {
                std::string storedFreq = annotation.substr(1, 6);
                if (storedFreq.find_first_not_of(' ') != std::string::npos)
                {
                    transferred = (storedFreq != freqToAnnotation(me.GetPrimaryFrequency()));
                }
            }
            if (!transferred)
            {
                dep_info += ",T";
            }
        }

        auto findIt = this->radarScreen->radarTargetDepartureInfos.find(callSign);
        if (findIt == this->radarScreen->radarTargetDepartureInfos.end())
        {
            depInfo di;
            di.dep_info  = dep_info;
            di.dep_color = cachedRow.depInfo.color;
            di.queue_pos = queuePos;
            di.anchor    = {0.0, 0.0};
            di.dragX     = 0;
            di.dragY     = 0;
            di.lastDrag  = {-1, -1};
            di.hp_info   = cachedRow.hp.tag;
            di.hp_color  = cachedRow.hp.color;
            di.sid_color = cachedRow.sameSid.color;
            this->radarScreen->radarTargetDepartureInfos.insert_or_assign(callSign, di);
        }
        else
        {
            findIt->second.dep_info  = dep_info;
            findIt->second.dep_color = cachedRow.depInfo.color;
            findIt->second.queue_pos = queuePos;
            findIt->second.hp_info   = cachedRow.hp.tag;
            findIt->second.hp_color  = cachedRow.hp.color;
            findIt->second.sid_color = cachedRow.sameSid.color;
        }
    }
}

/// @brief Sets ground status to PARK for arriving aircraft that have stopped inside their assigned stand polygon.
/// Depends on standOccupancy being current (refreshed by UpdateOccupiedStands every 4 ticks).
void CFlowX_Timers::CheckArrivedAtStand()
{
    if (!this->autoParked)
    {
        return;
    }

    static const std::set<std::string> depStates = {"PUSH", "ST-UP", "ONFREQ", "LINEUP", "DEPA"};

    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid())
        {
            continue;
        }

        std::string callSign = fp.GetCallsign();

        auto standIt = this->standAssignment.find(callSign);
        if (standIt == this->standAssignment.end())
        {
            continue;
        }

        std::string arr = fp.GetFlightPlanData().GetDestination();
        to_upper(arr);
        auto airportIt = this->airports.find(arr);
        if (airportIt == this->airports.end())
        {
            continue;
        }

        auto gsIt = this->groundStatus.find(callSign);
        if (gsIt != this->groundStatus.end() &&
            (depStates.contains(gsIt->second) || gsIt->second == "PARK"))
        {
            continue;
        }

        auto occupyIt = this->standOccupancy.find(standIt->second);
        if (occupyIt == this->standOccupancy.end() || occupyIt->second != callSign)
        {
            continue;
        }

        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
        {
            continue;
        }
        if (pos.GetReportedGS() >= 3)
        {
            continue;
        }
        if (pos.GetPressureAltitude() > airportIt->second.fieldElevation + 200)
        {
            continue;
        }

        std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
        fp.GetControllerAssignedData().SetScratchPadString("PARK");
        fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());

        this->gndTransfer_list.erase(callSign);
        this->gndTransfer_soundPlayed.erase(callSign);
        if (this->radarScreen)
        {
            this->radarScreen->gndTransferSquares.erase(callSign);
            this->radarScreen->pushTracked.erase(callSign);
            this->radarScreen->taxiTracked.erase(callSign);
            this->radarScreen->taxiAssigned.erase(callSign);
            this->radarScreen->taxiAssignedTimes.erase(callSign);
            this->radarScreen->taxiAssignedPos.erase(callSign);
        }

        this->LogDebugMessage(callSign + " auto-PARK at stand " + standIt->second, "GND");
    }

    // Remember each outbound's departure stand (polygon-derived). Never cleared on leaving;
    // overwritten only if the aircraft is subsequently detected inside a different stand.
    for (const auto& [standName, occupier] : this->standOccupancy)
    {
        if (occupier.empty())
            continue;
        auto fp = this->FlightPlanSelect(occupier.c_str());
        if (!fp.IsValid())
            continue;
        std::string dep = fp.GetFlightPlanData().GetOrigin();
        to_upper(dep);
        if (!this->airports.contains(dep))
            continue; // not a departure from one of our airports
        auto existing = this->departureStand.find(occupier);
        if (existing == this->departureStand.end() || existing->second != standName)
            this->departureStand[occupier] = standName;
    }
}

/// @brief Snapshots slow/grounded radar targets on the main thread, then offloads polygon tests to a worker thread.
/// The previous worker result is applied at the start of the next call (atisFuture pattern).
void CFlowX_Timers::UpdateOccupiedStands()
{
    // Apply previous worker result if ready.
    if (this->standOccupancyFuture.valid() &&
        this->standOccupancyFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        this->standOccupancy = this->standOccupancyFuture.get();
    }

    // Skip launch if worker is still running.
    if (this->standOccupancyFuture.valid() &&
        this->standOccupancyFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        this->dbg_standSkips++;
        return;
    }

    if (this->grStands.empty())
        return;

    // Snapshot phase — all EuroScope API calls happen here on the main thread.
    std::vector<StandCheckTarget> targets;
    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;
        if (pos.GetReportedGS() >= 50)
            continue;
        if (pos.GetPressureAltitude() > 5000)
            continue;

        StandCheckTarget snap;
        snap.callSign = rt.GetCallsign();
        snap.lat      = pos.GetPosition().m_Latitude;
        snap.lon      = pos.GetPosition().m_Longitude;
        snap.pressAlt = pos.GetPressureAltitude();
        snap.wingspan = 0.0;

        EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (fp.IsValid())
        {
            std::string acType = fp.GetFlightPlanData().GetAircraftFPType();
            auto        wsIt   = this->aircraftWingspans.find(acType);
            if (wsIt != this->aircraftWingspans.end())
                snap.wingspan = wsIt->second;
        }

        targets.push_back(std::move(snap));
    }

    if (targets.empty())
    {
        this->standOccupancy.clear();
        return;
    }

    // Launch worker — grStands and airports are read-only after startup; const-ref capture is safe.
    this->dbg_standLaunches++;
    this->standOccupancyFuture = std::async(
        std::launch::async,
        [targets = std::move(targets), &grStands = this->grStands, &airports = this->airports]()
            -> std::map<std::string, std::string>
        {
            std::map<std::string, std::string> result;
            std::string                        lastIcao;
            int                                fieldElev = 0;

            for (const auto& t : targets)
            {
                for (const auto& [key, stand] : grStands)
                {
                    if (stand.lat.size() < 3)
                        continue;

                    if (stand.icao != lastIcao)
                    {
                        lastIcao  = stand.icao;
                        fieldElev = 0;
                        auto apIt = airports.find(stand.icao);
                        if (apIt != airports.end())
                            fieldElev = apIt->second.fieldElevation;
                    }

                    if (t.pressAlt > fieldElev + 200)
                        continue;

                    // PointInsidePolygon requires non-const pointers — copy into local arrays.
                    size_t n = std::min(stand.lon.size(), size_t{16});
                    double polyX[16], polyY[16];
                    std::copy_n(stand.lon.data(), n, polyX);
                    std::copy_n(stand.lat.data(), n, polyY);

                    if (!CFlowX_LookupsTools::PointInsidePolygon(
                            static_cast<int>(n), polyX, polyY, t.lon, t.lat))
                        continue;

                    result[stand.name] = t.callSign;
                    for (const auto& block : stand.blocks)
                        if (t.wingspan >= block.minWingspan)
                            result[block.standName] = t.callSign;

                    break; // An aircraft can only occupy one stand — move on to the next target.
                }
            }

            return result;
        });
}

/// @brief Updates the TWR same-SID outbound list and records per-departure timing and sequencing data.
void CFlowX_Timers::UpdateTWROutbound()
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
            std::erase_if(ts, [now](ULONGLONG t)
                          { return (now - t) > 3600000ULL; });
        }
    }

    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CRadarTargetPositionData pos      = rt.GetPosition();
        EuroScopePlugIn::CFlightPlan              fp       = rt.GetCorrelatedFlightPlan();
        std::string                               callSign = fp.GetCallsign();

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
        if (dep.empty() || arr.empty())
        {
            if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
            {
                this->twrSameSID.RemoveFpFromTheList(fp);
                this->twrSameSID_flightPlans.erase(callSign);
                this->dep_liveSpacing.erase(callSign);
                this->dep_sequenceNumber.erase(callSign);
                this->readyTakeoff_wasWaiting.erase(callSign);
                this->readyTakeoff_okTick.erase(callSign);
                this->readyTakeoff_soundPlayed.erase(callSign);
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
                this->dep_liveSpacing.erase(callSign);
                this->dep_sequenceNumber.erase(callSign);
                this->readyTakeoff_wasWaiting.erase(callSign);
                this->readyTakeoff_okTick.erase(callSign);
                this->readyTakeoff_soundPlayed.erase(callSign);
            }

            continue;
        }

        // Check if the flight plan needs to be added to the list
        std::string groundState  = fp.GetGroundState();
        auto        pressAlt     = pos.GetPressureAltitude();
        int         depElevation = airport->second.fieldElevation;
        if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < depElevation + 50 && this->twrSameSID_flightPlans.find(callSign) == this->twrSameSID_flightPlans.end())
        {
            this->twrSameSID.AddFpToTheList(fp);
            this->twrSameSID_flightPlans.emplace(callSign, 0);
        }

        // Holding-point auto-detection (absorbed from AutoUpdateDepartureHoldingPoints)
        // Runs for any slow TAXI/DEPA aircraft that is still on the ground; writes slot 8 and
        // pushes to other controllers only when the annotation actually changes.
        if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < depElevation + 50 && pos.GetReportedGS() < 30)
        {
            EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad  = fp.GetControllerAssignedData();
            std::string                                        rwy    = fp.GetFlightPlanData().GetDepartureRwy();
            std::string                                        before = this->flightStripAnnotation[callSign];

            // Detect runway change — clear stale HP if it doesn't exist on the new runway.
            auto lastRwyIt = this->lastDepRunway.find(callSign);
            if (lastRwyIt != this->lastDepRunway.end() && lastRwyIt->second != rwy && !rwy.empty())
            {
                std::string ann = this->flightStripAnnotation[callSign];
                if (ann.length() > 7)
                {
                    std::string hpName = ann.substr(7);
                    if (!hpName.empty() && hpName.back() == '*')
                        hpName.pop_back();
                    if (!hpName.empty())
                    {
                        bool hpValid  = false;
                        auto newRwyIt = airport->second.runways.find(rwy);
                        if (newRwyIt != airport->second.runways.end())
                            hpValid = newRwyIt->second.holdingPoints.contains(hpName);
                        if (!hpValid)
                        {
                            ann                                   = ann.substr(0, 7);
                            this->flightStripAnnotation[callSign] = ann;
                            fpcad.SetFlightStripAnnotation(8, ann.c_str());
                            this->PushToOtherControllers(fp);
                            before = ann; // update baseline so the auto-detect block below doesn't re-push
                        }
                    }
                }
            }
            if (!rwy.empty())
                this->lastDepRunway[callSign] = rwy;

            auto rwyIt = airport->second.runways.find(rwy);
            if (rwyIt != airport->second.runways.end())
            {
                auto& cachedPos = this->lastHpCheckPos[callSign];
                auto  curPos    = pos.GetPosition();
                bool  moved     = (std::abs(curPos.m_Latitude - cachedPos.m_Latitude) > 0.0002 ||
                                   std::abs(curPos.m_Longitude - cachedPos.m_Longitude) > 0.0002);
                if (moved)
                {
                    cachedPos = curPos;
                    for (auto& [hpName, hpData] : rwyIt->second.holdingPoints)
                    {
                        if (PointInsidePolygon(static_cast<int>(hpData.lat.size()), hpData.lon.data(), hpData.lat.data(),
                                               curPos.m_Longitude,
                                               curPos.m_Latitude))
                        {
                            this->flightStripAnnotation[callSign] =
                                AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], hpName);
                        }
                    }
                }
            }
            if (before != this->flightStripAnnotation[callSign])
            {
                fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
                this->PushToOtherControllers(fp);
            }
        }

        // Check if we need to remove the flight plan because of ground state
        if (!(groundState == "TAXI" || groundState == "DEPA") && this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
        {
            this->twrSameSID.RemoveFpFromTheList(fp);
            this->twrSameSID_flightPlans.erase(callSign);
            this->dep_liveSpacing.erase(callSign);
            this->dep_sequenceNumber.erase(callSign);
            this->lastDepRunway.erase(callSign);
            this->lastHpCheckPos.erase(callSign);
            this->readyTakeoff_wasWaiting.erase(callSign);
            this->readyTakeoff_okTick.erase(callSign);
            this->readyTakeoff_soundPlayed.erase(callSign);
        }

        // Check if the aircraft has departed and is further than 15nm away or more than 4 minutes have passed since takeoff
        if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
        {
            if (this->twrSameSID_flightPlans.at(callSign) > 0)
            {
                ULONGLONG now     = GetTickCount64();
                auto      seconds = (now - this->twrSameSID_flightPlans.at(callSign)) / 1000;
                if (seconds > 4 * 60)
                {
                    this->flightStripAnnotation.erase(callSign);
                    fp.GetControllerAssignedData().SetFlightStripAnnotation(8, "");
                    this->PushToOtherControllers(fp);
                    this->twrSameSID.RemoveFpFromTheList(fp);
                    this->twrSameSID_flightPlans.erase(callSign);
                    this->dep_liveSpacing.erase(callSign);
                    this->dep_sequenceNumber.erase(callSign);
                    this->lastDepRunway.erase(callSign);
                    this->lastHpCheckPos.erase(callSign);
                    this->readyTakeoff_wasWaiting.erase(callSign);
                    this->readyTakeoff_okTick.erase(callSign);
                    this->readyTakeoff_soundPlayed.erase(callSign);
                    continue;
                }

                // Failsafe: snapshot not yet taken and tracking has already ended — record now
                auto spacingIt2 = this->dep_liveSpacing.find(callSign);
                if (this->dep_sequenceNumber.contains(callSign) &&
                    (spacingIt2 == this->dep_liveSpacing.end() || !spacingIt2->second.snapshotTaken) &&
                    (!fp.GetTrackingControllerIsMe() ||
                     fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED))
                {
                    this->RecordDepartureSpacingSnapshot(callSign);
                }
            }

            EuroScopePlugIn::CFlightPlanData fpd      = fp.GetFlightPlanData();
            std::string                      rwy      = fpd.GetDepartureRwy();
            auto                             position = pos.GetPosition();
            auto                             distance = DistanceFromRunwayThreshold(rwy, position, airport->second.runways);

            if (distance >= 20)
            {
                this->flightStripAnnotation.erase(callSign);
                fp.GetControllerAssignedData().SetFlightStripAnnotation(8, "");
                this->PushToOtherControllers(fp);
                this->twrSameSID.RemoveFpFromTheList(fp);
                this->twrSameSID_flightPlans.erase(callSign);
                this->dep_liveSpacing.erase(callSign);
                this->dep_sequenceNumber.erase(callSign);
                this->lastDepRunway.erase(callSign);
                this->lastHpCheckPos.erase(callSign);
                this->readyTakeoff_wasWaiting.erase(callSign);
                this->readyTakeoff_okTick.erase(callSign);
                this->readyTakeoff_soundPlayed.erase(callSign);
            }
        }
    }
}

/// @brief Updates the TTT inbound list: detects new inbounds, removes departed aircraft, and handles go-arounds.
void CFlowX_Timers::UpdateTWRInbound()
{
    if (this->GetConnectionType() == EuroScopePlugIn::CONNECTION_TYPE_NO)
    {
        if (!this->ttt_inbound.empty())
        {
            for (auto& [cs, state] : this->ttt_inbound)
            {
                auto fp = this->FlightPlanSelect(cs.c_str());
                this->tttInbound.RemoveFpFromTheList(fp);
            }

            this->ttt_inbound.clear();
            this->ttt_callSigns.clear();
            this->ttt_clearedToLand.clear();
            this->ttt_recentlyRemoved.clear();
            this->ttt_runwayOccupied.clear();
            this->dep_queuePos.clear();
            this->gndTransfer_list.clear();
            this->gndTransfer_soundPlayed.clear();
            if (this->radarScreen)
                this->radarScreen->gndTransferSquares.clear();
            if (this->radarScreen)
                this->radarScreen->gndTransferSquareTimes.clear();
        }

        return;
    }

    // Single pass over all radar targets.
    // Combines: runway-occupancy detection (was a separate airports×runways×targets triple-nest),
    // inbound tracking, and go-around lifecycle — all in one RadarTargetSelectFirst/Next walk.
    this->ttt_runwayOccupied.clear();

    // Prune stale recently-removed entries (>90 s with no active go-around)
    {
        ULONGLONG now = GetTickCount64();
        for (auto it = this->ttt_recentlyRemoved.begin(); it != this->ttt_recentlyRemoved.end();)
        {
            it = ((now - it->second) / 1000 > 90) ? this->ttt_recentlyRemoved.erase(it) : std::next(it);
        }
    }

    for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        EuroScopePlugIn::CRadarTargetPositionData pos      = rt.GetPosition();
        EuroScopePlugIn::CFlightPlan              fp       = rt.GetCorrelatedFlightPlan();
        std::string                               callSign = fp.GetCallsign();

        if (!pos.IsValid())
        {
            this->tttInbound.RemoveFpFromTheList(fp);
            continue;
        }

        auto position  = pos.GetPosition();
        auto pressAlt  = pos.GetPressureAltitude();
        auto heading   = pos.GetReportedHeading();
        bool isVfrByMe = fp.IsValid() && fp.GetFlightPlanData().GetPlanType() == std::string("V") && fp.GetTrackingControllerIsMe();

        for (auto& [icao, ap] : this->airports)
        {
            for (auto& [rwyKey, rwy] : ap.runways)
            {
                int         depElevation = (rwy.thresholdElevationFt > 0) ? rwy.thresholdElevationFt : ap.fieldElevation;
                std::string rwyCallsign  = callSign + rwy.designator;

                // ── (A) Runway occupancy ──
                // Inline check: replaces the old airports×runways×targets triple-nested walk.
                if (rwy.widthMeters > 0 && pressAlt <= depElevation + 80 && !this->ttt_runwayOccupied.contains(rwy.designator) && IsPositionOnRunway(rwy, ap.runways, position))
                {
                    this->ttt_runwayOccupied.insert(rwy.designator);
                }

                // ── Shared: runway heading number (pre-cached at config load) ──
                int arrRwyHdg = rwy.headingNumber;

                // ── (B) Main inbound tracking ──
                auto inboundIt      = this->ttt_inbound.find(callSign);
                bool trackedThisRwy = (inboundIt != this->ttt_inbound.end() && inboundIt->second.flightPlan.designator == rwy.designator);

                if (arrRwyHdg == -1)
                {
                    // Can't determine arrival heading — remove if tracked, then fall through to (C)
                    if (trackedThisRwy)
                    {
                        this->tttInbound.RemoveFpFromTheList(fp);
                        this->ttt_inbound.erase(callSign);
                        this->ttt_callSigns.erase(callSign);
                        this->ttt_recentlyRemoved[rwyCallsign] = GetTickCount64();
                    }
                    // Fall through to (C) — go-around check uses hdgDiff=0 for unknown headings
                }
                else
                {
                    double distance  = DistanceFromRunwayThreshold(rwy.designator, position, ap.runways);
                    double direction = DirectionFromRunwayThreshold(rwy.designator, position, ap.runways);
                    int    hdgDiff   = std::abs(heading - arrRwyHdg * 10);
                    if (hdgDiff > 180)
                        hdgDiff = 360 - hdgDiff;
                    double approachDir = std::fmod(arrRwyHdg * 10 + 180.0, 360.0);
                    double dirDiff     = std::abs(direction - approachDir);
                    if (dirDiff > 180.0)
                        dirDiff = 360.0 - dirDiff;

                    double dirLimit   = std::min(5.0 + distance * 0.7, 15.0);
                    double altProfile = depElevation + distance * 300.0 + 1000.0; // ~3.3° glidepath + 1000 ft buffer
                    if (pressAlt > depElevation + 50 && pressAlt < altProfile && hdgDiff <= 45 && distance < 25 && dirDiff <= dirLimit)
                    {
                        std::string trackingControllerId = fp.GetTrackingControllerId();

                        if ((fp.GetTrackingControllerIsMe() || trackingControllerId.empty()) && !this->standAssignment.contains(callSign))
                        {
                            // Trigger auto stand assignment in GroundRadar plugin
                            this->radarScreen->StartTagFunction(callSign.c_str(), "GRplugin", 0, "   Auto   ", GROUNDRADAR_PLUGIN_NAME, 2, POINT(), RECT());
                        }

                        if (!trackedThisRwy)
                        {
                            this->tttInbound.AddFpToTheList(fp);
                            TTTInboundState& state   = this->ttt_inbound[callSign];
                            state.flightPlan         = rwy;
                            state.distanceToRunway   = distance;
                            state.approachFixTracked = false;
                            state.approachPathIdx    = -1;
                            state.approachSegIdx     = -1;
                            state.goAroundTick       = 0;
                            this->ttt_callSigns.insert(callSign);
                            this->LogDebugMessage(callSign + " added to TTT (cone) rwy=" + rwy.designator + " dist=" + std::to_string(static_cast<int>(distance)) + "NM" + " alt=" + std::to_string(pressAlt) + "ft" + " hdgDiff=" + std::to_string(hdgDiff / 10) + "deg" + " dirDiff=" + std::to_string(static_cast<int>(dirDiff)) + "deg", "TTT");
                        }
                        else
                        {
                            inboundIt->second.distanceToRunway = distance;
                            inboundIt->second.frozenTick       = 0;
                            inboundIt->second.frozenTttStr     = {};
                            // Transition from approach-fix tracking to normal cone tracking
                            inboundIt->second.approachFixTracked = false;
                            inboundIt->second.approachPathIdx    = -1;
                            inboundIt->second.approachSegIdx     = -1;
                        }
                    }
                    else
                    {
                        const bool isFixTracked = trackedThisRwy && inboundIt->second.approachFixTracked;

                        if (isFixTracked)
                        {
                            // Aircraft is on a non-straight-in approach leg — keep in list and compute path distance
                            TTTInboundState& state   = inboundIt->second;
                            int              pathIdx = state.approachPathIdx;
                            int&             segIdx  = state.approachSegIdx;

                            if (pathIdx >= 0 && pathIdx < static_cast<int>(rwy.gpsApproachPaths.size()))
                            {
                                const auto& path = rwy.gpsApproachPaths[pathIdx];

                                // Advance segment when within 1.5 NM of the next fix
                                int nextFi = segIdx + 1;
                                if (nextFi < static_cast<int>(path.fixes.size()))
                                {
                                    EuroScopePlugIn::CPosition nextPos;
                                    nextPos.m_Latitude  = path.fixes[nextFi].lat;
                                    nextPos.m_Longitude = path.fixes[nextFi].lon;
                                    if (position.DistanceTo(nextPos) < 1.5)
                                        segIdx = nextFi;
                                }

                                // Compute remaining path distance from current position
                                int    curNext  = segIdx + 1;
                                double pathDist = 0.0;

                                if (curNext < static_cast<int>(path.fixes.size()))
                                {
                                    const auto& nextFix = path.fixes[curNext];
                                    if ((nextFix.legType == "arcLeft" || nextFix.legType == "arcRight") && nextFix.arcRadiusNm > 0.0)
                                    {
                                        // Remaining arc: r × angular distance from aircraft to end fix (in turn direction)
                                        double bearingToAC  = BearingBetween(nextFix.arcCenterLat, nextFix.arcCenterLon,
                                                                             position.m_Latitude, position.m_Longitude);
                                        double bearingToEnd = BearingBetween(nextFix.arcCenterLat, nextFix.arcCenterLon,
                                                                             nextFix.lat, nextFix.lon);
                                        double angDeg;
                                        if (nextFix.legType == "arcLeft")
                                        {
                                            // CCW: bearing decreases toward end
                                            angDeg = bearingToAC - bearingToEnd;
                                            if (angDeg < 0.0)
                                                angDeg += 360.0;
                                        }
                                        else
                                        {
                                            // CW: bearing increases toward end
                                            angDeg = bearingToEnd - bearingToAC;
                                            if (angDeg < 0.0)
                                                angDeg += 360.0;
                                        }
                                        pathDist = nextFix.arcRadiusNm * angDeg * std::numbers::pi / 180.0;
                                    }
                                    else
                                    {
                                        // Straight: direct distance to next fix
                                        EuroScopePlugIn::CPosition nextPos;
                                        nextPos.m_Latitude  = nextFix.lat;
                                        nextPos.m_Longitude = nextFix.lon;
                                        pathDist            = position.DistanceTo(nextPos);
                                    }

                                    // Add full lengths of all subsequent segments
                                    for (int fi = curNext + 1; fi < static_cast<int>(path.fixes.size()); ++fi)
                                        pathDist += path.fixes[fi].legLengthNm;

                                    // Add straight final from last approach fix to runway threshold
                                    EuroScopePlugIn::CPosition lastFixPos, threshPos;
                                    lastFixPos.m_Latitude  = path.fixes.back().lat;
                                    lastFixPos.m_Longitude = path.fixes.back().lon;
                                    threshPos.m_Latitude   = rwy.thresholdLat;
                                    threshPos.m_Longitude  = rwy.thresholdLon;
                                    pathDist += lastFixPos.DistanceTo(threshPos);
                                }
                                else
                                {
                                    // Past all approach fixes — on the straight final
                                    pathDist = distance;
                                }

                                state.distanceToRunway = pathDist;
                            }
                            else
                            {
                                state.distanceToRunway = distance;
                            }

                            // Safety valve: remove if impossibly far, too high, wrong heading/direction, or climbing
                            int prevAlt = rt.GetPreviousPosition(pos).GetPressureAltitude();
                            if (distance > 35.0 || pressAlt > depElevation + 8000 || hdgDiff > 120 || dirDiff > 60.0 || prevAlt < pressAlt - 200)
                            {
                                std::string why;
                                if (distance > 35.0)
                                    why += " dist>" + std::to_string(static_cast<int>(distance)) + "NM";
                                if (pressAlt > depElevation + 8000)
                                    why += " alt>" + std::to_string(pressAlt) + "ft";
                                if (hdgDiff > 120)
                                    why += " hdg=" + std::to_string(hdgDiff) + "deg";
                                if (dirDiff > 60.0)
                                    why += " dir=" + std::to_string(static_cast<int>(dirDiff)) + "deg";
                                if (prevAlt < pressAlt - 200)
                                    why += " climbing(" + std::to_string(prevAlt) + "->" + std::to_string(pressAlt) + "ft)";
                                this->LogDebugMessage(callSign + " removed from TTT (RNP safety) rwy=" + rwy.designator + why, "TTT");
                                this->tttInbound.RemoveFpFromTheList(fp);
                                this->ttt_clearedToLand.erase(callSign);
                                this->ttt_inbound.erase(callSign);
                                this->ttt_callSigns.erase(callSign);
                                this->ttt_recentlyRemoved[rwyCallsign] = GetTickCount64();
                            }
                        }
                        else if (!rwy.gpsApproachPaths.empty() && !trackedThisRwy && pressAlt > depElevation + 50 && pressAlt < depElevation + 8000 && hdgDiff <= 120 && dirDiff <= 45.0 && rt.GetPreviousPosition(pos).GetPressureAltitude() >= pressAlt)
                        {
                            // Approach-fix proximity detection for non-straight-in RNP approaches
                            bool added = false;
                            for (int pi = 0; pi < static_cast<int>(rwy.gpsApproachPaths.size()) && !added; ++pi)
                            {
                                const auto& path = rwy.gpsApproachPaths[pi];
                                for (int fi = 0; fi < static_cast<int>(path.fixes.size()) && !added; ++fi)
                                {
                                    const auto& fix = path.fixes[fi];
                                    if (fix.lat == 0.0 && fix.lon == 0.0)
                                        continue;

                                    EuroScopePlugIn::CPosition fixPos;
                                    fixPos.m_Latitude  = fix.lat;
                                    fixPos.m_Longitude = fix.lon;
                                    double fixDist     = position.DistanceTo(fixPos);

                                    bool altOk = (fix.altMinFt == 0 || pressAlt >= fix.altMinFt) && (fix.altMaxFt == 0 || pressAlt <= fix.altMaxFt);

                                    bool iafHdgOk = true;
                                    if (fix.iafHeading != 0)
                                    {
                                        int iafHdgDiff = std::abs(heading - fix.iafHeading * 10);
                                        if (iafHdgDiff > 1800)
                                            iafHdgDiff = 3600 - iafHdgDiff;
                                        iafHdgOk = iafHdgDiff <= 300;
                                    }

                                    if (fix.detectionRadiusNm > 0.0 && fixDist < fix.detectionRadiusNm && altOk && iafHdgOk)
                                    {
                                        std::string trackingControllerId = fp.GetTrackingControllerId();
                                        if ((fp.GetTrackingControllerIsMe() || trackingControllerId.empty()) && !this->standAssignment.contains(callSign))
                                        {
                                            this->radarScreen->StartTagFunction(callSign.c_str(), "GRplugin", 0,
                                                                                "   Auto   ", GROUNDRADAR_PLUGIN_NAME, 2, POINT(), RECT());
                                        }

                                        this->tttInbound.AddFpToTheList(fp);
                                        TTTInboundState& ns   = this->ttt_inbound[callSign];
                                        ns.flightPlan         = rwy;
                                        ns.distanceToRunway   = distance; // path dist computed next tick
                                        ns.goAroundTick       = 0;
                                        ns.approachFixTracked = true;
                                        ns.approachPathIdx    = pi;
                                        ns.approachSegIdx     = fi;
                                        this->ttt_callSigns.insert(callSign);
                                        this->LogDebugMessage(callSign + " added to TTT (RNP) rwy=" + rwy.designator + " approach=" + path.name + " fix=" + fix.name + " fixDist=" + std::to_string(fixDist).substr(0, 4) + "NM" + " alt=" + std::to_string(pressAlt) + "ft", "TTT");
                                        added = true;
                                    }
                                }
                            }
                        }
                        else
                        {
                            // Only process normal inbounds; go-arounds are handled in (C) below
                            if (trackedThisRwy && inboundIt->second.goAroundTick == 0)
                            {
                                TTTInboundState& st = inboundIt->second;

                                if (st.frozenTick != 0)
                                {
                                    // Already frozen — check 30-second timeout
                                    if ((GetTickCount64() - st.frozenTick) / 1000 >= 30)
                                    {
                                        this->LogDebugMessage(callSign + " removed from TTT (frozen timeout) rwy=" + rwy.designator, "TTT");
                                        this->tttInbound.RemoveFpFromTheList(fp);
                                        this->ttt_clearedToLand.erase(callSign);
                                        this->ttt_inbound.erase(callSign);
                                        this->ttt_callSigns.erase(callSign);
                                    }
                                    // else: still within 30 s window — keep displaying
                                }
                                else
                                {
                                    // First time leaving cone — decide: landing path or freeze
                                    std::string why;
                                    if (pressAlt <= depElevation + 50)
                                        why += " alt_low=" + std::to_string(pressAlt) + "ft";
                                    if (pressAlt >= altProfile)
                                        why += " alt_profile=" + std::to_string(pressAlt) + "ft(lim" + std::to_string(static_cast<int>(altProfile)) + ")";
                                    if (hdgDiff > 45)
                                        why += " hdg=" + std::to_string(hdgDiff) + "deg(lim45)";
                                    if (distance >= 25.0)
                                        why += " dist=" + std::to_string(static_cast<int>(distance)) + "NM(lim25)";
                                    if (dirDiff > dirLimit)
                                        why += " dir=" + std::to_string(static_cast<int>(dirDiff)) + "deg(lim" + std::to_string(static_cast<int>(dirLimit)) + ")";

                                    bool isLanding = pressAlt < depElevation + 125 && hdgDiff < 120;
                                    if (isLanding)
                                    {
                                        // Landing path: immediate removal + gndTransfer
                                        this->LogDebugMessage(callSign + " removed from TTT (cone left)" + why + " rwy=" + rwy.designator, "TTT");
                                        this->tttInbound.RemoveFpFromTheList(fp);
                                        this->ttt_clearedToLand.erase(callSign);
                                        this->ttt_inbound.erase(callSign);
                                        this->ttt_callSigns.erase(callSign);
                                        this->ttt_recentlyRemoved[rwyCallsign] = GetTickCount64();
                                        this->gndTransfer_list.insert(callSign);
                                    }
                                    else
                                    {
                                        // Freeze: stay in list for 5 s with "?mm:ss?" display
                                        int speed = rt.GetPosition().GetReportedGS();
                                        if (speed > 0)
                                        {
                                            int totalSec    = static_cast<int>((distance / speed) * 3600.0);
                                            st.frozenTttStr = std::format("{:02d}:{:02d}", totalSec / 60, totalSec % 60);
                                        }
                                        else
                                        {
                                            st.frozenTttStr = "--:--";
                                        }
                                        st.frozenTick                          = GetTickCount64();
                                        this->ttt_recentlyRemoved[rwyCallsign] = GetTickCount64();
                                        this->LogDebugMessage(callSign + " frozen in TTT (cone left)" + why + " rwy=" + rwy.designator, "TTT");
                                    }
                                }
                            }
                        }
                    }
                }

                // ── (C) Go-around / recently-removed lifecycle ──
                // Re-fetch inboundIt in case (B) erased or created the entry.
                inboundIt      = this->ttt_inbound.find(callSign);
                trackedThisRwy = (inboundIt != this->ttt_inbound.end() && inboundIt->second.flightPlan.designator == rwy.designator);

                if (!isVfrByMe)
                {
                    bool isGoAround        = trackedThisRwy && inboundIt->second.goAroundTick != 0;
                    bool isRecentlyRemoved = !isGoAround && this->ttt_recentlyRemoved.contains(rwyCallsign);

                    if (isGoAround || isRecentlyRemoved)
                    {
                        double distance = DistanceFromRunwayThreshold(rwy.designator, position, ap.runways);

                        if (isGoAround)
                        {
                            // Active go-around: remove if unconfirmed after 60 s, tag dropped, or outgoing handoff initiated.
                            // tagDropped/handoffInitiated are only meaningful if I was tracking when the go-around was detected.
                            // If I re-assume tracking after detection (e.g. approach handed back), treat that as confirmation:
                            // suppress the 60-s timeout and re-enable the tag-drop / handoff-initiated guards.
                            TTTInboundState& gaState = inboundIt->second;
                            if (!gaState.wasTrackedByMe && fp.GetTrackingControllerIsMe())
                            {
                                gaState.wasTrackedByMe    = true;
                                gaState.goAroundConfirmed = true;
                            }
                            bool unconfirmedTimeout = !gaState.goAroundConfirmed && (GetTickCount64() - gaState.goAroundTick) / 1000 > 60;
                            bool tagDropped         = gaState.wasTrackedByMe && !fp.GetTrackingControllerIsMe();
                            bool handoffInitiated   = gaState.wasTrackedByMe && fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED;
                            if (unconfirmedTimeout || tagDropped || handoffInitiated)
                            {
                                std::string why;
                                if (unconfirmedTimeout)
                                    why += " unconfirmed_timeout";
                                if (tagDropped)
                                    why += " tag_dropped";
                                if (handoffInitiated)
                                    why += " handoff_initiated";
                                this->LogDebugMessage(callSign + " removed from TTT (go-around cleared) rwy=" + rwy.designator + why, "TTT");
                                this->tttInbound.RemoveFpFromTheList(fp);
                                this->ttt_inbound.erase(callSign);
                                this->ttt_callSigns.erase(callSign);
                                this->ttt_clearedToLand.erase(callSign);
                            }
                            else
                            {
                                inboundIt->second.distanceToRunway = distance;
                            }
                        }
                        else
                        {
                            // Check for go-around: recently-removed aircraft that is climbing.
                            // Climbing is the only reliable signal; position relative to the opposite
                            // threshold is not — it was just an indirect proxy.
                            int  prevAlt        = rt.GetPreviousPosition(pos).GetPressureAltitude();
                            bool climbing       = prevAlt < pressAlt - 50;
                            bool detectGoAround = climbing && distance < 15.0 && pressAlt < depElevation + 3000;
                            if (detectGoAround && pressAlt > depElevation + 100)
                            {
                                this->ttt_clearedToLand.erase(callSign);
                                this->gndTransfer_list.erase(callSign);
                                this->gndTransfer_soundPlayed.erase(callSign);
                                if (this->radarScreen)
                                    this->radarScreen->gndTransferSquares.erase(callSign);
                                if (this->radarScreen)
                                    this->radarScreen->gndTransferSquareTimes.erase(callSign);
                                this->LogDebugMessage(callSign + " added to TTT (go-around) rwy=" + rwy.designator + " dist=" + std::to_string(static_cast<int>(distance)) + "NM" + " alt=" + std::to_string(pressAlt) + "ft", "TTT");
                                bool             wasTracked = trackedThisRwy; // frozen entries are already in ttt_inbound
                                TTTInboundState& ga         = this->ttt_inbound[callSign];
                                ga.flightPlan               = rwy;
                                ga.distanceToRunway         = distance;
                                ga.frozenTick               = 0;
                                ga.frozenTttStr             = {};
                                ga.goAroundTick             = GetTickCount64();
                                ga.goAroundConfirmed        = false;
                                ga.wasTrackedByMe           = fp.GetTrackingControllerIsMe();
                                ga.approachFixTracked       = false;
                                ga.approachPathIdx          = -1;
                                ga.approachSegIdx           = -1;
                                this->ttt_callSigns.insert(callSign);
                                if (!wasTracked)
                                    this->tttInbound.AddFpToTheList(fp);
                                this->ttt_recentlyRemoved.erase(rwyCallsign);
                            }
                        }
                    }
                }
            }
        }
    }

    // Rebuild sorted-by-runway index
    this->ttt_sortedByRunway.clear();
    for (auto& [cs, state] : this->ttt_inbound)
    {
        this->ttt_sortedByRunway[state.flightPlan.designator].push_back(cs);
    }
    for (auto& [designator, callSigns] : this->ttt_sortedByRunway)
    {
        std::sort(callSigns.begin(), callSigns.end(), [this](const std::string& a, const std::string& b)
                  { return this->ttt_inbound.at(a).distanceToRunway < this->ttt_inbound.at(b).distanceToRunway; });
    }
}

/// @brief Records today's UTC date as the last NAP acknowledgement date and persists it to settings.
void CFlowX_Timers::AckNapReminder()
{
    this->napLastDismissedDate = UtcDateString();
    this->SaveSettings();
}

/// @brief Syncs on-screen window positions into the settings layer and persists them.
void CFlowX_Timers::SaveWindowPositions()
{
    if (this->radarScreen == nullptr)
    {
        return;
    }

    this->approachEstWindowX = this->radarScreen->approachEstWindowPos.x;
    this->approachEstWindowY = this->radarScreen->approachEstWindowPos.y;
    this->approachEstWindowW = this->radarScreen->approachEstWindowW;
    this->approachEstWindowH = this->radarScreen->approachEstWindowH;
    this->depRateWindowX     = this->radarScreen->depRateWindowPos.x;
    this->depRateWindowY     = this->radarScreen->depRateWindowPos.y;
    this->twrOutboundWindowX = this->radarScreen->twrOutboundWindowPos.x;
    this->twrOutboundWindowY = this->radarScreen->twrOutboundWindowPos.y;
    this->twrInboundWindowX  = this->radarScreen->twrInboundWindowPos.x;
    this->twrInboundWindowY  = this->radarScreen->twrInboundWindowPos.y;
    this->napWindowX         = this->radarScreen->napWindowPos.x;
    this->napWindowY         = this->radarScreen->napWindowPos.y;
    this->weatherWindowX     = this->radarScreen->weatherWindowPos.x;
    this->weatherWindowY     = this->radarScreen->weatherWindowPos.y;

    this->SaveSettings();
}

/// @brief Clears all unacknowledged change flags for the given airport.
void CFlowX_Timers::AckWeather(const std::string& icao)
{
    this->windUnacked.erase(icao);
    this->qnhUnacked.erase(icao);
    this->atisUnacked.erase(icao);
    this->rvrUnacked.erase(icao);
}

void CFlowX_Timers::ClearGndTransfer(const std::string& callsign)
{
    this->gndTransfer_list.erase(callsign);
    this->gndTransfer_soundPlayed.erase(callsign);
}

/// @brief Symmetric push/drain sampler for the tail-dot history ring buffers.
/// Runs once per second from CFlowX::OnTimer. Moved targets grow their trail; stationary
/// targets lose one sample per tick so a parked aircraft fades to nothing.
void CFlowX_Timers::UpdateGndTailHistory()
{
    const int maxDots = this->gndTailDotCount;
    if (maxDots <= 0)
    {
        if (!this->gndTailHistory.empty())
            this->gndTailHistory.clear();
        return;
    }

    std::set<std::string> live;
    for (auto rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
    {
        if (!rt.GetPosition().IsValid())
            continue;
        std::string cs = rt.GetCallsign();
        if (cs.empty())
            continue;
        live.insert(cs);

        auto     curEs = rt.GetPosition().GetPosition();
        GeoPoint cur   = {curEs.m_Latitude, curEs.m_Longitude};
        auto&    buf   = this->gndTailHistory[cs];

        // "Moving" is decided by reported ground speed, not inter-tick position delta: VATSIM
        // sends position packets every ~5 s, so between packets the position is identical
        // tick-to-tick even though the aircraft is actively taxiing. Draining on identical
        // positions would empty the trail every second.
        const int  gs     = rt.GetPosition().GetReportedGS();
        const bool moving = gs > 1;

        bool posChanged = true;
        if (!buf.empty())
        {
            EuroScopePlugIn::CPosition lastEs;
            lastEs.m_Latitude  = buf.back().lat;
            lastEs.m_Longitude = buf.back().lon;
            // DistanceTo returns NM; ~3 m ≈ 0.0016 NM. Guards against logging duplicate
            // positions when no new packet has arrived since the last tick.
            posChanged = curEs.DistanceTo(lastEs) > 0.0016;
        }

        if (moving)
        {
            if (posChanged)
            {
                buf.push_back(cur);
                while ((int)buf.size() > maxDots)
                    buf.pop_front();
            }
            // Moving but same position as last tick: keep trail, wait for next position packet.
        }
        else if (!buf.empty())
        {
            // Parked / very slow: drain one dot per second so the trail visibly fades away.
            buf.pop_front();
            if (buf.empty())
                this->gndTailHistory.erase(cs);
        }
    }

    // Drop entries for callsigns that no longer have a valid radar target.
    for (auto it = this->gndTailHistory.begin(); it != this->gndTailHistory.end();)
    {
        if (live.find(it->first) == live.end())
            it = this->gndTailHistory.erase(it);
        else
            ++it;
    }
}

void CFlowX_Timers::AssignHoldingPoint(EuroScopePlugIn::CFlightPlan& fp, const std::string& hpName)
{
    std::string callSign = fp.GetCallsign();
    this->flightStripAnnotation[callSign] =
        AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], hpName);
    fp.GetControllerAssignedData().SetFlightStripAnnotation(
        8, this->flightStripAnnotation[callSign].c_str());
    this->PushToOtherControllers(fp);
}