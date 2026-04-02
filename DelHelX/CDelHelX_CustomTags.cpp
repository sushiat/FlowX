#include "pch.h"
#include "CDelHelX_CustomTags.h"
#include "helpers.h"

void CDelHelX_CustomTags::ComputeOutboundCacheEntry(EuroScopePlugIn::CFlightPlan& fp,
                                                     EuroScopePlugIn::CRadarTarget& rt,
                                                     TwrOutboundRowCache& row)
{
    // ── Shared lookups ──
    std::string callSign = fp.GetCallsign();
    auto fpd  = fp.GetFlightPlanData();
    std::string dep = fpd.GetOrigin();
    to_upper(dep);
    std::string rwy = fpd.GetDepartureRwy();

    auto airportIt = this->airports.find(dep);
    bool hasAirport = (airportIt != this->airports.end());

    bool rtValid = rt.IsValid() && rt.GetPosition().IsValid();

    // Annotation (slot 8) — read once, cache for this call
    this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
    const std::string& ann = this->flightStripAnnotation[callSign];

    // Position-derived shared values (local — not stored persistently)
    double distToRunwayThreshold = -1.0;
    bool   atHoldingPoint        = false;

    if (rtValid && hasAirport)
    {
        auto pos = rt.GetPosition().GetPosition();
        distToRunwayThreshold = DistanceFromRunwayThreshold(rwy, pos, airportIt->second.runways);

        auto rwyIt = airportIt->second.runways.find(rwy);
        if (rwyIt != airportIt->second.runways.end())
        {
            for (auto& [hpName, hpData] : rwyIt->second.holdingPoints)
            {
                u_int corners = static_cast<u_int>(hpData.lat.size());
                double polyX[10], polyY[10];
                std::copy(hpData.lon.begin(), hpData.lon.end(), polyX);
                std::copy(hpData.lat.begin(), hpData.lat.end(), polyY);
                if (PointInsidePolygon(static_cast<int>(corners), polyX, polyY,
                                       pos.m_Longitude, pos.m_Latitude))
                {
                    atHoldingPoint = true;
                    break;
                }
            }
        }
    }

    // Takeoff tick (0 = still on ground)
    auto twrIt = this->twrSameSID_flightPlans.find(callSign);
    ULONGLONG takeoffTick = (twrIt != this->twrSameSID_flightPlans.end()) ? twrIt->second : 0;

    // Ground state with DEPA-override logic
    std::string defGroundState = fp.GetGroundState();
    auto statusIt = this->groundStatus.find(callSign);
    std::string status = (statusIt != this->groundStatus.end()) ? statusIt->second : defGroundState;
    if (defGroundState == "DEPA" && status == "TAXI")
    {
        status = "DEPA";
    }

    // ── row.rwy ──
    row.rwy = [&]() -> tagInfo {
        tagInfo t;
        std::string r = rwy;
        r.erase(r.begin(), std::find_if(r.begin(), r.end(), [](unsigned char c) { return !std::isspace(c); }));
        r.erase(std::find_if(r.rbegin(), r.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), r.end());
        if (r.empty())
        {
            t.tag   = "??";
            t.color = this->blinking ? TAG_COLOR_ORANGE : TAG_COLOR_TURQ;
            return t;
        }
        t.tag = r;
        return t;
    }();

    // ── row.status (gndStateExpanded) ──
    row.status = [&]() -> tagInfo {
        tagInfo t;
        t.color = TAG_COLOR_DEFAULT_GRAY;
        std::string s = status;
        if (takeoffTick != 0)
        {
            s = "--DEP--";
        }
        if (s == "DEPA")
        {
            s = "TAKE OFF";
            t.color = TAG_COLOR_GREEN;
        }
        if (s == "LINEUP")
        {
            s = "LINE UP";
            t.color = TAG_COLOR_TURQ;
        }
        if (s == "ST-UP")
        {
            s = "START-UP";
        }
        t.tag = s;
        return t;
    }();

    // ── row.hp (holdingPoint) ──
    row.hp = [&]() -> tagInfo {
        tagInfo t;
        if (ann.length() <= 7)
        {
            return t;
        }
        t.tag   = ann.substr(7);
        t.color = TAG_COLOR_GREEN;
        if (t.tag.find('*') != std::string::npos)
        {
            t.color = TAG_COLOR_ORANGE;
        }
        if (takeoffTick != 0)
        {
            t.color = TAG_COLOR_DARKGREY;
        }
        return t;
    }();

    // ── row.spacing (takeoffSpacing) ──
    row.spacing = [&]() -> tagInfo {
        tagInfo t;
        if (takeoffTick == 0 || !hasAirport)
        {
            return t;
        }
        auto offsetIt = this->dep_prevTakeoffOffset.find(callSign);
        if (offsetIt == this->dep_prevTakeoffOffset.end())
        {
            t.tag = "---";
            return t;
        }

        bool timeBased = false;
        auto prevWtcIt = this->dep_prevWtc.find(callSign);
        if (prevWtcIt != this->dep_prevWtc.end())
        {
            char curWtc = fpd.GetAircraftWtc();
            if (GetAircraftWeightCategoryRanking(curWtc) < GetAircraftWeightCategoryRanking(prevWtcIt->second))
            {
                timeBased = true;
            }
        }

        if (timeBased)
        {
            ULONGLONG offsetSeconds = offsetIt->second;
            auto reqIt = this->dep_timeRequired.find(callSign);
            int timeRequired = (reqIt != this->dep_timeRequired.end()) ? reqIt->second : 120;
            std::string valStr = std::to_string(offsetSeconds);
            valStr = std::string(4 - std::min((size_t)4, valStr.size()), ' ') + valStr;
            t.tag = valStr + " s  /" + std::to_string(timeRequired);

            if (offsetSeconds >= (ULONGLONG)timeRequired)
            {
                t.color = TAG_COLOR_GREEN;
            }
            else if (offsetSeconds >= (ULONGLONG)(timeRequired - 15))
            {
                t.color = TAG_COLOR_YELLOW;
            }
            else
            {
                t.color = TAG_COLOR_RED;
            }
        }
        else
        {
            auto lockedDistIt = this->dep_prevDistanceAtTakeoff.find(callSign);
            if (lockedDistIt == this->dep_prevDistanceAtTakeoff.end())
            {
                t.tag = "---";
                return t;
            }

            double dist = lockedDistIt->second;
            std::string num_text = std::to_string(dist);
            std::string rounded  = num_text.substr(0, num_text.find('.') + 2);

            double distRequired = 3.0;
            std::string curSid  = fpd.GetSidName();
            auto prevSidIt      = this->dep_prevSid.find(callSign);
            if (prevSidIt != this->dep_prevSid.end() && !prevSidIt->second.empty() && !curSid.empty()
                && prevSidIt->second.length() > 2 && curSid.length() > 2)
            {
                auto prevSidKey = prevSidIt->second.substr(0, prevSidIt->second.length() - 2);
                auto curSidKey  = curSid.substr(0, curSid.length() - 2);
                auto rwyIt      = airportIt->second.runways.find(rwy);
                if (rwyIt != airportIt->second.runways.end())
                {
                    auto& sidGroupsMap  = rwyIt->second.sidGroups;
                    auto  prevGroupIt   = sidGroupsMap.find(prevSidKey);
                    auto  curGroupIt    = sidGroupsMap.find(curSidKey);
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
            t.tag = rounded + " nm /" + std::to_string(static_cast<int>(distRequired));

            if (dist >= distRequired)
            {
                t.color = TAG_COLOR_GREEN;
            }
            else if (dist >= distRequired - 0.3)
            {
                t.color = TAG_COLOR_YELLOW;
            }
            else
            {
                t.color = TAG_COLOR_RED;
            }
        }
        return t;
    }();

    // ── row.sortKey (twrSort text) ──
    row.sortKey = [&]() -> std::string {
        if (this->twrSameSID_flightPlans.find(callSign) == this->twrSameSID_flightPlans.end())
        {
            return {};
        }

        std::string rwyPadded = rwy;
        while (rwyPadded.size() < 3)
        {
            rwyPadded += ' ';
        }

        if (takeoffTick == 0)
        {
            double dist = (distToRunwayThreshold >= 0.0) ? distToRunwayThreshold : 0.0;

            char subGroup;
            if (status == "DEPA")        { subGroup = '1'; }
            else if (status == "LINEUP") { subGroup = '2'; }
            else if (status == "TAXI")   { subGroup = '3'; }
            else                         { subGroup = '4'; }

            char distBuf[16];
            (void)snprintf(distBuf, sizeof(distBuf), "%05.2f", dist);

            std::string group = "A";
            auto me = this->ControllerMyself();
            if (me.IsController() && me.GetRating() > 1 && me.GetFacility() == 3 && subGroup < '3')
            {
                group = "B";
            }

            return group + subGroup + rwyPadded + distBuf;
        }
        else
        {
            bool isStillMine = fp.GetTrackingControllerIsMe()
                            && fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED;
            std::string group = isStillMine ? "C" : "D";
            auto seqIt = this->dep_sequenceNumber.find(callSign);
            int  seq   = (seqIt != this->dep_sequenceNumber.end()) ? seqIt->second : 0;
            char seqBuf[8];
            (void)snprintf(seqBuf, sizeof(seqBuf), "%04d", seq);
            return group + rwyPadded + seqBuf;
        }
    }();

    // ── row.nextFreq (twrNextFreq) ──
    row.nextFreq = [&]() -> tagInfo {
        tagInfo t;
        t.color = TAG_COLOR_DEFAULT_GRAY;

        if (!hasAirport || !rtValid || this->radarScreen == nullptr
            || (defGroundState != "TAXI" && defGroundState != "DEPA"))
        {
            return t;
        }

        // Check if already transferred to next frequency
        bool transferred = false;
        if (ann.length() >= 7)
        {
            std::string storedFreq = ann.substr(1, 6);
            if (storedFreq.find_first_not_of(' ') != std::string::npos)
            {
                double myFreqDouble = this->ControllerMyself().GetPrimaryFrequency();
                auto   s            = std::to_string(myFreqDouble);
                std::string myFreq  = s.substr(0, s.find('.') + 4);
                myFreq.erase(std::remove(myFreq.begin(), myFreq.end(), '.'), myFreq.end());
                transferred = (storedFreq != myFreq);
            }
        }

        auto me = this->ControllerMyself();
        if (!me.IsController() || me.GetRating() <= 1 || me.GetFacility() < 3 || me.GetFacility() > 4)
        {
            return t;
        }

        if (me.GetFacility() == 3)
        {
            bool towerOnline = false;
            for (auto& station : this->radarScreen->towerStations)
            {
                if (station.first.find(dep) != std::string::npos)
                {
                    towerOnline = true;
                    break;
                }
            }

            if (towerOnline || this->towerOverride)
            {
                auto rwyIt = airportIt->second.runways.find(rwy);
                if (rwyIt != airportIt->second.runways.end())
                {
                    bool nearThreshold = (distToRunwayThreshold >= 0.0 && distToRunwayThreshold < 0.2);
                    if (!transferred)
                    {
                        t.color = (nearThreshold || atHoldingPoint)
                                  ? (this->blinking ? TAG_COLOR_YELLOW : TAG_COLOR_WHITE)
                                  : TAG_COLOR_WHITE;
                    }
                    t.tag = "->" + rwyIt->second.twrFreq;
                    return t;
                }

                if (rwy.empty() && fpd.GetPlanType() == std::string("V"))
                {
                    t.tag = "->" + airportIt->second.runways.begin()->second.twrFreq;
                    return t;
                }
            }
        }

        // Not squawking mode-C
        if (!rt.GetPosition().GetTransponderC()
            && rt.GetPosition().GetPressureAltitude() > (airportIt->second.fieldElevation + 50))
        {
            t.tag   = "!MODE-C";
            t.color = this->blinking ? TAG_COLOR_RED : TAG_COLOR_ORANGE;
            return t;
        }

        // TWR: find APP or CTR freq with altitude colour coding
        if (!transferred)
        {
            t.color = TAG_COLOR_WHITE;
            if (rt.GetPosition().GetPressureAltitude() >= airportIt->second.airborneTransfer
                && rt.GetPosition().GetPressureAltitude() < airportIt->second.airborneTransferWarning)
            {
                t.color = TAG_COLOR_TURQ;
            }
            else if (rt.GetPosition().GetPressureAltitude() >= airportIt->second.airborneTransferWarning)
            {
                t.color = this->blinking ? TAG_COLOR_ORANGE : TAG_COLOR_TURQ;
            }
        }

        std::string targetFreq = airportIt->second.defaultAppFreq;
        {
            std::string sid = fpd.GetSidName();
            for (auto& [f, sids] : airportIt->second.sidAppFreqs)
            {
                if (std::find(sids.begin(), sids.end(), sid) != sids.end())
                {
                    targetFreq = f;
                    break;
                }
            }
        }

        for (const auto& station : this->radarScreen->approachStations)
        {
            if (airportIt->second.appFreqFallbacks.count(station.second))
            {
                t.tag = "->" + targetFreq;
                return t;
            }
        }

        for (const auto& ctrFreq : airportIt->second.ctrStations)
        {
            for (const auto& station : this->radarScreen->centerStations)
            {
                if (station.second == ctrFreq)
                {
                    t.tag = "->" + ctrFreq;
                    return t;
                }
            }
        }

        t.tag = "->122.8";
        return t;
    }();

    // ── row.depInfo (departureInfo) ──
    row.depInfo = [&]() -> tagInfo {
        tagInfo t;
        try
        {
            if (!hasAirport)
            {
                return t;
            }
            if (defGroundState != "TAXI" && defGroundState != "DEPA")
            {
                return t;
            }
            if (this->twrSameSID_flightPlans.find(callSign) == this->twrSameSID_flightPlans.end()
                || this->twrSameSID_flightPlans.at(callSign) != 0)
            {
                return t;
            }

            std::string lastDeparted_callSign;
            if (this->twrSameSID_lastDeparted.find(rwy) != this->twrSameSID_lastDeparted.end())
            {
                lastDeparted_callSign = this->twrSameSID_lastDeparted.at(rwy);
            }

            if (lastDeparted_callSign.empty())
            {
                t.color = TAG_COLOR_GREEN;
                t.tag   = "OK?";
                return t;
            }

            EuroScopePlugIn::CRadarTarget lastDeparted_rt = this->RadarTargetSelect(lastDeparted_callSign.c_str());
            if (!lastDeparted_rt.IsValid())
            {
                t.color = TAG_COLOR_GREEN;
                t.tag   = "OK?";
                return t;
            }

            char departedWtc = lastDeparted_rt.GetCorrelatedFlightPlan().GetFlightPlanData().GetAircraftWtc();
            char curWtc      = fpd.GetAircraftWtc();

            if (GetAircraftWeightCategoryRanking(departedWtc) > GetAircraftWeightCategoryRanking(curWtc))
            {
                // Time-based separation
                unsigned long secondsRequired = 120;

                this->flightStripAnnotation[lastDeparted_callSign] =
                    lastDeparted_rt.GetCorrelatedFlightPlan().GetControllerAssignedData().GetFlightStripAnnotation(8);
                // ann already up-to-date for callSign
                if (this->flightStripAnnotation[lastDeparted_callSign].length() > 7 && ann.length() > 7)
                {
                    std::string departedHP = this->flightStripAnnotation[lastDeparted_callSign].substr(7);
                    std::string hp         = ann.substr(7);
                    if (!IsSameHoldingPoint(departedHP, hp, airportIt->second.runways))
                    {
                        secondsRequired += 60;
                    }
                }

                if (this->twrSameSID_flightPlans.find(lastDeparted_callSign) != this->twrSameSID_flightPlans.end())
                {
                    ULONGLONG now                = GetTickCount64();
                    auto secondsSinceDeparted    = (now - this->twrSameSID_flightPlans.at(lastDeparted_callSign)) / 1000;

                    if (secondsSinceDeparted > secondsRequired)
                    {
                        t.color = TAG_COLOR_GREEN;
                        t.tag   = "OK";
                        return t;
                    }
                    if (secondsSinceDeparted + 30 > secondsRequired)
                    {
                        t.color = TAG_COLOR_GREEN;
                        t.tag   = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
                        return t;
                    }
                    if (secondsSinceDeparted + 45 > secondsRequired)
                    {
                        t.color = TAG_COLOR_YELLOW;
                        t.tag   = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
                        return t;
                    }
                    t.color = TAG_COLOR_RED;
                    t.tag   = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
                    return t;
                }

                // lastDeparted not in map: disconnected / out of range
                t.color = TAG_COLOR_GREEN;
                t.tag   = "OK";
                return t;
            }

            // Distance-based separation
            std::string departedSID = lastDeparted_rt.GetCorrelatedFlightPlan().GetFlightPlanData().GetSidName();
            std::string sid         = fpd.GetSidName();
            double distanceRequired = 5.0;

            if (!departedSID.empty() && !sid.empty() && departedSID.length() > 2 && sid.length() > 2)
            {
                auto depSidKey = departedSID.substr(0, departedSID.length() - 2);
                auto sidKey    = sid.substr(0, sid.length() - 2);
                auto rwyIt     = airportIt->second.runways.find(rwy);
                if (rwyIt != airportIt->second.runways.end())
                {
                    auto& sidGroupsMap = rwyIt->second.sidGroups;
                    auto  depGroupIt   = sidGroupsMap.find(depSidKey);
                    auto  sidGroupIt   = sidGroupsMap.find(sidKey);
                    if (depGroupIt != sidGroupsMap.end() && sidGroupIt != sidGroupsMap.end()
                        && depGroupIt->second != sidGroupIt->second)
                    {
                        distanceRequired = 3.0;
                    }
                }
            }

            auto distanceBetween = rt.GetPosition().GetPosition()
                                    .DistanceTo(lastDeparted_rt.GetPosition().GetPosition());

            if (distanceBetween > distanceRequired)
            {
                t.color = TAG_COLOR_GREEN;
                t.tag   = "OK";
                return t;
            }

            auto makeNmTag = [&](COLORREF c) -> tagInfo {
                tagInfo r;
                r.color = c;
                std::string num_text = std::to_string(distanceBetween);
                r.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
                return r;
            };

            if (distanceRequired <= 3.1 && distanceBetween > 1.3)
            {
                return makeNmTag(TAG_COLOR_GREEN);
            }
            if (distanceBetween > 3.0)
            {
                return makeNmTag(TAG_COLOR_GREEN);
            }
            if (distanceBetween > 2.5)
            {
                return makeNmTag(TAG_COLOR_YELLOW);
            }
            return makeNmTag(TAG_COLOR_RED);
        }
        catch ([[maybe_unused]] const std::exception&)
        {
            t.color = TAG_COLOR_RED;
            t.tag   = "ERR";
        }
        return t;
    }();

    // ── row.sameSid ──
    row.sameSid = this->GetSameSidTag(fp);
}

void CDelHelX_CustomTags::ComputeInboundCacheEntry(const std::string& tttKey,
                                                    EuroScopePlugIn::CFlightPlan& fp,
                                                    EuroScopePlugIn::CRadarTarget& rt,
                                                    TwrInboundRowCache& row)
{
    // ── Shared lookups ──
    std::string callSign = fp.GetCallsign();
    auto fpd = fp.GetFlightPlanData();

    // Resolve tttKey: if empty, search by callSign prefix (for UpdatePositionDerivedTags)
    std::string resolvedKey = tttKey;
    if (resolvedKey.empty())
    {
        auto it = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
            [&callSign](const auto& e) { return e.first.rfind(callSign, 0) == 0; });
        if (it != this->ttt_flightPlans.end())
        {
            resolvedKey = it->first;
        }
    }

    auto planIt = this->ttt_flightPlans.find(resolvedKey);
    if (planIt == this->ttt_flightPlans.end())
    {
        row.sortKey = {};
        row.nm      = tagInfo{};
        row.vacate  = tagInfo{};
        row.arrRwy  = tagInfo{};
        return;
    }

    const std::string& designator = planIt->second.designator;

    auto distIt      = this->ttt_distanceToRunway.find(resolvedKey);
    bool hasDistance = (distIt != this->ttt_distanceToRunway.end());
    double distToThreshold = hasDistance ? distIt->second : 0.0;

    bool isGoAround = (this->ttt_goAround.count(resolvedKey) > 0);

    auto sortedIt  = this->ttt_sortedByRunway.find(designator);
    bool hasSorted = (sortedIt != this->ttt_sortedByRunway.end());

    // ── row.sortKey — raw "RWY_MM:SS" string for ordering (caller fills row.ttt display value) ──
    row.sortKey = [&]() -> std::string {
        if (isGoAround)
        {
            return designator + "_->" + planIt->second.goAroundFreq;
        }

        auto position = rt.GetPosition().GetPosition();
        int  speed    = rt.GetPosition().GetReportedGS();
        if (speed > 0)
        {
            EuroScopePlugIn::CPosition rwyThreshold;
            rwyThreshold.m_Latitude  = planIt->second.thresholdLat;
            rwyThreshold.m_Longitude = planIt->second.thresholdLon;

            double distNm    = position.DistanceTo(rwyThreshold);
            int totalSeconds = static_cast<int>((distNm / speed) * 3600.0);
            int mm = totalSeconds / 60;
            int ss = totalSeconds % 60;
            char buf[16];
            (void)sprintf_s(buf, "%s_%02d:%02d", designator.c_str(), mm, ss);
            return std::string(buf);
        }
        return {};
    }();

    // ── row.nm (inboundNm) ──
    row.nm = [&]() -> tagInfo {
        tagInfo t;
        if (!hasDistance)
        {
            return t;
        }
        if (isGoAround)
        {
            char buf[16] = {};
            (void)sprintf_s(buf, "%.1f", distToThreshold);
            t.tag = std::string(buf);
            return t;
        }
        if (!hasSorted)
        {
            return t;
        }

        const auto& keys = sortedIt->second;
        char buf[16] = {};
        if (keys.front() == resolvedKey)
        {
            (void)sprintf_s(buf, "%.1f", distToThreshold);
        }
        else
        {
            for (size_t i = 1; i < keys.size(); ++i)
            {
                if (keys[i] == resolvedKey)
                {
                    auto leaderIt = this->ttt_distanceToRunway.find(keys[i - 1]);
                    if (leaderIt == this->ttt_distanceToRunway.end())
                    {
                        break;
                    }
                    double gap = distToThreshold - leaderIt->second;
                    (void)sprintf_s(buf, "+%.1f", gap);
                    if (gap > 3.0)
                    {
                        t.color = TAG_COLOR_GREEN;
                    }
                    else if (gap > 2.5)
                    {
                        t.color = TAG_COLOR_YELLOW;
                    }
                    else
                    {
                        t.color = TAG_COLOR_RED;
                    }
                    break;
                }
            }
        }
        t.tag = std::string(buf);
        return t;
    }();

    // ── row.arrRwy (assignedArrRwy) ──
    row.arrRwy = [&]() -> tagInfo {
        tagInfo t;
        t.tag = fpd.GetArrivalRwy();
        if (t.tag != designator)
        {
            t.color = this->blinking ? TAG_COLOR_RED : TAG_COLOR_YELLOW;
        }
        return t;
    }();

    // ── row.vacate (suggestedVacate) ──
    row.vacate = [&]() -> tagInfo {
        tagInfo t;

        auto standIt = this->standAssignment.find(callSign);
        if (standIt == this->standAssignment.end())
        {
            return t;
        }
        if (!hasSorted)
        {
            return t;
        }

        std::string stand = standIt->second;
        const auto& keys  = sortedIt->second;
        auto myIdx = std::find(keys.begin(), keys.end(), resolvedKey);
        if (myIdx == keys.end())
        {
            return t;
        }

        bool   hasFollower = (myIdx + 1 != keys.end());
        double gap         = 0.0;
        if (hasFollower)
        {
            auto followerIt = this->ttt_distanceToRunway.find(*(myIdx + 1));
            auto selfIt     = this->ttt_distanceToRunway.find(resolvedKey);
            if (followerIt == this->ttt_distanceToRunway.end() || selfIt == this->ttt_distanceToRunway.end())
            {
                return t;
            }
            gap = followerIt->second - selfIt->second;
        }

        std::string arr = fpd.GetDestination();
        to_upper(arr);
        auto airportIt = this->airports.find(arr);
        if (airportIt == this->airports.end())
        {
            return t;
        }

        auto rwyIt = airportIt->second.runways.find(designator);
        if (rwyIt == airportIt->second.runways.end())
        {
            return t;
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
                    matched = (stand.rfind(prefix, 0) == 0);
                }
                else
                {
                    std::string p = pattern;
                    to_upper(p);
                    matched = (stand == p);
                }

                if (matched)
                {
                    t.tag   = vpName;
                    t.color = TAG_COLOR_WHITE;
                    return t;
                }
            }
        }
        return t;
    }();

    // row.ttt is intentionally left empty here — the outer UpdateTagCache loop fills it
    // with the gap-adjusted display value after sorting.
}

void CDelHelX_CustomTags::UpdateTagCache()
{
    if (this->radarScreen == nullptr) { return; }

    if (this->GetConnectionType() == EuroScopePlugIn::CONNECTION_TYPE_NO)
    {
        this->radarScreen->depRateRowsCache.clear();
        this->radarScreen->twrOutboundRowsCache.clear();
        this->radarScreen->twrInboundRowsCache.clear();
        return;
    }

    // Parse "mm:ss" display string → total seconds; returns -1 if malformed or empty
    auto parseTttSec = [](const std::string& s) -> int {
        auto colon = s.find(':');
        if (s.empty() || colon == std::string::npos) { return -1; }
        return atoi(s.c_str()) * 60 + atoi(s.c_str() + colon + 1);
    };

    // =========================================================
    // TWR OUTBOUND — departure aircraft in twrSameSID_flightPlans
    // =========================================================
    if (this->ControllerMyself().GetFacility() >= 3)
    {
        std::vector<TwrOutboundRowCache> outboundRows;

        for (auto& [callSign, takeoffTick] : this->twrSameSID_flightPlans)
        {
            EuroScopePlugIn::CFlightPlan  fp = this->FlightPlanSelect(callSign.c_str());
            EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelect(callSign.c_str());
            if (!fp.IsValid()) { continue; }

            auto callsignColor = [&]() -> COLORREF {
                if (fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED) { return TAG_COLOR_BROWN; }
                if (fp.GetTrackingControllerIsMe()) { return TAG_COLOR_WHITE; }
                return TAG_COLOR_DEFAULT_GRAY;
            };

            TwrOutboundRowCache row;
            row.callsign      = callSign;
            row.callsignColor = callsignColor();
            row.wtc           = fp.GetFlightPlanData().GetAircraftWtc();
            row.aircraftType  = fp.GetFlightPlanData().GetAircraftFPType();

            ComputeOutboundCacheEntry(fp, rt, row);

            // Dim group D (departed + not tracking by me, including transfer-from-me-initiated)
            row.dimmed = row.status.tag.find("DEP") != std::string::npos
                      && row.callsignColor != TAG_COLOR_WHITE;

            outboundRows.push_back(std::move(row));
        }

        std::sort(outboundRows.begin(), outboundRows.end(),
            [](const TwrOutboundRowCache& a, const TwrOutboundRowCache& b) {
                return a.sortKey < b.sortKey;
            });

        // Mark the first row of each sort group (A/B vs C vs D) so the draw function can insert separators
        char lastGroup = '\0';
        for (auto& r : outboundRows)
        {
            char g = r.sortKey.empty() ? '\0' : r.sortKey[0];
            r.groupSeparatorAbove = (lastGroup != '\0' && g != lastGroup);
            lastGroup = g;
        }

        this->radarScreen->twrOutboundRowsCache = std::move(outboundRows);
    }
    else
    {
        this->radarScreen->twrOutboundRowsCache.clear();
    }

    // =========================================================
    // TTT INBOUND — inbound aircraft in ttt_sortedByRunway order
    // =========================================================
    {
        std::vector<TwrInboundRowCache> inboundRows;

        for (auto& [runway, keys] : this->ttt_sortedByRunway)
        {
            bool isFirst   = true;
            int prevTttSec = -1;

            for (auto& key : keys)
            {
                auto planIt = this->ttt_flightPlans.find(key);
                if (planIt == this->ttt_flightPlans.end()) { continue; }

                // Key = callSign + runway designator; strip the designator suffix
                const std::string& designator = planIt->second.designator;
                std::string callSign = key.substr(0, key.size() - designator.size());

                EuroScopePlugIn::CFlightPlan  fp = this->FlightPlanSelect(callSign.c_str());
                EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelect(callSign.c_str());
                if (!fp.IsValid() || !rt.IsValid()) { continue; }

                auto callsignColor = [&]() -> COLORREF {
                    if (fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED) { return TAG_COLOR_BROWN; }
                    if (fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED) { return TAG_COLOR_TURQ; }
                    if (fp.GetTrackingControllerIsMe()) { return TAG_COLOR_WHITE; }
                    return TAG_COLOR_LIST_GRAY;
                };

                TwrInboundRowCache row;
                row.callsign      = callSign;
                row.callsignColor = callsignColor();
                row.wtc           = fp.GetFlightPlanData().GetAircraftWtc();
                row.groundSpeed   = rt.GetPosition().GetReportedGS();
                row.rwyGroup      = designator;
                row.aircraftType  = fp.GetFlightPlanData().GetAircraftFPType();
                {
                    auto standIt = this->standAssignment.find(callSign);
                    row.gate = (standIt != this->standAssignment.end()) ? standIt->second : "";
                }
                row.dimmed = !isFirst;

                ComputeInboundCacheEntry(key, fp, rt, row);

                // Strip "designator_" prefix from sortKey for TTT display value
                tagInfo tttDisplay;
                tttDisplay.color = TAG_COLOR_DEFAULT_GRAY;
                {
                    // Derive the colour from the raw sortKey time string
                    std::string raw = row.sortKey;
                    auto sep = raw.find('_');
                    std::string timeStr = (sep != std::string::npos) ? raw.substr(sep + 1) : raw;

                    if (!timeStr.empty() && timeStr[0] != '-' && timeStr[0] != '>')
                    {
                        int totalSeconds = parseTttSec(timeStr);
                        if (totalSeconds > 120)      { tttDisplay.color = TAG_COLOR_GREEN; }
                        else if (totalSeconds > 60)  { tttDisplay.color = TAG_COLOR_YELLOW; }
                        else if (totalSeconds >= 0)  { tttDisplay.color = TAG_COLOR_RED; }
                    }
                    else if (!timeStr.empty())
                    {
                        // go-around: blinking yellow/red
                        tttDisplay.color = this->blinking ? TAG_COLOR_RED : TAG_COLOR_YELLOW;
                    }
                    tttDisplay.tag = timeStr;
                }

                // Non-first aircraft: replace TTT with "+mm:ss" gap to the aircraft ahead
                int currTttSec = parseTttSec(tttDisplay.tag);
                if (!isFirst && currTttSec >= 0 && prevTttSec >= 0)
                {
                    int gap = currTttSec - prevTttSec;
                    if (gap < 0) { gap = 0; }
                    char buf[16];
                    snprintf(buf, sizeof(buf), "+%02d:%02d", gap / 60, gap % 60);
                    tttDisplay.tag = buf;
                }
                if (currTttSec >= 0) { prevTttSec = currTttSec; }

                row.ttt = tttDisplay;

                inboundRows.push_back(std::move(row));
                isFirst = false;
            }
        }

        this->radarScreen->twrInboundRowsCache = std::move(inboundRows);
    }

    // =========================================================
    // DEP/H window — per-runway average departure spacing
    // =========================================================
    {
        this->radarScreen->depRateRowsCache.clear();
        ULONGLONG nowMs = GetTickCount64();

        for (auto& [rwy, timestamps] : this->radarScreen->depRateLog)
        {
            DepRateRowCache depRow;
            depRow.runway = rwy;

            int count      = static_cast<int>(timestamps.size());
            depRow.countStr   = std::to_string(count);
            depRow.countColor = count > 0 ? TAG_COLOR_GREEN : TAG_COLOR_DEFAULT_GRAY;

            std::vector<ULONGLONG> recent;
            for (auto t : timestamps)
            {
                if ((nowMs - t) <= 900000ULL) { recent.push_back(t); }
            }

            depRow.spacingStr   = "--:--";
            depRow.spacingColor = TAG_COLOR_DEFAULT_GRAY;
            if (recent.size() >= 2)
            {
                std::sort(recent.begin(), recent.end());
                ULONGLONG avgGapSec = ((recent.back() - recent.front()) / (recent.size() - 1)) / 1000ULL;
                char buf[8];
                snprintf(buf, sizeof(buf), "%02llu:%02llu", avgGapSec / 60, avgGapSec % 60);
                depRow.spacingStr   = buf;
                depRow.spacingColor = TAG_COLOR_WHITE;
            }

            this->radarScreen->depRateRowsCache.push_back(std::move(depRow));
        }
    }
}

void CDelHelX_CustomTags::UpdatePositionDerivedTags(EuroScopePlugIn::CRadarTarget rt)
{
    if (!rt.IsValid()) { return; }

    std::string callSign = rt.GetCallsign();

    EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
    if (!fp.IsValid()) { return; }

    // Only update for inbound aircraft
    bool isInbound = std::any_of(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
        [&callSign](const auto& e) { return e.first.rfind(callSign, 0) == 0; });
    if (!isInbound) { return; }

    // Find the full tttKey (callSign + runway designator)
    std::string tttKey;
    {
        auto it = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
            [&callSign](const auto& e) { return e.first.rfind(callSign, 0) == 0; });
        if (it != this->ttt_flightPlans.end())
        {
            tttKey = it->first;
        }
    }

    if (tttKey.empty()) { return; }

    if (this->radarScreen == nullptr) { return; }

    // Find the row in twrInboundRowsCache for this callsign and update it directly
    for (auto& row : this->radarScreen->twrInboundRowsCache)
    {
        if (row.callsign != callSign) { continue; }

        // Re-compute nm and sortKey in a temporary row, then copy relevant fields
        TwrInboundRowCache tmp;
        tmp.callsign = callSign;
        ComputeInboundCacheEntry(tttKey, fp, rt, tmp);

        // Parse "mm:ss" display string → total seconds; returns -1 if malformed or empty
        auto parseTttSec = [](const std::string& s) -> int {
            auto colon = s.find(':');
            if (s.empty() || colon == std::string::npos) { return -1; }
            return atoi(s.c_str()) * 60 + atoi(s.c_str() + colon + 1);
        };

        // Build the display ttt from the raw sortKey
        tagInfo tttDisplay;
        tttDisplay.color = TAG_COLOR_DEFAULT_GRAY;
        {
            std::string raw = tmp.sortKey;
            auto sep = raw.find('_');
            std::string timeStr = (sep != std::string::npos) ? raw.substr(sep + 1) : raw;

            if (!timeStr.empty() && timeStr[0] != '-' && timeStr[0] != '>')
            {
                int totalSeconds = parseTttSec(timeStr);
                if (totalSeconds > 120)      { tttDisplay.color = TAG_COLOR_GREEN; }
                else if (totalSeconds > 60)  { tttDisplay.color = TAG_COLOR_YELLOW; }
                else if (totalSeconds >= 0)  { tttDisplay.color = TAG_COLOR_RED; }
            }
            else if (!timeStr.empty())
            {
                tttDisplay.color = this->blinking ? TAG_COLOR_RED : TAG_COLOR_YELLOW;
            }
            tttDisplay.tag = timeStr;
        }

        row.ttt         = tttDisplay;
        row.nm          = tmp.nm;
        row.sortKey     = tmp.sortKey;
        row.groundSpeed = rt.GetPosition().GetReportedGS();
        break;
    }
}
