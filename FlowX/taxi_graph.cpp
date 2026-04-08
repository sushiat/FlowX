/**
 * @file taxi_graph.cpp
 * @brief Taxiway routing graph implementation: graph building, A* pathfinding, and snapping.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "taxi_graph.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <queue>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Geometry helpers (file-local)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr double EARTH_R = 6'371'000.0; // metres

/// @brief Haversine distance between two WGS-84 points, in metres.
static double HaversineM(const GeoPoint& a, const GeoPoint& b)
{
    const double dLat = (b.lat - a.lat) * std::numbers::pi / 180.0;
    const double dLon = (b.lon - a.lon) * std::numbers::pi / 180.0;
    const double cosA = std::cos(a.lat * std::numbers::pi / 180.0);
    const double cosB = std::cos(b.lat * std::numbers::pi / 180.0);
    const double h    = std::sin(dLat / 2) * std::sin(dLat / 2) + cosA * cosB * std::sin(dLon / 2) * std::sin(dLon / 2);
    return EARTH_R * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
}

/// @brief Forward bearing in degrees [0, 360) from @p a to @p b.
static double BearingDeg(const GeoPoint& a, const GeoPoint& b)
{
    const double lat1 = a.lat * std::numbers::pi / 180.0;
    const double lat2 = b.lat * std::numbers::pi / 180.0;
    const double dLon = (b.lon - a.lon) * std::numbers::pi / 180.0;
    const double y    = std::sin(dLon) * std::cos(lat2);
    const double x    = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    double       deg  = std::atan2(y, x) * 180.0 / std::numbers::pi;
    return std::fmod(deg + 360.0, 360.0);
}

/// @brief Absolute angular difference between two bearings, in degrees [0, 180].
static double BearingDiff(double a, double b)
{
    double d = std::fmod(std::abs(a - b), 360.0);
    return d > 180.0 ? 360.0 - d : d;
}

/// @brief Convert a cardinal direction string ("N"/"S"/"E"/"W") to a bearing in degrees.
static double CardinalToBearing(const std::string& dir)
{
    if (dir == "N")
        return 0.0;
    if (dir == "E")
        return 90.0;
    if (dir == "S")
        return 180.0;
    if (dir == "W")
        return 270.0;
    return -1.0; // unknown
}

// ─────────────────────────────────────────────────────────────────────────────
// Build helpers
// ─────────────────────────────────────────────────────────────────────────────

int TaxiGraph::FindOrCreateNode(const GeoPoint& pos, double mergeThreshM,
                                TaxiNodeType     type,
                                std::string_view label,
                                std::string_view wayRef)
{
    // Linear scan is acceptable: LOWW has ~2 000 nodes at most.
    for (const auto& n : nodes_)
    {
        if (HaversineM(n.pos, pos) <= mergeThreshM)
            return n.id;
    }
    const int id = static_cast<int>(nodes_.size());
    nodes_.push_back({id, pos, type, std::string(label), std::string(wayRef)});
    adj_.emplace_back();
    return id;
}

void TaxiGraph::AddEdge(int from, int to, double cost,
                        const std::string& wayRef, double bearingDeg)
{
    adj_[from].push_back({to, cost, wayRef, bearingDeg});
}

double TaxiGraph::FlowMult(double                       bearingDeg,
                           const std::string&           wayRef,
                           const std::set<std::string>& activeDepRwys) const
{
    constexpr double WITH_FLOW_MAX = 45.0;
    constexpr double AGAINST_FLOW  = 135.0;
    constexpr double AGAINST_MULT  = 3.0;

    auto checkRules = [&](const std::vector<TaxiFlowRule>& rules) -> double
    {
        for (const auto& rule : rules)
        {
            if (rule.taxiway != wayRef)
                continue;
            const double ruleBearing = CardinalToBearing(rule.direction);
            if (ruleBearing < 0.0)
                continue;
            const double diff = BearingDiff(bearingDeg, ruleBearing);
            if (diff <= WITH_FLOW_MAX)
                return 1.0;
            if (diff >= AGAINST_FLOW)
                return AGAINST_MULT;
        }
        return 1.0; // no rule applies → neutral
    };

    // Check generic rules first.
    double mult = checkRules(apt_.taxiFlowGeneric);
    if (mult > 1.0)
        return mult;

    // Check departure-specific rules for each active runway.
    for (const auto& rwyDes : activeDepRwys)
    {
        auto it = apt_.runways.find(rwyDes);
        if (it == apt_.runways.end())
            continue;
        mult = checkRules(it->second.taxiFlowDep);
        if (mult > 1.0)
            return mult;
    }
    return 1.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::Build
// ─────────────────────────────────────────────────────────────────────────────

void TaxiGraph::Build(const OsmAirportData& osm, const airport& ap)
{
    nodes_.clear();
    adj_.clear();
    apt_ = ap;

    // Use generic rules only during graph building (departure rules are applied
    // per-query at FindRoute time via the activeDepRwys parameter).
    const std::set<std::string> noDepRwys;

    // Type multipliers (distance × mult = base cost).
    constexpr double MULT_TAXIWAY      = 1.0;
    constexpr double MULT_HOLDINGPOINT = 1.0;
    constexpr double MULT_INTERSECTION = 1.1;
    constexpr double MULT_TAXILANE     = 3.0;  // stand-access taxilanes; strongly discouraged vs main taxiways
    constexpr double MULT_RUNWAY       = 20.0; // runways; only used to get an aircraft off the runway; never preferred for taxi

    // ── Step 1: ways ─────────────────────────────────────────────────────────
    for (const auto& way : osm.ways)
    {
        if (way.type == AerowayType::Unknown)
            continue;

        const double typeMult =
            (way.type == AerowayType::Taxiway_HoldingPoint) ? MULT_HOLDINGPOINT
            : (way.type == AerowayType::Taxiway_Intersection) ? MULT_INTERSECTION
            : (way.type == AerowayType::Taxilane)             ? MULT_TAXILANE
            : (way.type == AerowayType::Runway)               ? MULT_RUNWAY
                                                              : MULT_TAXIWAY;

        const std::string& ref = way.ref.empty() ? way.name : way.ref;

        for (size_t k = 1; k < way.geometry.size(); ++k)
        {
            const GeoPoint& a    = way.geometry[k - 1];
            const GeoPoint& b    = way.geometry[k];
            const double    dist = HaversineM(a, b);
            if (dist < 0.1)
                continue; // skip degenerate segments

            // Runway nodes use a larger merge threshold so they snap onto nearby taxiway exit nodes.
            const double mergeThresh = (way.type == AerowayType::Runway) ? 15.0 : 5.0;
            const int nA = FindOrCreateNode(a, mergeThresh, TaxiNodeType::Waypoint, ref, ref);
            const int nB = FindOrCreateNode(b, mergeThresh, TaxiNodeType::Waypoint, ref, ref);
            if (nA == nB)
                continue;

            const double bAB = BearingDeg(a, b);
            const double bBA = BearingDeg(b, a);

            const double costAB = dist * typeMult * FlowMult(bAB, ref, noDepRwys);
            const double costBA = dist * typeMult * FlowMult(bBA, ref, noDepRwys);

            AddEdge(nA, nB, costAB, ref, bAB);
            AddEdge(nB, nA, costBA, ref, bBA);
        }
    }

    // ── Step 2: OSM holding position nodes ───────────────────────────────────
    for (const auto& hp : osm.holdingPositions)
    {
        const std::string& label  = hp.ref.empty() ? hp.name : hp.ref;
        const int          hpNode = FindOrCreateNode(hp.pos, 20.0,
                                                     TaxiNodeType::HoldingPosition, label, "");
        // Connect to nearest waypoint within 25 m.
        double bestDist = 25.0;
        int    bestId   = -1;
        for (const auto& n : nodes_)
        {
            if (n.type != TaxiNodeType::Waypoint)
                continue;
            const double d = HaversineM(n.pos, hp.pos);
            if (d < bestDist)
            {
                bestDist = d;
                bestId   = n.id;
            }
        }
        if (bestId >= 0 && bestId != hpNode)
        {
            AddEdge(hpNode, bestId, bestDist, "", BearingDeg(hp.pos, nodes_[bestId].pos));
            AddEdge(bestId, hpNode, bestDist, "", BearingDeg(nodes_[bestId].pos, hp.pos));
        }
    }

    // ── Step 3: config holding points ────────────────────────────────────────
    for (const auto& [rwyDes, rwy] : ap.runways)
    {
        for (const auto& [hpName, hp] : rwy.holdingPoints)
        {
            if (!hp.assignable)
                continue;
            GeoPoint  hpPos{hp.centerLat, hp.centerLon};
            const int hpNode   = FindOrCreateNode(hpPos, 20.0,
                                                  TaxiNodeType::HoldingPoint, hpName, "");
            double    bestDist = 30.0;
            int       bestId   = -1;
            for (const auto& n : nodes_)
            {
                if (n.type != TaxiNodeType::Waypoint)
                    continue;
                const double d = HaversineM(n.pos, hpPos);
                if (d < bestDist)
                {
                    bestDist = d;
                    bestId   = n.id;
                }
            }
            if (bestId >= 0 && bestId != hpNode)
            {
                AddEdge(hpNode, bestId, bestDist, "", BearingDeg(hpPos, nodes_[bestId].pos));
                AddEdge(bestId, hpNode, bestDist, "", BearingDeg(nodes_[bestId].pos, hpPos));
            }
        }
    }

    // ── Step 4: stands ───────────────────────────────────────────────────────
    // (Stand nodes are added lazily in StandCentroid and FindRoute; we skip bulk
    //  stand insertion during Build to avoid loading thousands of irrelevant stands.)
}

// ─────────────────────────────────────────────────────────────────────────────
// A* helpers
// ─────────────────────────────────────────────────────────────────────────────

int TaxiGraph::NearestNode(const GeoPoint& pos) const
{
    if (nodes_.empty())
        return -1;
    double bestD  = std::numeric_limits<double>::max();
    int    bestId = 0;
    for (const auto& n : nodes_)
    {
        const double d = HaversineM(n.pos, pos);
        if (d < bestD)
        {
            bestD  = d;
            bestId = n.id;
        }
    }
    return bestId;
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::FindRoute  (A*)
// ─────────────────────────────────────────────────────────────────────────────

TaxiRoute TaxiGraph::FindRoute(const GeoPoint&              from,
                               const GeoPoint&              to,
                               double                       wingspanM,
                               const std::set<std::string>& activeDepRwys,
                               double                       initialBearingDeg) const
{
    if (nodes_.empty())
        return {};

    const int startId = NearestNode(from);
    const int goalId  = NearestNode(to);
    if (startId == goalId)
    {
        TaxiRoute r;
        r.polyline   = {nodes_[startId].pos};
        r.totalDistM = 0.0;
        r.valid      = true;
        return r;
    }

    // Per-edge wingspan check: build a set of excluded wayRefs.
    std::set<std::string> excludedRefs;
    if (wingspanM > 0.0)
    {
        for (const auto& [ref, maxWs] : apt_.taxiWingspanMax)
            if (wingspanM > maxWs)
                excludedRefs.insert(ref);
    }

    constexpr int    MAX_NODES    = 5000;
    constexpr double TURN_PENALTY = 200.0; // metres equivalent; strong enough to prefer straight over any detour

    const GeoPoint& goalPos = nodes_[goalId].pos;

    struct PQEntry
    {
        double f;
        int    id;
    };
    auto cmp = [](const PQEntry& a, const PQEntry& b)
    { return a.f > b.f; };
    std::priority_queue<PQEntry, std::vector<PQEntry>, decltype(cmp)> open(cmp);

    // 500 m per wayref switch. With ×3 flow multiplier, the worst flow savings on any realistic
    // segment (≤300 m) is 2×300 = 600 m. Two wayref changes + turn penalty (200+2×500 = 1200 m)
    // always exceeds that, preventing side-taxiway detours.
    constexpr double WAYREF_CHANGE_PENALTY = 500.0;

    std::vector<double>      g(nodes_.size(), std::numeric_limits<double>::max());
    std::vector<int>         prev(nodes_.size(), -1);
    std::vector<double>      incomingBearing(nodes_.size(), -1.0);
    std::vector<std::string> incomingWayRef(nodes_.size(), "");
    int                      nodesExpanded = 0;

    // Seed the aircraft's current heading so the first edge is turn-checked.
    incomingBearing[startId] = initialBearingDeg;

    g[startId] = 0.0;
    open.push({HaversineM(nodes_[startId].pos, goalPos), startId});

    while (!open.empty())
    {
        const auto [f, cur] = open.top();
        open.pop();

        if (cur == goalId)
            break;
        if (++nodesExpanded > MAX_NODES)
            break;

        // Apply departure-rule cost multipliers at query time (on top of graph edge costs).
        for (const auto& edge : adj_[cur])
        {
            if (!excludedRefs.empty() && excludedRefs.contains(edge.wayRef))
                continue;

            // Re-apply departure rule multiplier (generic rules already baked in, dep rules added here).
            double depMult = 1.0;
            if (!activeDepRwys.empty() && !edge.wayRef.empty())
            {
                for (const auto& rwyDes : activeDepRwys)
                {
                    auto it = apt_.runways.find(rwyDes);
                    if (it == apt_.runways.end())
                        continue;
                    for (const auto& rule : it->second.taxiFlowDep)
                    {
                        if (rule.taxiway != edge.wayRef)
                            continue;
                        const double rb = CardinalToBearing(rule.direction);
                        if (rb < 0.0)
                            continue;
                        const double diff = BearingDiff(edge.bearingDeg, rb);
                        if (diff >= 135.0)
                        {
                            depMult = 3.0; // same multiplier as generic flow rules
                        }
                        break;
                    }
                    if (depMult > 1.0)
                        break;
                }
            }

            // Hard-block sharp turns (>120°); penalise turns beyond 85°.
            // 120° catches tight S-turns / near-reversals that large aircraft cannot execute.
            double turnPenalty = 0.0;
            if (incomingBearing[cur] >= 0.0)
            {
                const double diff = BearingDiff(incomingBearing[cur], edge.bearingDeg);
                if (diff > 120.0)
                    continue; // turn too sharp for normal aircraft operations
                if (diff > 85.0)
                    turnPenalty = TURN_PENALTY;
            }

            // Penalise switching between named taxiways (prefer staying on the same ref).
            if (!incomingWayRef[cur].empty() && !edge.wayRef.empty() &&
                incomingWayRef[cur] != edge.wayRef)
                turnPenalty += WAYREF_CHANGE_PENALTY;

            const double ng = g[cur] + edge.cost * depMult + turnPenalty;
            if (ng < g[edge.to])
            {
                g[edge.to]               = ng;
                prev[edge.to]            = cur;
                incomingBearing[edge.to] = edge.bearingDeg;
                incomingWayRef[edge.to]  = edge.wayRef;
                const double h           = HaversineM(nodes_[edge.to].pos, goalPos);
                open.push({ng + h, edge.to});
            }
        }
    }

    if (prev[goalId] == -1 && goalId != startId)
        return {}; // no path

    // Reconstruct path.
    TaxiRoute route;
    route.valid = true;
    std::vector<int> path;
    for (int n = goalId; n != -1; n = prev[n])
        path.push_back(n);
    std::ranges::reverse(path);

    std::string lastRef;
    for (const int nId : path)
    {
        route.polyline.push_back(nodes_[nId].pos);
        const std::string& ref = nodes_[nId].wayRef;
        if (!ref.empty() && ref != lastRef)
        {
            route.wayRefs.push_back(ref);
            lastRef = ref;
        }
        if (nId != path.front())
            route.totalDistM += HaversineM(nodes_[prev[nId] == -1 ? nId : prev[nId]].pos,
                                           nodes_[nId].pos);
    }

    // Record exit bearing from the last edge so FindWaypointRoute can chain segments.
    if (path.size() >= 2)
    {
        const GeoPoint& a  = nodes_[path[path.size() - 2]].pos;
        const GeoPoint& b  = nodes_[path.back()].pos;
        route.exitBearing  = BearingDeg(a, b);
    }

    return route;
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::FindWaypointRoute
// ─────────────────────────────────────────────────────────────────────────────

TaxiRoute TaxiGraph::FindWaypointRoute(const GeoPoint&              origin,
                                       const std::vector<GeoPoint>& waypoints,
                                       const GeoPoint&              dest,
                                       double                       wingspanM,
                                       const std::set<std::string>& activeDepRwys,
                                       double                       initialBearingDeg) const
{
    std::vector<GeoPoint> stops;
    stops.push_back(origin);
    for (const auto& wp : waypoints)
        stops.push_back(wp);
    stops.push_back(dest);

    TaxiRoute combined;
    combined.valid         = true;
    double segInitBearing  = initialBearingDeg; // propagated across segment boundaries

    for (size_t i = 1; i < stops.size(); ++i)
    {
        TaxiRoute seg = FindRoute(stops[i - 1], stops[i], wingspanM, activeDepRwys, segInitBearing);
        if (!seg.valid)
        {
            combined.valid = false;
            return combined;
        }
        // Chain: the exit bearing of this segment becomes the initial bearing of the next.
        segInitBearing = seg.exitBearing;

        // Avoid duplicate junction points when concatenating.
        if (!combined.polyline.empty() && !seg.polyline.empty())
            seg.polyline.erase(seg.polyline.begin());
        combined.polyline.insert(combined.polyline.end(), seg.polyline.begin(), seg.polyline.end());
        for (const auto& ref : seg.wayRefs)
            if (combined.wayRefs.empty() || combined.wayRefs.back() != ref)
                combined.wayRefs.push_back(ref);
        combined.totalDistM += seg.totalDistM;
        combined.exitBearing = seg.exitBearing;
    }
    return combined;
}

// ─────────────────────────────────────────────────────────────────────────────
// Snapping
// ─────────────────────────────────────────────────────────────────────────────

std::pair<GeoPoint, std::string> TaxiGraph::SnapNearest(const GeoPoint& rawPos,
                                                        double          maxM) const
{
    double bestD  = maxM;
    int    bestId = -1;
    for (const auto& n : nodes_)
    {
        const double d = HaversineM(n.pos, rawPos);
        if (d < bestD)
        {
            bestD  = d;
            bestId = n.id;
        }
    }
    if (bestId < 0)
        return {rawPos, ""};
    return {nodes_[bestId].pos, nodes_[bestId].label};
}

GeoPoint TaxiGraph::SnapForPlanning(const GeoPoint&  rawPos,
                                    const TaxiRoute& suggested) const
{
    constexpr double HP_SNAP_M    = 30.0;
    constexpr double ISX_SNAP_M   = 15.0;
    constexpr double ROUTE_SNAP_M = 20.0;
    constexpr double WP_SNAP_M    = 40.0;

    // Priority 1: holding points / holding positions.
    {
        double bestD  = HP_SNAP_M;
        int    bestId = -1;
        for (const auto& n : nodes_)
        {
            if (n.type != TaxiNodeType::HoldingPoint &&
                n.type != TaxiNodeType::HoldingPosition)
                continue;
            const double d = HaversineM(n.pos, rawPos);
            if (d < bestD)
            {
                bestD  = d;
                bestId = n.id;
            }
        }
        if (bestId >= 0)
            return nodes_[bestId].pos;
    }

    // Priority 2: intersection waypoints (nodes on Taxiway_Intersection ways).
    // We detect these by checking whether any edge from the node belongs to an
    // intersection way — but we don't store type on the node directly.
    // Instead, use nodes whose wayRef is a key in osmData intersections list.
    // As a proxy: nodes whose label starts with "Exit" (LOWW-specific but reasonable).
    {
        double bestD  = ISX_SNAP_M;
        int    bestId = -1;
        for (const auto& n : nodes_)
        {
            if (n.type != TaxiNodeType::Waypoint)
                continue;
            if (n.label.find("Exit") == std::string::npos)
                continue;
            const double d = HaversineM(n.pos, rawPos);
            if (d < bestD)
            {
                bestD  = d;
                bestId = n.id;
            }
        }
        if (bestId >= 0)
            return nodes_[bestId].pos;
    }

    // Priority 3: nearest point on suggested route polyline.
    if (suggested.valid && suggested.polyline.size() >= 2)
    {
        double   bestD   = ROUTE_SNAP_M;
        GeoPoint bestPt  = rawPos;
        bool     snapped = false;
        for (size_t i = 1; i < suggested.polyline.size(); ++i)
        {
            // Snap to the endpoint of the segment (nearest vertex on polyline).
            const double d = HaversineM(rawPos, suggested.polyline[i]);
            if (d < bestD)
            {
                bestD   = d;
                bestPt  = suggested.polyline[i];
                snapped = true;
            }
        }
        if (snapped)
            return bestPt;
    }

    // Priority 4: nearest waypoint.
    {
        double bestD  = WP_SNAP_M;
        int    bestId = -1;
        for (const auto& n : nodes_)
        {
            if (n.type != TaxiNodeType::Waypoint)
                continue;
            const double d = HaversineM(n.pos, rawPos);
            if (d < bestD)
            {
                bestD  = d;
                bestId = n.id;
            }
        }
        if (bestId >= 0)
            return nodes_[bestId].pos;
    }

    return rawPos;
}

// ─────────────────────────────────────────────────────────────────────────────
// Destination helpers
// ─────────────────────────────────────────────────────────────────────────────

GeoPoint TaxiGraph::BestDepartureHP(const std::set<std::string>& activeDepRwys,
                                    const airport&               ap) const
{
    for (const auto& rwyDes : activeDepRwys)
    {
        auto it = ap.runways.find(rwyDes);
        if (it == ap.runways.end())
            continue;
        const auto& hps = it->second.holdingPoints;
        if (hps.empty())
            continue;

        // Pick the first assignable HP in config order (lowest insertion index).
        const holdingPoint* best = nullptr;
        for (const auto& [name, hp] : hps)
        {
            if (!hp.assignable)
                continue;
            if (!best || hp.order < best->order)
                best = &hp;
        }
        if (best)
            return {best->centerLat, best->centerLon};
    }
    return {};
}

GeoPoint TaxiGraph::StandCentroid(const std::string&                    icaoStandKey,
                                  const std::map<std::string, grStand>& grStands)
{
    auto it = grStands.find(icaoStandKey);
    if (it == grStands.end())
        return {};
    const auto& st = it->second;
    if (st.lat.empty())
        return {};
    double sumLat = 0.0, sumLon = 0.0;
    for (size_t i = 0; i < st.lat.size(); ++i)
    {
        sumLat += st.lat[i];
        sumLon += st.lon[i];
    }
    return {sumLat / static_cast<double>(st.lat.size()),
            sumLon / static_cast<double>(st.lat.size())};
}

// ─────────────────────────────────────────────────────────────────────────────
// FormatRoute (debug helper, declared extern in taxi_graph.h via forward reference)
// ─────────────────────────────────────────────────────────────────────────────

std::string FormatTaxiRoute(const TaxiRoute& route)
{
    if (!route.valid)
        return "(no route)";
    std::string s;
    for (const auto& ref : route.wayRefs)
    {
        if (!s.empty())
            s += " -> ";
        s += '[';
        s += ref;
        s += ']';
    }
    s += std::format(" | {:.0f} m", route.totalDistM);
    return s;
}
