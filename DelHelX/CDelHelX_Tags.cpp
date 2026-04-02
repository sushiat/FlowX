#include "pch.h"
#include "CDelHelX_Tags.h"

#include "helpers.h"

/// @brief Builds the TWR next-frequency tag for a departing aircraft.
/// @param fp Flight plan being evaluated.
/// @param rt Correlated radar target.
/// @return tagInfo with the "->frequency" string and urgency colour, or empty if not applicable.
tagInfo CDelHelX_Tags::GetTwrNextFreqTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
    tagInfo tag;
    tag.color = TAG_COLOR_DEFAULT_GRAY;

    auto fpd = fp.GetFlightPlanData();
    std::string dep = fpd.GetOrigin();
    to_upper(dep);
    auto airport = this->airports.find(dep);
    if (airport == this->airports.end())
    {
        // Airport not in config, so ignore it
        return tag;
    }

    std::string groundState = fp.GetGroundState();
    bool transferred = false;
    if (groundState == "TAXI" || groundState == "DEPA")
    {
        // Check if we passed the aircraft to the next frequency, clear the tag if we did
        std::string callSign = fp.GetCallsign();
        this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
        if (this->flightStripAnnotation[callSign].length() >= 7)
        {
            std::string storedFreq = this->flightStripAnnotation[callSign].substr(1, 6);
            if (storedFreq.find_first_not_of(' ') != std::string::npos)
            {
                double myFreqDouble = this->ControllerMyself().GetPrimaryFrequency();
                auto s = std::to_string(myFreqDouble);
                std::string myFreq = s.substr(0, s.find('.') + 4);
                myFreq.erase(std::remove(myFreq.begin(), myFreq.end(), '.'), myFreq.end());
                transferred = (storedFreq != myFreq);
            }
        }

        if (this->radarScreen == nullptr)
        {
            return tag;
        }

        auto me = this->ControllerMyself();
        if (me.IsController() && me.GetRating() > 1 && me.GetFacility() >= 3 && me.GetFacility() <= 4)
        {
            // Only show tower to ground, but not to tower
            if (me.GetFacility() == 3) {
                bool towerOnline = false;
                for (auto station : this->radarScreen->towerStations)
                {
                    if (station.first.find(dep) != std::string::npos)
                    {
                        towerOnline = true;
                        continue;
                    }
                }

                if (towerOnline || this->towerOverride)
                {
                    std::string rwy = fpd.GetDepartureRwy();
                    double distToThreshold = DistanceFromRunwayThreshold(rwy, rt.GetPosition().GetPosition(), airport->second.runways);
                    bool nearThreshold = distToThreshold < 0.2;

                    bool atHoldingPoint = false;
                    auto rwyIt = airport->second.runways.find(rwy);
                    if (rwyIt != airport->second.runways.end())
                    {
                        for (auto& hp : rwyIt->second.holdingPoints)
                        {
                            u_int corners = static_cast<u_int>(hp.second.lat.size());
                            double polyX[10], polyY[10];
                            std::copy(hp.second.lon.begin(), hp.second.lon.end(), polyX);
                            std::copy(hp.second.lat.begin(), hp.second.lat.end(), polyY);
                            if (PointInsidePolygon(static_cast<int>(corners), polyX, polyY, rt.GetPosition().GetPosition().m_Longitude, rt.GetPosition().GetPosition().m_Latitude))
                            {
                                atHoldingPoint = true;
                                break;
                            }
                        }
                    }

                    if (rwyIt != airport->second.runways.end())
                    {
                        if (!transferred)
                        {
                            tag.color = nearThreshold || atHoldingPoint ? (this->blinking ? TAG_COLOR_YELLOW : TAG_COLOR_WHITE) : TAG_COLOR_WHITE;
                        }
                        tag.tag = "->" + rwyIt->second.twrFreq;
                        return tag;
                    }

                    // No runway assigned and VFR? Use default TWR freq
                    if (rwy=="" && fp.GetFlightPlanData().GetPlanType() == "V")
                    {
                        tag.tag = "->" + airport->second.runways.begin()->second.twrFreq;
                        return tag;
                    }
                }
            }

            // Not squawking mode-C, don't show next freq
            if (!rt.GetPosition().GetTransponderC() && rt.GetPosition().GetPressureAltitude() > (airport->second.fieldElevation + 50))
            {
                tag.tag = "!MODE-C";
                tag.color = this->blinking ? TAG_COLOR_RED : TAG_COLOR_ORANGE;
                return tag;
            }

            // We are TWR, find APP or CTR freq, plus more color coding
            if (!transferred)
            {
                tag.color = TAG_COLOR_WHITE;
                if (rt.GetPosition().GetPressureAltitude() >= airport->second.airborneTransfer && rt.GetPosition().GetPressureAltitude() < airport->second.airborneTransferWarning)
                {
                    tag.color = TAG_COLOR_TURQ;
                }
                else if (rt.GetPosition().GetPressureAltitude() >= airport->second.airborneTransferWarning)
                {
                    tag.color = this->blinking ? TAG_COLOR_ORANGE : TAG_COLOR_TURQ;
                }
            }

            // Determine target approach frequency from SID or use the default
            std::string targetFreq = airport->second.defaultAppFreq;
            {
                std::string sid = fpd.GetSidName();
                for (auto& [f, sids] : airport->second.sidAppFreqs)
                {
                    if (std::find(sids.begin(), sids.end(), sid) != sids.end())
                    {
                        targetFreq = f;
                        break;
                    }
                }
            }

            // If any online approach station is on one of this airport's approach frequencies, show the SID-determined frequency
            for (const auto& station : this->radarScreen->approachStations)
            {
                if (airport->second.appFreqFallbacks.count(station.second))
                {
                    tag.tag = "->" + targetFreq;
                    return tag;
                }
            }

            // No approach station: try centre frequencies in config-defined priority order
            for (const auto& ctrFreq : airport->second.ctrStations)
            {
                for (const auto& station : this->radarScreen->centerStations)
                {
                    if (station.second == ctrFreq)
                    {
                        tag.tag = "->" + ctrFreq;
                        return tag;
                    }
                }
            }

            // Nothing online, UNICOM
            tag.tag = "->122.8";
            return tag;
        }
    }

    // Not taxiing or taking off, ignore it
    return tag;
}

/// @brief Builds the Push+Start helper tag with the next applicable frequency or a validation error code.
/// @param fp Flight plan being evaluated.
/// @param rt Correlated radar target.
/// @return tagInfo with frequency string and colour, or an error code in red.
tagInfo CDelHelX_Tags::GetPushStartHelperTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
    tagInfo tag;
    tag.color = TAG_COLOR_GREEN;

    auto fpd = fp.GetFlightPlanData();
    std::string dep = fpd.GetOrigin();
    to_upper(dep);
    auto airport = this->airports.find(dep);
    if (airport == this->airports.end())
    {
        // Airport not in config, so ignore it
        return tag;
    }

    std::string groundState = fp.GetGroundState();
    if (!groundState.empty())
    {
        // Aircraft is now moving, so we can remove the tag
        return tag;
    }

    std::string rwy = fpd.GetDepartureRwy();
    if (!this->noChecks && rwy.empty())
    {
        tag.tag = "!RWY";
        tag.color = TAG_COLOR_RED;

        return tag;
    }

    auto cad = fp.GetControllerAssignedData();
    std::string assignedSquawk = cad.GetSquawk();
    std::string currentSquawk = rt.GetPosition().GetSquawk();

    if (this->noChecks && assignedSquawk.empty())
    {
        assignedSquawk = "2000";
    }

    if (assignedSquawk.empty())
    {
        tag.tag = "!ASSR";
        tag.color = TAG_COLOR_RED;

        return tag;
    }

    bool clearanceFlag = fp.GetClearenceFlag();
    if (!this->noChecks && !clearanceFlag)
    {
        tag.tag = "!CLR";
        tag.color = TAG_COLOR_RED;

        return tag;
    }

    if (assignedSquawk != currentSquawk)
    {
        tag.tag = assignedSquawk;
        tag.color = TAG_COLOR_ORANGE;
    }

    // If I'm a controller, not Observer(1) and facility at least GND(3), I can push and start aircraft myself
    auto me = this->ControllerMyself();
    if (me.IsController() && me.GetRating() > 1 && me.GetFacility() >= 3)
    {
        tag.tag.empty() ? tag.tag = "OK" : tag.tag += "->OK";
        return tag;
    }

    if (this->radarScreen == nullptr)
    {
        return tag;
    }

    bool groundOnline = false;
    for (auto station : this->radarScreen->groundStations)
    {
        if (station.first.find(dep) != std::string::npos)
        {
            groundOnline = true;
            continue;
        }
    }

    if (groundOnline || this->groundOverride)
    {
        EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();
        for (auto& geoGnd : airport->second.geoGndFreq)
        {
            u_int corners = geoGnd.second.lat.size();
            double lat[10], lon[10];
            std::copy(geoGnd.second.lat.begin(), geoGnd.second.lat.end(), lat);
            std::copy(geoGnd.second.lon.begin(), geoGnd.second.lon.end(), lon);

            if (PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
            {
                tag.tag += "->" + geoGnd.second.freq;
                return tag;
            }
        }

        // Didn't find any geo-based GND, so return default
        tag.tag += "->" + airport->second.gndFreq;
        return tag;
    }

    bool towerOnline = false;
    for (auto station : this->radarScreen->towerStations)
    {
        if (station.first.find(dep) != std::string::npos)
        {
            towerOnline = true;
            continue;
        }
    }

    if (towerOnline || this->towerOverride)
    {
        auto rwyIt = airport->second.runways.find(rwy);
        if (rwyIt != airport->second.runways.end())
        {
            tag.tag += "->" + rwyIt->second.twrFreq;
        }
        return tag;
    }

    // Determine target approach frequency from SID or use the default
    std::string targetFreq = airport->second.defaultAppFreq;
    {
        std::string sid = fpd.GetSidName();
        for (auto& [f, sids] : airport->second.sidAppFreqs)
        {
            if (std::find(sids.begin(), sids.end(), sid) != sids.end())
            {
                targetFreq = f;
                break;
            }
        }
    }

    // If any online approach station is on one of this airport's approach frequencies, show the SID-determined frequency
    for (const auto& station : this->radarScreen->approachStations)
    {
        if (airport->second.appFreqFallbacks.count(station.second))
        {
            tag.tag += "->" + targetFreq;
            return tag;
        }
    }

    // No approach station: try centre frequencies in config-defined priority order
    for (const auto& ctrFreq : airport->second.ctrStations)
    {
        for (const auto& station : this->radarScreen->centerStations)
        {
            if (station.second == ctrFreq)
            {
                tag.tag += "->" + ctrFreq;
                return tag;
            }
        }
    }

    // Nothing online, UNICOM
    tag.tag += "->122.8";
    return tag;
}

/// @brief Builds the taxi-out indicator tag ("T" for taxi-out stand, "P" for push stand).
/// @param fp Flight plan being evaluated.
/// @param rt Correlated radar target.
/// @return tagInfo with "T", "P", or empty text.
tagInfo CDelHelX_Tags::GetTaxiOutTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
    tagInfo tag;

    EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
    std::string dep = fpd.GetOrigin();
    to_upper(dep);

    auto airport = this->airports.find(dep);
    if (airport == this->airports.end())
    {
        return tag;
    }

    EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();
    std::string groundState = fp.GetGroundState();

    if (groundState.empty() || groundState == "STUP")
    {
        bool isTaxiOut = false;
        for (auto& taxiOut : airport->second.taxiOutStands)
        {
            u_int corners = taxiOut.second.lat.size();
            double lat[10], lon[10];
            std::copy(taxiOut.second.lat.begin(), taxiOut.second.lat.end(), lat);
            std::copy(taxiOut.second.lon.begin(), taxiOut.second.lon.end(), lon);

            if (CDelHelX_Tags::PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
            {
                isTaxiOut = true;
                continue;
            }
        }

        if (isTaxiOut)
        {
            tag.tag = "T";
            tag.color = TAG_COLOR_GREEN;
        }
        else
        {
            tag.tag = groundState.empty() ? "P" : "";
        }
    }

    return tag;
}

/// @brief Builds the new-QNH tag showing orange "X" when annotation slot 8 contains the 'Q' flag.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with "X" in orange when a new QNH is pending, or empty.
tagInfo CDelHelX_Tags::GetNewQnhTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;
    std::string callSign = fp.GetCallsign();

    EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
    this->flightStripAnnotation[callSign] = fpcad.GetFlightStripAnnotation(8);
    if (!this->flightStripAnnotation[callSign].empty() && this->flightStripAnnotation[callSign][0] == 'Q')
    {
        tag.tag = "X";
        tag.color = TAG_COLOR_ORANGE;
    }
    else
    {
        tag.tag = "";
    }

    return tag;
}

/// @brief Builds the same-SID tag showing the SID name colour-coded by its configured group.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with the SID name and group colour, greyed out once the aircraft has departed.
tagInfo CDelHelX_Tags::GetSameSidTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;

    EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
    std::string dep = fpd.GetOrigin();
    to_upper(dep);
    std::string rwy = fpd.GetDepartureRwy();
    std::string sid = fpd.GetSidName();

    auto airport = this->airports.find(dep);
    if (airport == this->airports.end())
    {
        return tag;
    }

    if (!sid.empty() && sid.length() > 2) {
        auto sidKey = sid.substr(0, sid.length() - 2);
        auto sidDesignator = sid.substr(sid.length() - 2);

        tag.color = TAG_COLOR_WHITE;

        // Extend night SIDs
        auto nightIt = airport->second.nightTimeSids.find(sidKey);
        if (nightIt != airport->second.nightTimeSids.end())
        {
            sid = nightIt->second + sidDesignator + "*";
        }

        tag.tag = sid;

        auto rwyIt = airport->second.runways.find(rwy);
        if (rwyIt != airport->second.runways.end())
        {
            auto colorIt = rwyIt->second.sidColors.find(sidKey);
            if (colorIt != rwyIt->second.sidColors.end())
            {
                tag.color = ColorFromString(colorIt->second);
            }
        }

        // Override to dark gray if the aircraft has already departed
        std::string callSign = fp.GetCallsign();
        auto depIt = this->twrSameSID_flightPlans.find(callSign);
        if (depIt != this->twrSameSID_flightPlans.end() && depIt->second > 0)
        {
            tag.color = TAG_COLOR_DARKGREY;
        }
    }

    return tag;
}

/// @brief Builds the takeoff-spacing tag showing time or distance separation from the previous departure.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with spacing text and a colour indicating separation compliance.
tagInfo CDelHelX_Tags::GetTakeoffSpacingTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;

    EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
    std::string dep = fpd.GetOrigin();
    to_upper(dep);
    std::string callSign = fp.GetCallsign();
    std::string rwy = fpd.GetDepartureRwy();
    auto airport = this->airports.find(dep);
    if (airport == this->airports.end())
    {
        return tag;
    }

    if (this->twrSameSID_flightPlans.find(callSign) == this->twrSameSID_flightPlans.end() || this->twrSameSID_flightPlans.at(callSign) == 0)
    {
        return tag;
    }

    auto offsetIt = this->dep_prevTakeoffOffset.find(callSign);
    if (offsetIt == this->dep_prevTakeoffOffset.end())
    {
        tag.tag = "---";
        return tag;
    }

    // Determine whether lighter follows heavier (time-based) or not (distance-based)
    bool timeBased = false;
    auto prevWtcIt = this->dep_prevWtc.find(callSign);
    if (prevWtcIt != this->dep_prevWtc.end())
    {
        char curWtc = fp.GetFlightPlanData().GetAircraftWtc();
        if (GetAircraftWeightCategoryRanking(curWtc) < GetAircraftWeightCategoryRanking(prevWtcIt->second))
        {
            timeBased = true;
        }
    }

    if (timeBased)
    {
        ULONGLONG offsetSeconds = offsetIt->second;
        auto reqIt = this->dep_timeRequired.find(callSign);
        int timeRequired = reqIt != this->dep_timeRequired.end() ? reqIt->second : 120;
        std::string valStr = std::to_string(offsetSeconds);
        valStr = std::string(4 - std::min((size_t)4, valStr.size()), ' ') + valStr;
        tag.tag = valStr + " s  /" + std::to_string(timeRequired);

        if (offsetSeconds >= (ULONGLONG)timeRequired)
        {
            tag.color = TAG_COLOR_GREEN;
        }
        else if (offsetSeconds >= (ULONGLONG)(timeRequired - 15))
        {
            tag.color = TAG_COLOR_YELLOW;
        }
        else
        {
            tag.color = TAG_COLOR_RED;
        }
    }
    else
    {
        auto lockedDistIt = this->dep_prevDistanceAtTakeoff.find(callSign);
        if (lockedDistIt == this->dep_prevDistanceAtTakeoff.end())
        {
            tag.tag = "---";
            return tag;
        }

        double dist = lockedDistIt->second;
        std::string num_text = std::to_string(dist);
        std::string rounded = num_text.substr(0, num_text.find('.') + 2);

        double distRequired = 3.0;
        std::string curSid = fp.GetFlightPlanData().GetSidName();
        auto prevSidIt = this->dep_prevSid.find(callSign);
        if (prevSidIt != this->dep_prevSid.end() && !prevSidIt->second.empty() && !curSid.empty()
            && prevSidIt->second.length() > 2 && curSid.length() > 2)
        {
            auto prevSidKey = prevSidIt->second.substr(0, prevSidIt->second.length() - 2);
            auto curSidKey = curSid.substr(0, curSid.length() - 2);

            auto rwyIt = airport->second.runways.find(rwy);
            if (rwyIt != airport->second.runways.end())
            {
                auto& sidGroupsMap = rwyIt->second.sidGroups;
                auto prevGroupIt = sidGroupsMap.find(prevSidKey);
                auto curGroupIt = sidGroupsMap.find(curSidKey);
                if (prevGroupIt != sidGroupsMap.end() && curGroupIt != sidGroupsMap.end()
                    && prevGroupIt->second == curGroupIt->second)
                {
                    distRequired = 5.0;
                }
            }
        }

        if (rounded.size() < 4)
        {
            rounded = std::string(4 - rounded.size(), ' ') + rounded;
        }
        tag.tag = rounded + " nm /" + std::to_string(static_cast<int>(distRequired));

        if (dist >= distRequired)
        {
            tag.color = TAG_COLOR_GREEN;
        }
        else if (dist >= distRequired - 0.3)
        {
            tag.color = TAG_COLOR_YELLOW;
        }
        else
        {
            tag.color = TAG_COLOR_RED;
        }
    }

    return tag;
}

/// @brief Builds the assigned runway tag showing the flight-plan departure runway designator.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with the runway designator string.
tagInfo CDelHelX_Tags::GetAssignedRunwayTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;

    EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
    std::string rwy = fpd.GetDepartureRwy();

    // Trim whitespace
    rwy.erase(rwy.begin(), std::find_if(rwy.begin(), rwy.end(), [](unsigned char c) { return !std::isspace(c); }));
    rwy.erase(std::find_if(rwy.rbegin(), rwy.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), rwy.end());

    if (rwy.empty())
    {
        tag.tag   = "??";
        tag.color = this->blinking ? TAG_COLOR_ORANGE : TAG_COLOR_TURQ;
        return tag;
    }

    tag.tag = rwy;
    return tag;
}

/// @brief Builds the assigned arrival runway tag showing the flight-plan arrival runway, including a red/yellow warning if it doesn't match current inbound runway.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with the runway designator string.
tagInfo CDelHelX_Tags::GetAssignedArrivalRwyTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;
    EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
    std::string rwy = fpd.GetArrivalRwy();
    tag.tag = rwy;

    // If the aircraft is on the TTT inbound list but assigned to a different runway, warn with red/yellow blinking
    std::string callSign = fp.GetCallsign();
    auto it = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
        [&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
    if (it != this->ttt_flightPlans.end() && it->second.designator != rwy)
    {
        tag.color = this->blinking ? TAG_COLOR_RED : TAG_COLOR_YELLOW;
    }

    return tag;
}

/// @brief Builds the TTT (time-to-touchdown) tag for an inbound aircraft.
/// @param fp Flight plan being evaluated.
/// @param rt Correlated radar target.
/// @return tagInfo with "RWY_MM:SS" string and urgency colour; go-arounds show a negative elapsed counter.
tagInfo CDelHelX_Tags::GetTttTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
    tagInfo tag;
    std::string callSign = fp.GetCallsign();

    auto it = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
        [&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
    if (it != this->ttt_flightPlans.end())
    {
        // Go-around: show runway go-around frequency, blink red/yellow
        auto goAroundIt = this->ttt_goAround.find(it->first);
        if (goAroundIt != this->ttt_goAround.end())
        {
            tag.tag = it->second.designator + "_->" + it->second.goAroundFreq;
            tag.color = this->blinking ? TAG_COLOR_RED : TAG_COLOR_YELLOW;
            return tag;
        }

        auto position = rt.GetPosition().GetPosition();
        auto speed = rt.GetGS();

        EuroScopePlugIn::CPosition rwyThreshold;
        rwyThreshold.m_Latitude = it->second.thresholdLat;
        rwyThreshold.m_Longitude = it->second.thresholdLon;

        // Calculate TTT based on current position/speed and runway distance
        if (speed > 0)
        {
            double distanceNm = position.DistanceTo(rwyThreshold);
            int totalSeconds = static_cast<int>((distanceNm / speed) * 3600.0);
            int mm = totalSeconds / 60;
            int ss = totalSeconds % 60;
            char buf[16];
            (void)sprintf_s(buf, "%s_%02d:%02d", it->second.designator.c_str(), mm, ss);
            tag.tag = std::string(buf);
            if (totalSeconds > 120)
            {
                tag.color = TAG_COLOR_GREEN;
            }
            else if (totalSeconds > 60)
            {
                tag.color = TAG_COLOR_YELLOW;
            }
            else
            {
                tag.color = TAG_COLOR_RED;
            }
        }
    }

    return tag;
}

/// @brief Builds the inbound NM tag showing distance to threshold, or gap to the leading inbound.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with absolute NM for the nearest inbound, or "+X.X" gap with colour coding for others.
tagInfo CDelHelX_Tags::GetInboundNmTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;
    std::string callSign = fp.GetCallsign();

    auto myIt = std::find_if(this->ttt_distanceToRunway.begin(), this->ttt_distanceToRunway.end(),
        [&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
    auto myPlan = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
        [&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });

    if (myIt != this->ttt_distanceToRunway.end() && myPlan != this->ttt_flightPlans.end())
    {
        // Go-around: always show absolute distance from original runway threshold
        if (this->ttt_goAround.find(myIt->first) != this->ttt_goAround.end())
        {
            char buf[16] = {};
            (void)sprintf_s(buf, "%.1f", myIt->second);
            tag.tag = std::string(buf);
            return tag;
        }

        auto sortedIt = this->ttt_sortedByRunway.find(myPlan->second.designator);
        if (sortedIt != this->ttt_sortedByRunway.end())
        {
            const auto& keys = sortedIt->second;
            char buf[16] = {};
            if (keys.front() == myIt->first)
            {
                (void)sprintf_s(buf, "%.1f", myIt->second);
            }
            else
            {
                for (size_t i = 1; i < keys.size(); ++i)
                {
                    if (keys[i] == myIt->first)
                    {
                        auto leaderIt = this->ttt_distanceToRunway.find(keys[i - 1]);
                    if (leaderIt == this->ttt_distanceToRunway.end())
                    {
                        break; // stale index, bail
                    }
                    double gap = myIt->second - leaderIt->second;
                    (void)sprintf_s(buf, "+%.1f", gap);
                    if (gap > 3.0)
                    {
                        tag.color = TAG_COLOR_GREEN;
                    }
                    else if (gap > 2.5)
                    {
                        tag.color = TAG_COLOR_YELLOW;
                    }
                    else
                    {
                        tag.color = TAG_COLOR_RED;
                    }
                    break;
                    }
                }
            }
            tag.tag = std::string(buf);
        }
    }

    return tag;
}

/// @brief Builds the suggested runway vacate point tag based on assigned stand and gap to the following inbound.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with the vacate point name in white, or empty if no match is found.
tagInfo CDelHelX_Tags::GetSuggestedVacateTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;
    std::string callSign = fp.GetCallsign();

    auto standIt = this->standAssignment.find(callSign);
    if (standIt == this->standAssignment.end())
    {
        return tag;
    }

    std::string stand = standIt->second;

    // Find this aircraft in the sorted inbound list
    auto myPlan = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
        [&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
    if (myPlan == this->ttt_flightPlans.end())
    {
        return tag;
    }

    auto sortedIt = this->ttt_sortedByRunway.find(myPlan->second.designator);
    if (sortedIt == this->ttt_sortedByRunway.end())
    {
        return tag;
    }

    const auto& keys = sortedIt->second;
    auto myIdx = std::find(keys.begin(), keys.end(), myPlan->first);
    if (myIdx == keys.end())
    {
        return tag;
    }

    // Calculate gap to follower if one exists
    bool hasFollower = myIdx + 1 != keys.end();
    double gap = 0.0;
    if (hasFollower)
    {
        auto followerIt = this->ttt_distanceToRunway.find(*(myIdx + 1));
        auto selfIt = this->ttt_distanceToRunway.find(myPlan->first);
        if (followerIt == this->ttt_distanceToRunway.end() || selfIt == this->ttt_distanceToRunway.end())
        {
            return tag; // stale sortedByRunway index, bail until next rebuild
        }
        gap = followerIt->second - selfIt->second;
    }

    // Look up runway vacate config
    std::string arr = fp.GetFlightPlanData().GetDestination();
    to_upper(arr);
    auto airportIt = this->airports.find(arr);
    if (airportIt == this->airports.end())
    {
        return tag;
    }

    auto rwyIt = airportIt->second.runways.find(myPlan->second.designator);
    if (rwyIt == airportIt->second.runways.end())
    {
        return tag;
    }

    for (auto& [vpName, vp] : rwyIt->second.vacatePoints)
    {
        if (hasFollower && gap < vp.minGap)
        {
            continue;
        }

        for (auto& pattern : vp.stands)
        {
            bool matched = false;
            if (pattern == "*")
            {
                matched = true;
            }
            else if (pattern.back() == '*')
            {
                std::string prefix = pattern.substr(0, pattern.size() - 1);
                to_upper(prefix);
                matched = stand.rfind(prefix, 0) == 0;
            }
            else
            {
                std::string p = pattern;
                to_upper(p);
                matched = stand == p;
            }

            if (matched)
            {
                tag.tag = vpName;
                tag.color = TAG_COLOR_WHITE;
                return tag;
            }
        }
    }

    return tag;
}

/// @brief Builds the holding-point tag from flight-strip annotation slot 8.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with the HP name: green for assigned, orange for requested (*), grey after departure.
tagInfo CDelHelX_Tags::GetHoldingPointTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;
    std::string callSign = fp.GetCallsign();
    this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
    if (this->flightStripAnnotation[callSign].length() <= 7)
    {
        return tag;
    }
    tag.tag = this->flightStripAnnotation[callSign].substr(7);
    tag.color = TAG_COLOR_GREEN;
    if (tag.tag.find('*') != std::string::npos)
    {
        tag.color = TAG_COLOR_ORANGE;
    }
    if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
    {
        tag.color = TAG_COLOR_DARKGREY;
    }
    return tag;
}

/// @brief Builds the departure-info tag summarising readiness to depart relative to the previous departure.
/// @param fp Flight plan being evaluated.
/// @param rt Correlated radar target.
/// @return tagInfo with readiness text and colour (green=OK, yellow=almost, red=not yet).
tagInfo CDelHelX_Tags::GetDepartureInfoTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
    tagInfo tag;
    std::string callSign = fp.GetCallsign();

    try
    {
        std::string depAirport = fp.GetFlightPlanData().GetOrigin();
        to_upper(depAirport);
        auto airport = this->airports.find(depAirport);
        if (airport == this->airports.end())
        {
            return tag;
        }
        std::string groundState = fp.GetGroundState();
        if (groundState == "TAXI" || groundState == "DEPA")
        {
            std::string rwy = fp.GetFlightPlanData().GetDepartureRwy();
            if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) == 0)
            {
                std::string lastDeparted_callSign;
                if (this->twrSameSID_lastDeparted.find(rwy) != this->twrSameSID_lastDeparted.end())
                {
                    lastDeparted_callSign = this->twrSameSID_lastDeparted[rwy];
                }

                if (!lastDeparted_callSign.empty())
                {
                    bool lastDeparted_active = false;
                    EuroScopePlugIn::CRadarTarget lastDeparted_radarTarget = this->RadarTargetSelect(lastDeparted_callSign.c_str());
                    if (lastDeparted_radarTarget.IsValid())
                    {
                        lastDeparted_active = true;
                    }

                    if (lastDeparted_active)
                    {
                        char departedWtc = lastDeparted_radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetAircraftWtc();
                        char wtc = fp.GetFlightPlanData().GetAircraftWtc();

                        if (GetAircraftWeightCategoryRanking(departedWtc) > GetAircraftWeightCategoryRanking(wtc))
                        {
                            // Time based
                            unsigned long secondsRequired = 120;

                            this->flightStripAnnotation[lastDeparted_callSign] = lastDeparted_radarTarget.GetCorrelatedFlightPlan().GetControllerAssignedData().GetFlightStripAnnotation(8);
                            this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
                            if (this->flightStripAnnotation[lastDeparted_callSign].length() > 7 && this->flightStripAnnotation[callSign].length() > 7)
                            {
                                std::string departedHP = this->flightStripAnnotation[lastDeparted_callSign].substr(7);
                                std::string hp = this->flightStripAnnotation[callSign].substr(7);
                                if (!IsSameHoldingPoint(departedHP, hp, airport->second.runways))
                                {
                                    secondsRequired += 60;
                                }
                            }

                            ULONGLONG now = GetTickCount64();
                            if (this->twrSameSID_flightPlans.find(lastDeparted_callSign) != this->twrSameSID_flightPlans.end()) {
                                auto secondsSinceDeparted = (now - this->twrSameSID_flightPlans.at(lastDeparted_callSign)) / 1000;

                                if (secondsSinceDeparted > secondsRequired)
                                {
                                    tag.color = TAG_COLOR_GREEN;
                                    tag.tag = "OK";
                                    return tag;
                                }

                                if (secondsSinceDeparted + 30 > secondsRequired)
                                {
                                    tag.color = TAG_COLOR_GREEN;
                                    tag.tag = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
                                    return tag;
                                }

                                if (secondsSinceDeparted + 45 > secondsRequired)
                                {
                                    tag.color = TAG_COLOR_YELLOW;
                                    tag.tag = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
                                    return tag;
                                }

                                tag.color = TAG_COLOR_RED;
                                tag.tag = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
                                return tag;
                            }

                            // Flight plan removed, either disconnected or out of range
                            tag.color = TAG_COLOR_GREEN;
                            tag.tag = "OK";
                            return tag;
                        }

                        // Distance based
                        std::string departedSID = lastDeparted_radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetSidName();
                        std::string sid = fp.GetFlightPlanData().GetSidName();

                        double distanceRequired = 5;

                        if (!departedSID.empty() && !sid.empty() && departedSID.length() > 2 && sid.length() > 2) {
                            auto depSidKey = departedSID.substr(0, departedSID.length() - 2);
                            auto sidKey = sid.substr(0, sid.length() - 2);

                            auto rwyIt = airport->second.runways.find(rwy);
                            if (rwyIt != airport->second.runways.end())
                            {
                                auto& sidGroupsMap = rwyIt->second.sidGroups;
                                auto depGroupIt = sidGroupsMap.find(depSidKey);
                                auto sidGroupIt = sidGroupsMap.find(sidKey);
                                if (depGroupIt != sidGroupsMap.end() && sidGroupIt != sidGroupsMap.end())
                                {
                                    if (depGroupIt->second != sidGroupIt->second)
                                    {
                                        distanceRequired = 3;
                                    }
                                }
                            }
                        }

                        auto distanceBetween = rt.GetPosition().GetPosition().DistanceTo(lastDeparted_radarTarget.GetPosition().GetPosition());
                        if (distanceBetween > distanceRequired)
                        {
                            tag.color = TAG_COLOR_GREEN;
                            tag.tag = "OK";
                            return tag;
                        }

                        if (distanceRequired <= 3.1 && distanceBetween > 1.3)
                        {
                            tag.color = TAG_COLOR_GREEN;
                            std::string num_text = std::to_string(distanceRequired - distanceBetween);
                            tag.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
                            return tag;
                        }

                        if (distanceBetween > 3)
                        {
                            tag.color = TAG_COLOR_GREEN;
                            std::string num_text = std::to_string(distanceRequired - distanceBetween);
                            tag.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
                            return tag;
                        }

                        if (distanceBetween > 2.5)
                        {
                            tag.color = TAG_COLOR_YELLOW;
                            std::string num_text = std::to_string(distanceRequired - distanceBetween);
                            tag.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
                            return tag;
                        }

                        tag.color = TAG_COLOR_RED;
                        std::string num_text = std::to_string(distanceRequired - distanceBetween);
                        tag.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
                        return tag;
                    }
                }

                tag.color = TAG_COLOR_GREEN;
                tag.tag = "OK?";
            }
        }
    }
    catch ([[maybe_unused]] const std::exception& ex)
    {
        tag.color = TAG_COLOR_RED;
        tag.tag = "ERR";
    }

    return tag;
}

/// @brief Builds the tower sort key tag used to order the TWR departure list lexicographically.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with a sort key string encoding group, ground status, runway, and distance or sequence.
/// @note Pre-departure aircraft sort A/B first (nearest-to-threshold first); departed aircraft sort C last.
tagInfo CDelHelX_Tags::GetTwrSortKey(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;

    std::string callSign = fp.GetCallsign();
    if (this->twrSameSID_flightPlans.find(callSign) == this->twrSameSID_flightPlans.end())
    {
        return tag;
    }

    EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
    std::string dep = fpd.GetOrigin();
    to_upper(dep);
    std::string rwy = fpd.GetDepartureRwy();

    // Pad runway to 3 chars for consistent lexicographic ordering
    std::string rwyPadded = rwy;
    while (rwyPadded.size() < 3) rwyPadded += ' ';

    ULONGLONG takeoffTick = this->twrSameSID_flightPlans.at(callSign);

    if (takeoffTick == 0)
    {
        // Not yet departed: group A, then ground status sub-group, then runway, then distance to threshold ascending
        double dist = 0.0;
        auto airport = this->airports.find(dep);
        if (airport != this->airports.end())
        {
            auto position = fp.GetCorrelatedRadarTarget().GetPosition().GetPosition();
            dist = DistanceFromRunwayThreshold(rwy, position, airport->second.runways);
        }

        std::string defGroundState = fp.GetGroundState();
        auto statusIt = this->groundStatus.find(callSign);
        std::string status = statusIt != this->groundStatus.end() ? statusIt->second : defGroundState;

        if (defGroundState == "DEPA" && status == "TAXI")
        {
            status = "DEPA";
        }

        char subGroup;
        if (status == "DEPA")        { subGroup = '1'; }
        else if (status == "LINEUP") { subGroup = '2'; }
        else if (status == "TAXI")   { subGroup = '3'; }
        else                         { subGroup = '4'; }

        char distBuf[16];
        (void)snprintf(distBuf, sizeof(distBuf), "%05.2f", dist);

        std::string group = "A";
        auto me = this->ControllerMyself();
        if (me.IsController() && me.GetRating() > 1 && me.GetFacility() == 3 && subGroup < 3)
        {
            // If GND controller, line up and departures are not that important anymore, move them into the "in-between" group "B"
            group = "B";
        }

        tag.tag = group + subGroup + rwyPadded + distBuf;
    }
    else
    {
        // Departed: group C, then runway, then departure sequence ascending
        auto seqIt = this->dep_sequenceNumber.find(callSign);
        int seq = seqIt != this->dep_sequenceNumber.end() ? seqIt->second : 0;
        char seqBuf[8];
        (void)snprintf(seqBuf, sizeof(seqBuf), "%04d", seq);
        tag.tag = "C" + rwyPadded + seqBuf;
    }

    return tag;
}

/// @brief Builds the expanded ground-state tag with a human-readable label and colour.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with a text label (e.g. "PUSH", "TAXI", "LINE UP", "TAKE OFF", "--DEP--") and appropriate colour.
tagInfo CDelHelX_Tags::GetGndStateExpandedTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;
    tag.color = TAG_COLOR_DEFAULT_GRAY;

    std::string callSign = fp.GetCallsign();
    std::string defGroundState = fp.GetGroundState();
    auto statusIt = this->groundStatus.find(callSign);
    std::string status = statusIt != this->groundStatus.end() ? statusIt->second : defGroundState;

    auto twtIt = this->twrSameSID_flightPlans.find(callSign);
    if (twtIt != this->twrSameSID_flightPlans.end() && twtIt->second != 0)
    {
        status = "--DEP--";
    }

    if (status == "DEPA")
    {
        status = "TAKE OFF";
        tag.color = TAG_COLOR_GREEN;
    }
    if (status == "LINEUP")
    {
        status = "LINE UP";
        tag.color = TAG_COLOR_TURQ;
    }
    if (status == "ST-UP") { status = "START-UP"; }

    tag.tag = status;
    return tag;
}