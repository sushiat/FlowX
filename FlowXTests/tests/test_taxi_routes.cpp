#include "pch.h"
// test_taxi_routes.cpp
// Fixture-driven regression tests for taxi route generation on the real LOWW
// TaxiGraph.  Test cases are defined in fixtures/taxi-test.json and deployed
// to $(OutDir) by the PostBuildEvent.

#include <doctest/doctest.h>
#include <fstream>
#include <iostream>
#include <set>
#include <algorithm>
#include "test_accessor.h"
#include "taxi_graph.h"
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
///   - "STAND:B67"        → stand centroid (prepends "LOWW:")
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
        // Try with each airport ICAO prefix
        for (const auto& [icao, _] : airports)
        {
            GeoPoint pt = TaxiGraph::StandCentroid(icao + ":" + standName, grStands);
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

    for (const auto& tc : j)
    {
        const std::string name      = tc.at("name").get<std::string>();
        const std::string type      = tc.at("type").get<std::string>();
        const std::string rwyCfg    = tc.at("runwayConfig").get<std::string>();
        const std::string fromLabel = tc.at("from").get<std::string>();
        const std::string toLabel   = tc.at("to").get<std::string>();
        const double      wingspan  = tc.value("wingspan", 0.0);

        auto [depRwys, arrRwys] = ParseRunwayConfig(rwyCfg);

        GeoPoint from = ResolvePosition(fromLabel, acc.osmGraph, acc.grStands, acc.airports);
        GeoPoint to   = ResolvePosition(toLabel, acc.osmGraph, acc.grStands, acc.airports);

        ++cases;
        bool caseOk = true;

        if (from.lat == 0.0 && from.lon == 0.0)
        {
            std::cout << "  [" << name << "] FAIL: cannot resolve 'from' position: " << fromLabel << "\n";
            CHECK(false);
            continue;
        }
        if (to.lat == 0.0 && to.lon == 0.0)
        {
            std::cout << "  [" << name << "] FAIL: cannot resolve 'to' position: " << toLabel << "\n";
            CHECK(false);
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

        TaxiRoute route = (waypoints.empty() && !hasSwingover)
                              ? acc.osmGraph.FindRoute(from, to, wingspan, depRwys, arrRwys)
                              : acc.osmGraph.FindWaypointRoute(routeFrom, waypoints, to, wingspan, depRwys, arrRwys,
                                                               swingoverBearing);

        std::string formatted = FormatTaxiRoute(route);
        std::cout << "  [" << name << "] " << formatted << "\n";

        if (!route.valid)
        {
            std::cout << "    FAIL: route is invalid\n";
            CHECK(false);
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
                    std::cout << "    VIOLATION: mustInclude \"" << r << "\" not found in route\n";
                    caseOk = false;
                }
                CHECK_MESSAGE(found, ("mustInclude '" + r + "' not found in route for: " + name).c_str());
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
                    std::cout << "    VIOLATION: mustNotInclude \"" << r << "\" found in route\n";
                    caseOk = false;
                }
                CHECK_MESSAGE(absent, ("mustNotInclude '" + r + "' found in route for: " + name).c_str());
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
                std::cout << "    VIOLATION: distance " << route.totalDistM
                          << " m outside range [" << minD << ", " << maxD << "]\n";
                caseOk = false;
            }
            CHECK_MESSAGE(inRange,
                          std::format("distance {:.0f} m outside [{:.0f}, {:.0f}] for: {}", route.totalDistM, minD, maxD, name).c_str());
        }

        if (caseOk)
            ++pass;
    }

    std::cout << "  " << pass << "/" << cases << " taxi route test(s) passed\n";
}
