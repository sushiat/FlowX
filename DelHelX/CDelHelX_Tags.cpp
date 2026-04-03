/**
 * @file CDelHelX_Tags.cpp
 * @brief EuroScope tag item implementations for the five registered tag columns.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CDelHelX_Tags.h"

#include <algorithm>
#include <set>
#include "helpers.h"

/// @brief Returns the cached ADES tag for the given flight plan.
tagInfo CDelHelX_Tags::GetAdesTag(EuroScopePlugIn::CFlightPlan& fp)
{
    auto it = this->adesCache.find(fp.GetCallsign());
    if (it != this->adesCache.end())
    {
        return it->second;
    }

    // Cache miss (first 5 s or plan not in a configured airport) — fall back to raw destination
    tagInfo tag;
    tag.tag   = fp.GetFlightPlanData().GetDestination();
    tag.color = TAG_COLOR_DEFAULT_NONE;
    return tag;
}

/// @brief Builds the new-QNH tag showing orange "X" when annotation slot 8 contains the 'Q' flag.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with "X" in orange when a new QNH is pending, or empty.
tagInfo CDelHelX_Tags::GetNewQnhTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo     tag;
    std::string callSign = fp.GetCallsign();

    EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
    this->flightStripAnnotation[callSign]                    = fpcad.GetFlightStripAnnotation(8);
    if (!this->flightStripAnnotation[callSign].empty() && this->flightStripAnnotation[callSign][0] == 'Q')
    {
        tag.tag   = "X";
        tag.color = TAG_COLOR_ORANGE;
    }
    else
    {
        tag.tag = "";
    }

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

    auto        fpd = fp.GetFlightPlanData();
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
        tag.tag   = "!RWY";
        tag.color = TAG_COLOR_RED;

        return tag;
    }

    auto        cad            = fp.GetControllerAssignedData();
    std::string assignedSquawk = cad.GetSquawk();
    std::string currentSquawk  = rt.GetPosition().GetSquawk();

    if (this->noChecks && assignedSquawk.empty())
    {
        assignedSquawk = "2000";
    }

    if (assignedSquawk.empty())
    {
        tag.tag   = "!ASSR";
        tag.color = TAG_COLOR_RED;

        return tag;
    }

    bool clearanceFlag = fp.GetClearenceFlag();
    if (!this->noChecks && !clearanceFlag)
    {
        tag.tag   = "!CLR";
        tag.color = TAG_COLOR_RED;

        return tag;
    }

    if (assignedSquawk != currentSquawk)
    {
        tag.tag   = assignedSquawk;
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
            u_int  corners = geoGnd.second.lat.size();
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

/// @brief Builds the same-SID tag showing the SID name colour-coded by its configured group.
/// @param fp Flight plan being evaluated.
/// @return tagInfo with the SID name and group colour, greyed out once the aircraft has departed.
tagInfo CDelHelX_Tags::GetSameSidTag(EuroScopePlugIn::CFlightPlan& fp)
{
    tagInfo tag;

    EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
    std::string                      dep = fpd.GetOrigin();
    to_upper(dep);
    std::string rwy = fpd.GetDepartureRwy();
    std::string sid = fpd.GetSidName();

    auto airport = this->airports.find(dep);
    if (airport == this->airports.end())
    {
        return tag;
    }

    if (!sid.empty() && sid.length() > 2)
    {
        auto sidKey        = sid.substr(0, sid.length() - 2);
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
        auto        depIt    = this->twrSameSID_flightPlans.find(callSign);
        if (depIt != this->twrSameSID_flightPlans.end() && depIt->second > 0)
        {
            tag.color = TAG_COLOR_DARKGREY;
        }
    }

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
    std::string                      dep = fpd.GetOrigin();
    to_upper(dep);

    auto airport = this->airports.find(dep);
    if (airport == this->airports.end())
    {
        return tag;
    }

    EuroScopePlugIn::CPosition position    = rt.GetPosition().GetPosition();
    std::string                groundState = fp.GetGroundState();

    if (groundState.empty() || groundState == "STUP")
    {
        bool isTaxiOut = false;
        for (auto& taxiOut : airport->second.taxiOutStands)
        {
            u_int  corners = taxiOut.second.lat.size();
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
            tag.tag   = "T";
            tag.color = TAG_COLOR_GREEN;
        }
        else
        {
            tag.tag = groundState.empty() ? "P" : "";
        }
    }

    return tag;
}
