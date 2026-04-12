#include "pch.h"
// test_taxi_routes.cpp
// Fixture-driven regression tests for taxi route generation on the real LOWW
// TaxiGraph.  Test cases are defined in fixtures/taxi-test.json and deployed
// to $(OutDir) by the PostBuildEvent.

#include <doctest/doctest.h>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>
#include "test_accessor.h"
#include "taxi_graph.h"
#include "CFlowX_LookupsTools.h"
#include "helpers.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// @brief Parses a runway config string like "29_34" or "16/29_16" into
///        departure and arrival runway sets.
static std::pair<std::set<std::string>, std::set<std::string>>
ParseRunwayConfig(const std::string& cfg)
{
    std::set<std::string> dep, arr;
    auto                  halves = split(cfg, '_');
    if (halves.size() >= 1)
        for (const auto& r : split(halves[0], '/'))
            if (!r.empty())
                dep.insert(r);
    if (halves.size() >= 2)
        for (const auto& r : split(halves[1], '/'))
            if (!r.empty())
                arr.insert(r);
    return {dep, arr};
}

/// @brief Resolves a position label to a GeoPoint.
///   - "GEO:lat,lon" → raw WGS-84 coordinates (used for waypoint-type positions)
///   - "RWY:29"    → runway threshold
///   - "STAND:B67"        → stand approach point (prepends "LOWW:")
///   - "HP:A12"           → holding position/point by label
///   - "GEO:lat,lon"      → raw WGS-84 coordinates
static GeoPoint ResolvePosition(const std::string&                    label,
                                const TaxiGraph&                      graph,
                                const std::map<std::string, grStand>& grStands,
                                const std::map<std::string, airport>& airports)
{
    if (label.starts_with("GEO:"))
    {
        auto parts = split(label.substr(4), ',');
        if (parts.size() == 2)
        {
            try
            {
                return {std::stod(parts[0]), std::stod(parts[1])};
            }
            catch (...)
            {
            }
        }
        return {0.0, 0.0};
    }

    if (label.starts_with("RWY:"))
    {
        std::string rwyDes = label.substr(4);
        for (const auto& [icao, ap] : airports)
        {
            auto it = ap.runways.find(rwyDes);
            if (it != ap.runways.end())
                return {it->second.thresholdLat, it->second.thresholdLon};
        }
        return {0.0, 0.0};
    }

    if (label.starts_with("STAND:"))
    {
        std::string standName = label.substr(6);
        // Honour standRoutingTargets override — mirrors RadarScreen's inbound routing logic.
        for (const auto& [icao, ap] : airports)
        {
            auto ovIt = ap.standRoutingTargets.find(standName);
            if (ovIt != ap.standRoutingTargets.end())
            {
                const auto& srt = ovIt->second;
                if (srt.type == standRoutingTarget::Type::stand)
                {
                    GeoPoint pt = TaxiGraph::StandApproachPoint(icao + ":" + srt.target, grStands);
                    if (pt.lat != 0.0 || pt.lon != 0.0)
                        return pt;
                }
                else
                {
                    GeoPoint hp = graph.HoldingPositionByLabel(srt.target);
                    if (hp.lat != 0.0 || hp.lon != 0.0)
                        return hp;
                }
            }
            GeoPoint pt = TaxiGraph::StandApproachPoint(icao + ":" + standName, grStands);
            if (pt.lat != 0.0 || pt.lon != 0.0)
                return pt;
        }
        return {0.0, 0.0};
    }

    if (label.starts_with("HP:"))
    {
        return graph.HoldingPositionByLabel(label.substr(3));
    }

    // Fallback: try as raw holding position label
    return graph.HoldingPositionByLabel(label);
}

/// @brief Returns true when the position lies inside any taxiOutStands polygon.
static bool InTaxiOutStand(const GeoPoint& pos, const std::map<std::string, airport>& airports)
{
    for (const auto& [_, ap] : airports)
    {
        for (const auto& [__, poly] : ap.taxiOutStands)
        {
            if (poly.lat.empty())
                continue;
            if (CFlowX_LookupsTools::PointInsidePolygon(
                    static_cast<int>(poly.lat.size()),
                    const_cast<double*>(poly.lon.data()),
                    const_cast<double*>(poly.lat.data()),
                    pos.lon, pos.lat))
                return true;
        }
    }
    return false;
}

// ─── Fixture test ────────────────────────────────────────────────────────────

TEST_CASE("TaxiRoute - real world taxi tests")
{
    std::ifstream file("taxi-test.json");
    if (!file.is_open())
    {
        std::cout << "  taxi-test.json not found — skipping\n";
        return;
    }

    json j;
    file >> j;
    if (!j.is_array() || j.empty())
    {
        std::cout << "  taxi-test.json is empty — skipping\n";
        return;
    }

    auto& acc   = accessor();
    int   cases = 0;
    int   pass  = 0;

    bool allPassed = true;

    constexpr const char* RED    = "\x1b[31m";
    constexpr const char* GREEN  = "\x1b[32m";
    constexpr const char* YELLOW = "\x1b[33m";
    constexpr const char* GRAY   = "\x1b[90m";
    constexpr const char* RESET  = "\x1b[0m";

    for (const auto& tc : j)
    {
        const std::string name      = tc.at("name").get<std::string>();
        const std::string type      = tc.at("type").get<std::string>();
        const std::string rwyCfg    = tc.at("runwayConfig").get<std::string>();
        const std::string fromLabel = tc.at("from").get<std::string>();
        const std::string toLabel   = tc.at("to").get<std::string>();
        const double      wingspan  = tc.value("wingspan", 0.0);
        const double      heading   = tc.value("heading", -1.0);

        auto [depRwys, arrRwys] = ParseRunwayConfig(rwyCfg);

        GeoPoint from = ResolvePosition(fromLabel, acc.osmGraph, acc.grStands, acc.airports);
        GeoPoint to   = ResolvePosition(toLabel, acc.osmGraph, acc.grStands, acc.airports);

        // Derive goal-bearing constraint for stand destinations.
        // If the stand has a routing override to another stand, use that stand's heading.
        double goalBrng = -1.0;
        if (toLabel.starts_with("STAND:"))
        {
            std::string standName = toLabel.substr(6);
            for (const auto& [icao, ap] : acc.airports)
            {
                auto ovIt = ap.standRoutingTargets.find(standName);
                if (ovIt != ap.standRoutingTargets.end() && ovIt->second.type == standRoutingTarget::Type::stand)
                    standName = ovIt->second.target;
                auto stIt = acc.grStands.find(icao + ":" + standName);
                if (stIt != acc.grStands.end() && stIt->second.heading.has_value())
                {
                    goalBrng = std::fmod(stIt->second.heading.value() + 180.0, 360.0);
                    break;
                }
            }
        }

        ++cases;
        bool               caseOk = true;
        std::ostringstream buf;

        if (from.lat == 0.0 && from.lon == 0.0)
        {
            std::cout << "\n  " << RED << "[FAIL]" << RESET << " -------- " << name << " --------\n";
            std::cout << "    FAIL: cannot resolve 'from' position: " << fromLabel << "\n\n";
            allPassed = false;
            continue;
        }
        if (to.lat == 0.0 && to.lon == 0.0)
        {
            std::cout << "\n  " << RED << "[FAIL]" << RESET << " -------- " << name << " --------\n";
            std::cout << "    FAIL: cannot resolve 'to' position: " << toLabel << "\n\n";
            allPassed = false;
            continue;
        }

        // Parse optional waypoints array
        std::vector<GeoPoint> waypoints;
        if (tc.contains("waypoints") && tc["waypoints"].is_array())
        {
            for (const auto& wp : tc["waypoints"])
            {
                if (wp.contains("lat") && wp.contains("lon"))
                    waypoints.push_back({wp["lat"].get<double>(), wp["lon"].get<double>()});
            }
        }

        // Parse optional swingover object: {"origin":{lat,lon},"bearing":270.5}
        // When present, routing starts from swingoverOrigin with the captured bearing,
        // reproducing the tail segment that A* computed after the lane-crossing maneuver.
        // distanceRange is skipped for swingover cases because the test only routes the
        // tail; the fixed crossing segment's distance cannot be reproduced here.
        GeoPoint swingoverOrigin  = {};
        double   swingoverBearing = -1.0;
        if (tc.contains("swingover") && tc["swingover"].is_object())
        {
            const auto& sw = tc["swingover"];
            if (sw.contains("origin"))
            {
                swingoverOrigin.lat = sw["origin"]["lat"].get<double>();
                swingoverOrigin.lon = sw["origin"]["lon"].get<double>();
            }
            swingoverBearing = sw.value("bearing", -1.0);
        }

        const bool     hasSwingover = (swingoverOrigin.lat != 0.0 || swingoverOrigin.lon != 0.0);
        const GeoPoint routeFrom    = hasSwingover ? swingoverOrigin : from;
        const bool     forwardOnly  = InTaxiOutStand(from, acc.airports);

        // Use the same heading / forwardOnly logic as the live RadarScreen:
        // heading steers forward/backward candidate split, forwardOnly
        // (taxiOutStand) suppresses backward candidates entirely.
        const double routeBearing = hasSwingover ? swingoverBearing : heading;

        TaxiRoute tail = (waypoints.empty() && !hasSwingover)
                             ? acc.osmGraph.FindRoute(from, to, wingspan, depRwys, arrRwys,
                                                      heading, {}, {}, false, {}, false, forwardOnly, goalBrng)
                             : acc.osmGraph.FindWaypointRoute(routeFrom, waypoints, to, wingspan, depRwys, arrRwys,
                                                              routeBearing, {}, false, {}, false, forwardOnly, goalBrng);

        // For swingover cases, replicate the combined route that RadarScreen builds:
        // fixed crossing segment (from → swingoverOrigin) + tail (swingoverOrigin → to).
        // This gives the full distance and wayRefs including the blue line.
        TaxiRoute route = tail;
        if (hasSwingover && tail.valid)
        {
            std::string fromWayRef = acc.osmGraph.WayRefAt(from, 50.0);
            route.polyline.insert(route.polyline.begin(), from);
            if (!fromWayRef.empty())
                route.wayRefs.insert(route.wayRefs.begin(), fromWayRef);
            route.totalDistM += HaversineM(from, swingoverOrigin);
        }

        buf << "  " << FormatTaxiRoute(route) << "\n";

        if (!route.valid)
        {
            std::cout << "\n  " << RED << "[FAIL]" << RESET << " -------- " << name << " --------\n";
            std::cout << buf.str();
            std::cout << "    FAIL: route is invalid\n\n";
            allPassed = false;
            continue;
        }

        // mustInclude checks
        if (tc.contains("mustInclude"))
        {
            for (const auto& ref : tc["mustInclude"])
            {
                std::string r     = ref.get<std::string>();
                bool        found = std::find(route.wayRefs.begin(), route.wayRefs.end(), r) != route.wayRefs.end();
                if (!found)
                {
                    buf << "    " << YELLOW << "VIOLATION: mustInclude \"" << r << "\" not found in route" << RESET
                        << "\n";
                    caseOk    = false;
                    allPassed = false;
                }
            }
        }

        // mustNotInclude checks
        if (tc.contains("mustNotInclude"))
        {
            for (const auto& ref : tc["mustNotInclude"])
            {
                std::string r      = ref.get<std::string>();
                bool        absent = std::find(route.wayRefs.begin(), route.wayRefs.end(), r) == route.wayRefs.end();
                if (!absent)
                {
                    buf << "    " << YELLOW << "VIOLATION: mustNotInclude \"" << r << "\" found in route" << RESET
                        << "\n";
                    caseOk    = false;
                    allPassed = false;
                }
            }
        }

        // distanceRange check
        if (tc.contains("distanceRange"))
        {
            auto   range   = tc["distanceRange"];
            double minD    = range[0].get<double>();
            double maxD    = range[1].get<double>();
            bool   inRange = route.totalDistM >= minD && route.totalDistM <= maxD;
            if (!inRange)
            {
                buf << "    " << YELLOW << "VIOLATION: distance " << route.totalDistM << " m outside range [" << minD
                    << ", " << maxD << "]" << RESET << "\n";
                caseOk    = false;
                allPassed = false;
            }
        }

        const char* color = caseOk ? GREEN : RED;
        std::cout << "\n  " << color << "[" << (caseOk ? "PASS" : "FAIL") << "]" << RESET << " -------- " << name
                  << " --------\n";
        std::cout << buf.str() << "\n";

        if (caseOk)
            ++pass;
    }

    const int fail = cases - pass;
    std::cout << "\n  " << cases << " tests"
              << "  " << GREEN << pass << " passed" << RESET
              << "  " << (fail > 0 ? RED : GRAY) << fail << " failed" << RESET
              << "\n"
              << std::flush;

    // Single check — details are already printed above.
    CHECK(allPassed);
}
