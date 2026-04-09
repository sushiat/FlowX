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
// HaversineM, BearingDeg, BearingDiff are now inline free functions in taxi_graph.h.

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

static constexpr double GRID_CELL_M = 15.0; ///< Spatial grid cell size in metres.

// ─────────────────────────────────────────────────────────────────────────────
// Build helpers
// ─────────────────────────────────────────────────────────────────────────────

int TaxiGraph::FindOrCreateNode(const GeoPoint& pos, double mergeThreshM,
                                TaxiNodeType     type,
                                std::string_view label,
                                std::string_view wayRef)
{
    // Grid-accelerated lookup: only scan cells within mergeThreshM.
    const int rings = static_cast<int>(std::ceil(mergeThreshM / GRID_CELL_M)) + 1;
    auto [cx0, cy0] = GridCell(pos);
    for (int dx = -rings; dx <= rings; ++dx)
    {
        for (int dy = -rings; dy <= rings; ++dy)
        {
            auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
            if (it == grid_.end())
                continue;
            for (const int nid : it->second)
            {
                const double d = HaversineM(nodes_[nid].pos, pos);
                if (d > mergeThreshM)
                    continue;
                // Prevent parallel taxiway nodes from merging: if both have different named
                // refs, require distance <= 1 m. Genuine OSM junction nodes share
                // coordinates (0 m); OSM endpoint threshold is 1 m for float precision.
                // Nodes with empty refs (HPs, stands) are exempt.
                if (!wayRef.empty() && !nodes_[nid].wayRef.empty() &&
                    nodes_[nid].wayRef != wayRef && d > 1.0)
                    continue;
                return nid;
            }
        }
    }
    const int id = static_cast<int>(nodes_.size());
    nodes_.push_back({id, pos, type, std::string(label), std::string(wayRef)});
    adj_.emplace_back();
    GridInsert(id);
    return id;
}

void TaxiGraph::AddEdge(int from, int to, double cost,
                        const std::string& wayRef, double bearingDeg)
{
    adj_[from].push_back({to, cost, wayRef, bearingDeg});
}

double TaxiGraph::FlowMult(double bearingDeg, const std::string& wayRef) const
{
    const double WITH_FLOW_MAX = apt_.taxiNetworkConfig.flowRules.withFlowMaxDeg;
    const double AGAINST_FLOW  = apt_.taxiNetworkConfig.flowRules.againstFlowMinDeg;
    const double AGAINST_MULT  = apt_.taxiNetworkConfig.flowRules.againstFlowMult;

    for (const auto& rule : apt_.taxiFlowGeneric)
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
    return 1.0;
}

/// @brief Builds a canonical taxiFlowConfigs key from sorted dep/arr runway sets.
/// Sets are already lexicographically ordered; elements are joined with '/' and
/// separated by '_' (e.g. {"16","29"} dep + {"16"} arr → "16/29_16").
static std::string BuildConfigKey(const std::set<std::string>& dep,
                                  const std::set<std::string>& arr)
{
    auto join = [](const std::set<std::string>& s) -> std::string
    {
        std::string out;
        for (const auto& r : s)
        {
            if (!out.empty())
                out += '/';
            out += r;
        }
        return out;
    };
    return join(dep) + '_' + join(arr);
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::Build
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Spatial grid helpers
// ─────────────────────────────────────────────────────────────────────────────

std::pair<int, int> TaxiGraph::GridCell(const GeoPoint& p) const
{
    return {static_cast<int>(std::floor(p.lat / gridLatStep_)),
            static_cast<int>(std::floor(p.lon / gridLonStep_))};
}

int64_t TaxiGraph::GridKey(int cx, int cy)
{
    return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
}

void TaxiGraph::GridInsert(int id)
{
    auto [cx, cy] = GridCell(nodes_[id].pos);
    grid_[GridKey(cx, cy)].push_back(id);
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::Build
// ─────────────────────────────────────────────────────────────────────────────

void TaxiGraph::Build(const OsmAirportData& osm, const airport& ap)
{
    nodes_.clear();
    adj_.clear();
    grid_.clear();
    isxWayRefs_.clear();
    apt_ = ap;

    // Initialise grid step sizes from the first available geometry point.
    // Using a fixed cosLat for the airport area is accurate enough over ~5 km.
    gridLatStep_ = 1.0;
    gridLonStep_ = 1.0;
    for (const auto& way : osm.ways)
    {
        if (!way.geometry.empty())
        {
            const double cosLat = std::cos(way.geometry[0].lat * std::numbers::pi / 180.0);
            gridLatStep_        = GRID_CELL_M / TAXI_EARTH_R * 180.0 / std::numbers::pi;
            gridLonStep_        = GRID_CELL_M / (TAXI_EARTH_R * cosLat) * 180.0 / std::numbers::pi;
            break;
        }
    }

    // Only generic rules are baked into edge costs at build time.
    // Config-specific rules (taxiFlowConfigs) are applied per-query in RunAStar.

    // Type multipliers (distance × mult = base cost).
    constexpr double MULT_TAXIWAY      = 1.0;
    constexpr double MULT_HOLDINGPOINT = 1.0;
    const double     MULT_INTERSECTION = apt_.taxiNetworkConfig.edgeCosts.multIntersection;
    const double     MULT_TAXILANE     = apt_.taxiNetworkConfig.edgeCosts.multTaxilane;
    const double     MULT_RUNWAY       = apt_.taxiNetworkConfig.edgeCosts.multRunway;

    // ── Step 1: ways ─────────────────────────────────────────────────────────
    for (const auto& way : osm.ways)
    {
        if (way.type == AerowayType::Unknown)
            continue;

        const double typeMult =
            (way.type == AerowayType::Taxiway_HoldingPoint)   ? MULT_HOLDINGPOINT
            : (way.type == AerowayType::Taxiway_Intersection) ? MULT_INTERSECTION
            : (way.type == AerowayType::Taxilane)             ? MULT_TAXILANE
            : (way.type == AerowayType::Runway)               ? MULT_RUNWAY
                                                              : MULT_TAXIWAY;

        const std::string& ref = way.ref.empty() ? way.name : way.ref;

        if (way.type == AerowayType::Taxiway_Intersection && !ref.empty())
            isxWayRefs_.insert(ref);

        for (size_t k = 1; k < way.geometry.size(); ++k)
        {
            const GeoPoint& a    = way.geometry[k - 1];
            const GeoPoint& b    = way.geometry[k];
            const double    dist = HaversineM(a, b);
            if (dist < 0.1)
                continue; // skip degenerate segments

            const double bAB  = BearingDeg(a, b);
            const double bBA  = BearingDeg(b, a);
            const double fmAB = FlowMult(bAB, ref) * typeMult;
            const double fmBA = FlowMult(bBA, ref) * typeMult;

            // Subdivide long straight segments so there is a node every SUBDIV_M metres.
            // OSM geometry may place nodes hundreds of metres apart on runways/taxiways;
            // dense nodes improve aircraft snapping and deviation detection accuracy.
            // Interpolated nodes use a 0.001 m merge (float-precision only) so they never
            // cross-merge with nodes from adjacent ways.
            // OSM endpoints use 1.0 m (not 5 m) — genuine shared junction nodes are at
            // exactly 0 m in OSM data, so 1 m handles float precision without merging
            // nearby same-ref nodes on different fillet branches at intersections.
            const double SUBDIV_M = apt_.taxiNetworkConfig.graph.subdivisionIntervalM;
            const int    nSteps   = static_cast<int>(std::floor(dist / SUBDIV_M));

            int prev = FindOrCreateNode(a, 1.0, TaxiNodeType::Waypoint, ref, ref);

            for (int s = 1; s < nSteps; ++s)
            {
                const double   t   = static_cast<double>(s) * SUBDIV_M / dist;
                const GeoPoint mid = {a.lat + (b.lat - a.lat) * t,
                                      a.lon + (b.lon - a.lon) * t};
                const int      cur = FindOrCreateNode(mid, 0.001, TaxiNodeType::Waypoint, ref, ref);
                if (cur == prev)
                    continue;
                const double d = HaversineM(nodes_[prev].pos, nodes_[cur].pos);
                AddEdge(prev, cur, d * fmAB, ref, bAB);
                AddEdge(cur, prev, d * fmBA, ref, bBA);
                prev = cur;
            }

            const int nB = FindOrCreateNode(b, 1.0, TaxiNodeType::Waypoint, ref, ref);
            if (nB != prev)
            {
                const double d = HaversineM(nodes_[prev].pos, nodes_[nB].pos);
                AddEdge(prev, nB, d * fmAB, ref, bAB);
                AddEdge(nB, prev, d * fmBA, ref, bBA);
            }
        }
    }

    // ── Step 2: OSM holding position nodes ───────────────────────────────────
    // Promote the nearest existing Waypoint node to HoldingPosition type rather than
    // creating a separate node with a long connecting edge. The OSM stop-bar node is
    // typically already on the taxiway centreline; snapping within osmHoldingPositionSnapM is sufficient.
    for (const auto& hp : osm.holdingPositions)
    {
        const std::string& label    = hp.ref.empty() ? hp.name : hp.ref;
        double             bestDist = apt_.taxiNetworkConfig.graph.osmHoldingPositionSnapM;
        int                bestId   = -1;
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
        if (bestId >= 0)
        {
            nodes_[bestId].type  = TaxiNodeType::HoldingPosition;
            nodes_[bestId].label = label;
        }
    }

    // ── Step 3: config holding points ────────────────────────────────────────
    // Same approach: promote the nearest Waypoint node rather than adding a separate
    // node + long edge. Search up to configHoldingPointSnapM because config HP centres
    // are polygon centroids that may sit a few metres back from the taxiway edge.
    for (const auto& [rwyDes, rwy] : ap.runways)
    {
        for (const auto& [hpName, hp] : rwy.holdingPoints)
        {
            if (!hp.assignable)
                continue;
            const GeoPoint hpPos{hp.centerLat, hp.centerLon};
            double         bestDist = apt_.taxiNetworkConfig.graph.configHoldingPointSnapM;
            int            bestId   = -1;
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
            if (bestId >= 0)
            {
                nodes_[bestId].type  = TaxiNodeType::HoldingPoint;
                nodes_[bestId].label = hpName;
            }
        }
    }

    // ── Step 5: stands ───────────────────────────────────────────────────────
    // (Stand nodes are added lazily in StandCentroid and FindRoute; we skip bulk
    //  stand insertion during Build to avoid loading thousands of irrelevant stands.)

    // ── Steps 6+: gap bridging and post-cleanup removed ──────────────────────
    // FindOrCreateNode already merges different-ref nodes within 1 m (the GPS
    // noise tolerance for genuine OSM junction nodes). Gap-bridging is redundant
    // because all real intersections share exact OSM nodes (≤ 0.1 m), and any
    // edge-removal pass broad enough to catch phantom parallel-lane connections
    // also removes legitimate cross-taxiway edges from junction nodes (breaking
    // all intersection routing). The 1 m merge threshold in FindOrCreateNode is
    // the single, sufficient safeguard against phantom parallel-lane merges.

    // ── Step 7: build typed node indices ────────────────────────────────────
    // hpNodeIds_ / isxNodeIds_ for O(k) SnapForPlanning scans.
    // wayRefNodes_ for O(k) SwingoverSnap wayRef-filtered scans.
    // isxWayRefs_ was populated during Step 1 from way.type == Taxiway_Intersection.
    hpNodeIds_.clear();
    isxNodeIds_.clear();
    wayRefNodes_.clear();
    for (const auto& n : nodes_)
    {
        if (n.type == TaxiNodeType::HoldingPoint || n.type == TaxiNodeType::HoldingPosition)
            hpNodeIds_.push_back(n.id);
        else if (n.type == TaxiNodeType::Waypoint)
        {
            if (n.label.find("Exit") != std::string::npos)
                isxNodeIds_.push_back(n.id);
            if (!n.wayRef.empty())
                wayRefNodes_[n.wayRef].push_back(n.id);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// A* helpers
// ─────────────────────────────────────────────────────────────────────────────

int TaxiGraph::NearestNode(const GeoPoint& pos) const
{
    if (nodes_.empty())
        return -1;

    // Expanding-ring search: start at the cell containing pos, expand one ring at a time.
    // Stop once the inner edge of the next ring is further than the current best distance.
    auto [cx0, cy0] = GridCell(pos);
    double bestD    = std::numeric_limits<double>::max();
    int    bestId   = 0;

    for (int ring = 0;; ++ring)
    {
        // Inner edge of this ring is at least (ring - 1) * GRID_CELL_M away.
        if (ring > 0 && (ring - 1) * GRID_CELL_M >= bestD)
            break;

        for (int dx = -ring; dx <= ring; ++dx)
        {
            for (int dy = -ring; dy <= ring; ++dy)
            {
                if (ring > 0 && std::abs(dx) < ring && std::abs(dy) < ring)
                    continue; // skip interior — already checked in previous rings
                auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
                if (it == grid_.end())
                    continue;
                for (const int id : it->second)
                {
                    const double d = HaversineM(nodes_[id].pos, pos);
                    if (d < bestD)
                    {
                        bestD  = d;
                        bestId = id;
                    }
                }
            }
        }
    }
    return bestId;
}

int TaxiGraph::NearestForwardNode(const GeoPoint& pos, double headingDeg, double maxM) const
{
    double    bestD  = std::numeric_limits<double>::max();
    int       bestId = -1;
    const int rings  = static_cast<int>(std::ceil(maxM / GRID_CELL_M)) + 1;
    auto [cx0, cy0]  = GridCell(pos);

    for (int dx = -rings; dx <= rings; ++dx)
    {
        for (int dy = -rings; dy <= rings; ++dy)
        {
            auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
            if (it == grid_.end())
                continue;
            for (const int id : it->second)
            {
                const double d = HaversineM(nodes_[id].pos, pos);
                if (d > maxM || d >= bestD)
                    continue;
                if (BearingDiff(BearingDeg(pos, nodes_[id].pos), headingDeg) <= 90.0)
                {
                    bestD  = d;
                    bestId = id;
                }
            }
        }
    }
    return bestId;
}

int TaxiGraph::NearestBackwardNode(const GeoPoint& pos, double headingDeg, double maxM) const
{
    double    bestD  = std::numeric_limits<double>::max();
    int       bestId = -1;
    const int rings  = static_cast<int>(std::ceil(maxM / GRID_CELL_M)) + 1;
    auto [cx0, cy0]  = GridCell(pos);

    for (int dx = -rings; dx <= rings; ++dx)
    {
        for (int dy = -rings; dy <= rings; ++dy)
        {
            auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
            if (it == grid_.end())
                continue;
            for (const int id : it->second)
            {
                const double d = HaversineM(nodes_[id].pos, pos);
                if (d > maxM || d >= bestD)
                    continue;
                if (BearingDiff(BearingDeg(pos, nodes_[id].pos), headingDeg) > 90.0)
                {
                    bestD  = d;
                    bestId = id;
                }
            }
        }
    }
    return bestId;
}

std::vector<int> TaxiGraph::NearestCandidateNodes(
    const GeoPoint& pos, double headingDeg, double maxFwdM, double maxBwdM, int maxFwd,
    int maxBwd) const
{
    struct Cand
    {
        double dist;
        int    id;
    };
    std::vector<Cand> fwd, bwd;

    const double maxR  = std::max(maxFwdM, maxBwdM);
    const int    rings = static_cast<int>(std::ceil(maxR / GRID_CELL_M)) + 1;
    auto [cx0, cy0]    = GridCell(pos);

    for (int dx = -rings; dx <= rings; ++dx)
    {
        for (int dy = -rings; dy <= rings; ++dy)
        {
            auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
            if (it == grid_.end())
                continue;
            for (const int id : it->second)
            {
                const double d = HaversineM(nodes_[id].pos, pos);
                if (headingDeg >= 0.0)
                {
                    const double diff = BearingDiff(BearingDeg(pos, nodes_[id].pos), headingDeg);
                    if (diff <= 90.0 && d <= maxFwdM)
                        fwd.push_back({d, id});
                    else if (diff > 90.0 && d <= maxBwdM)
                        bwd.push_back({d, id});
                }
                else if (d <= maxFwdM)
                {
                    fwd.push_back({d, id});
                }
            }
        }
    }

    std::ranges::sort(fwd, {}, &Cand::dist);
    std::ranges::sort(bwd, {}, &Cand::dist);

    std::vector<int> result;
    result.reserve(maxFwd + maxBwd);
    for (int i = 0; i < maxFwd && i < static_cast<int>(fwd.size()); ++i)
        result.push_back(fwd[i].id);
    for (int i = 0; i < maxBwd && i < static_cast<int>(bwd.size()); ++i)
        result.push_back(bwd[i].id);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::FindRoute  (A*)
// ─────────────────────────────────────────────────────────────────────────────

TaxiRoute TaxiGraph::RunAStar(int                          startId,
                              int                          goalId,
                              const std::set<std::string>& excludedRefs,
                              const std::set<int>&         blockedNodes,
                              const std::set<std::string>& activeDepRwys,
                              const std::set<std::string>& activeArrRwys,
                              double                       initialBearingDeg) const
{
    if (startId == goalId)
    {
        TaxiRoute r;
        r.polyline   = {nodes_[startId].pos};
        r.totalDistM = 0.0;
        r.valid      = true;
        return r;
    }

    constexpr int MAX_NODES             = 5000;
    const double  TURN_PENALTY          = apt_.taxiNetworkConfig.routing.turnPenalty;
    const double  WAYREF_CHANGE_PENALTY = apt_.taxiNetworkConfig.routing.wayrefChangePenalty;

    // Pre-resolve the active taxiFlowConfigs entry once per A* call.
    // Previously BuildConfigKey() and map::find() were called inside the per-edge loop,
    // costing O(|activeRwys|) string construction + O(log N) lookup for every edge evaluated.
    const bool hasActiveRwys  = !activeDepRwys.empty() || !activeArrRwys.empty();
    const auto cfgFlowIt      = hasActiveRwys
                                    ? apt_.taxiFlowConfigs.find(BuildConfigKey(activeDepRwys, activeArrRwys))
                                    : apt_.taxiFlowConfigs.end();
    const bool hasConfigRules = cfgFlowIt != apt_.taxiFlowConfigs.end();

    const GeoPoint& goalPos = nodes_[goalId].pos;

    struct PQEntry
    {
        double f;
        int    id;
    };
    auto cmp = [](const PQEntry& a, const PQEntry& b)
    { return a.f > b.f; };
    std::priority_queue<PQEntry, std::vector<PQEntry>, decltype(cmp)> open(cmp);

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

        for (const auto& edge : adj_[cur])
        {
            if (!excludedRefs.empty() && excludedRefs.contains(edge.wayRef))
                continue;
            if (!blockedNodes.empty() && blockedNodes.contains(edge.to) &&
                edge.to != goalId)
                continue;

            // Re-apply config flow rule multiplier (generic rules baked in at Build time;
            // taxiFlowConfigs rules for the active dep+arr configuration are added here).
            // cfgFlowIt and hasConfigRules are resolved once per A* call (above).
            double depMult = 1.0;
            if (!edge.wayRef.empty() && hasConfigRules)
            {
                for (const auto& rule : cfgFlowIt->second)
                {
                    if (rule.taxiway != edge.wayRef)
                        continue;
                    const double rb = CardinalToBearing(rule.direction);
                    if (rb < 0.0)
                        continue;
                    if (BearingDiff(edge.bearingDeg, rb) >= apt_.taxiNetworkConfig.flowRules.againstFlowMinDeg)
                        depMult = apt_.taxiNetworkConfig.flowRules.againstFlowMult;
                    break;
                }
            }

            // Hard-block sharp turns; penalise turns beyond the soft threshold.
            double turnPenalty = 0.0;
            if (incomingBearing[cur] >= 0.0)
            {
                const double diff = BearingDiff(incomingBearing[cur], edge.bearingDeg);
                if (diff > apt_.taxiNetworkConfig.routing.hardTurnDeg)
                    continue;
                if (diff > apt_.taxiNetworkConfig.routing.softTurnDeg)
                    turnPenalty = TURN_PENALTY;
            }

            if (!incomingWayRef[cur].empty() && !edge.wayRef.empty() &&
                incomingWayRef[cur] != edge.wayRef &&
                !isxWayRefs_.count(incomingWayRef[cur]) &&
                !isxWayRefs_.count(edge.wayRef))
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
        return {};

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

    if (path.size() >= 2)
    {
        const GeoPoint& a = nodes_[path[path.size() - 2]].pos;
        const GeoPoint& b = nodes_[path.back()].pos;
        route.exitBearing = BearingDeg(a, b);
    }

    return route;
}

TaxiRoute TaxiGraph::FindRoute(const GeoPoint&              from,
                               const GeoPoint&              to,
                               double                       wingspanM,
                               const std::set<std::string>& activeDepRwys,
                               const std::set<std::string>& activeArrRwys,
                               double                       initialBearingDeg,
                               const std::set<int>&         blockedNodes) const
{
    if (nodes_.empty())
        return {};

    // Collect candidate start nodes: up to 3 nearest forward (within 120 m) and up to 2 nearest
    // backward (within 300 m). A* runs from each candidate; the shortest valid route wins.
    // This handles curve intersections where the closest forward node is on the wrong branch.
    const double FORWARD_SNAP_M  = apt_.taxiNetworkConfig.routing.forwardSnapM;
    const double BACKWARD_SNAP_M = apt_.taxiNetworkConfig.routing.backwardSnapM;

    std::vector<int> candidates = NearestCandidateNodes(from, initialBearingDeg,
                                                        FORWARD_SNAP_M, BACKWARD_SNAP_M, 3, 2);
    if (candidates.empty())
        candidates.push_back(NearestNode(from));

    // Restrict candidates to the same taxiway ref as the nearest one.
    // This prevents A* from starting on a different taxiway (e.g. centre line
    // instead of the blue taxilane) whose nodes happen to be within the snap radius.
    if (candidates.size() > 1)
    {
        const std::string& primaryRef = nodes_[candidates[0]].wayRef;
        if (!primaryRef.empty())
        {
            candidates.erase(
                std::remove_if(candidates.begin() + 1, candidates.end(),
                               [&](int id)
                               {
                                   const std::string& r = nodes_[id].wayRef;
                                   return !r.empty() && r != primaryRef;
                               }),
                candidates.end());
        }
    }

    const int goalId = NearestNode(to);

    // Per-edge wingspan check: build a set of excluded wayRefs.
    std::set<std::string> excludedRefs;
    if (wingspanM > 0.0)
        for (const auto& [ref, maxWs] : apt_.taxiWingspanMax)
            if (wingspanM > maxWs)
                excludedRefs.insert(ref);

    // Try A* from each candidate; keep the shortest result when measured from the
    // aircraft's actual position. totalDistM is from the start node to the goal —
    // add the snap distance (aircraft → start node) so that backward candidates
    // naturally pay for the backward travel they impose. Forward candidates pay
    // only their small forward snap distance, so they are preferred unless a
    // backward start genuinely produces a shorter overall path.
    TaxiRoute best;
    double    bestScore = std::numeric_limits<double>::max();
    for (const int startId : candidates)
    {
        TaxiRoute r = RunAStar(startId, goalId, excludedRefs, blockedNodes, activeDepRwys,
                               activeArrRwys, initialBearingDeg);
        if (!r.valid)
            continue;
        const double snapDist = HaversineM(from, nodes_[startId].pos);
        const double score    = r.totalDistM + snapDist;
        if (score < bestScore)
        {
            bestScore = score;
            best      = std::move(r);
        }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::FindWaypointRoute
// ─────────────────────────────────────────────────────────────────────────────

TaxiRoute TaxiGraph::FindWaypointRoute(const GeoPoint&              origin,
                                       const std::vector<GeoPoint>& waypoints,
                                       const GeoPoint&              dest,
                                       double                       wingspanM,
                                       const std::set<std::string>& activeDepRwys,
                                       const std::set<std::string>& activeArrRwys,
                                       double                       initialBearingDeg,
                                       const std::set<int>&         blockedNodes) const
{
    std::vector<GeoPoint> stops;
    stops.push_back(origin);
    for (const auto& wp : waypoints)
        stops.push_back(wp);
    stops.push_back(dest);

    TaxiRoute combined;
    combined.valid        = true;
    double segInitBearing = initialBearingDeg; // propagated across segment boundaries

    for (size_t i = 1; i < stops.size(); ++i)
    {
        TaxiRoute seg = FindRoute(stops[i - 1], stops[i], wingspanM, activeDepRwys, activeArrRwys,
                                  segInitBearing, blockedNodes);
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
// Push zone walk
// ─────────────────────────────────────────────────────────────────────────────

TaxiRoute TaxiGraph::WalkGraph(const GeoPoint& from, double bearingDeg,
                               double maxDistM, double wingspanM) const
{
    TaxiRoute result;
    if (nodes_.empty())
        return result;

    // Build wingspan exclusion set.
    std::set<std::string> excludedRefs;
    if (wingspanM > 0.0)
        for (const auto& [ref, maxWs] : apt_.taxiWingspanMax)
            if (wingspanM > maxWs)
                excludedRefs.insert(ref);

    const int startId = NearestNode(from);
    if (startId < 0)
        return result;

    result.polyline.push_back(nodes_[startId].pos);
    result.valid = true;

    int    cur     = startId;
    int    prev    = -1;
    double accum   = 0.0;
    double curBrng = bearingDeg;

    for (;;)
    {
        int    bestTo   = -1;
        double bestDiff = 90.0; // only accept edges within ±90° of current bearing
        double bestBrng = curBrng;

        for (const auto& e : adj_[cur])
        {
            if (e.to == prev)
                continue; // no backtracking
            if (excludedRefs.contains(e.wayRef))
                continue; // wingspan restriction
            const double diff = BearingDiff(e.bearingDeg, curBrng);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                bestTo   = e.to;
                bestBrng = e.bearingDeg;
            }
        }

        if (bestTo < 0)
            break; // dead end or no edge within ±90°

        const double edgeDist = HaversineM(nodes_[cur].pos, nodes_[bestTo].pos);
        if (accum + edgeDist >= maxDistM)
        {
            // Interpolate the stopping point along this edge.
            const double   frac = (maxDistM - accum) / edgeDist;
            const GeoPoint stop = {
                nodes_[cur].pos.lat + frac * (nodes_[bestTo].pos.lat - nodes_[cur].pos.lat),
                nodes_[cur].pos.lon + frac * (nodes_[bestTo].pos.lon - nodes_[cur].pos.lon)};
            result.polyline.push_back(stop);
            accum = maxDistM;
            break;
        }

        accum += edgeDist;
        result.polyline.push_back(nodes_[bestTo].pos);
        prev    = cur;
        cur     = bestTo;
        curBrng = bestBrng; // follow curves naturally
    }

    result.totalDistM = accum;
    return result;
}

std::vector<TaxiGraph::PushPivotCandidate> TaxiGraph::PushCandidates(
    const GeoPoint& origin, double bearingDeg, double wingspanM, double maxDistM) const
{
    // Build wingspan exclusion set.
    std::set<std::string> excludedRefs;
    if (wingspanM > 0.0)
        for (const auto& [ref, maxWs] : apt_.taxiWingspanMax)
            if (wingspanM > maxWs)
                excludedRefs.insert(ref);

    // Flat-earth push-axis unit vector centred on origin.
    const double cosLat   = std::cos(origin.lat * std::numbers::pi / 180.0);
    const double scaleLat = TAXI_EARTH_R * std::numbers::pi / 180.0;
    const double scaleLon = TAXI_EARTH_R * cosLat * std::numbers::pi / 180.0;
    const double brngRad  = bearingDeg * std::numbers::pi / 180.0;
    const double ux       = std::sin(brngRad); // lon component of push unit vector
    const double uy       = std::cos(brngRad); // lat component of push unit vector

    // One representative node per wayRef: keep the one closest to the push axis (lateral).
    // Grid bounding-radius query: the search region is maxDistM ahead × ±60 m lateral,
    // so a circle of radius sqrt(maxDistM² + 60²) covers the whole rectangle.
    struct Entry
    {
        int    nodeId;
        double t;       ///< Along-axis distance from origin
        double lateral; ///< Perpendicular distance from push axis
    };
    std::map<std::string, Entry> best;

    const double searchR = std::sqrt(maxDistM * maxDistM + 60.0 * 60.0);
    const int    rings   = static_cast<int>(std::ceil(searchR / GRID_CELL_M)) + 1;
    auto [cx0, cy0]      = GridCell(origin);
    for (int dx = -rings; dx <= rings; ++dx)
    {
        for (int dy = -rings; dy <= rings; ++dy)
        {
            auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
            if (it == grid_.end())
                continue;
            for (const int nid : it->second)
            {
                const TaxiNode& n = nodes_[nid];
                if (n.type != TaxiNodeType::Waypoint || n.wayRef.empty())
                    continue;
                if (excludedRefs.contains(n.wayRef))
                    continue;

                const double ndx = (n.pos.lon - origin.lon) * scaleLon;
                const double ndy = (n.pos.lat - origin.lat) * scaleLat;
                const double t   = ndx * ux + ndy * uy;
                if (t < 5.0 || t > maxDistM)
                    continue;
                const double lateral = std::abs(ndx * uy - ndy * ux);
                if (lateral > 60.0)
                    continue;

                auto bIt = best.find(n.wayRef);
                if (bIt == best.end() || lateral < bIt->second.lateral)
                    best[n.wayRef] = {n.id, t, lateral};
            }
        }
    }

    std::vector<PushPivotCandidate> result;
    result.reserve(best.size());
    for (const auto& [ref, e] : best)
        result.push_back({nodes_[e.nodeId].pos, e.t, ref});

    std::sort(result.begin(), result.end(),
              [](const PushPivotCandidate& a, const PushPivotCandidate& b)
              { return a.distM < b.distM; });
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Swingover
// ─────────────────────────────────────────────────────────────────────────────

double TaxiGraph::NodeLaneBearing(int nodeId, const std::string& wayRef) const
{
    if (nodeId < 0 || nodeId >= static_cast<int>(adj_.size()))
        return 0.0;
    double sumSin = 0.0, sumCos = 0.0;
    int    count = 0;
    for (const auto& e : adj_[nodeId])
    {
        if (e.wayRef == wayRef)
        {
            const double rad = e.bearingDeg * std::numbers::pi / 180.0;
            sumSin += std::sin(rad);
            sumCos += std::cos(rad);
            ++count;
        }
    }
    if (count == 0)
        return 0.0;
    double mean = std::atan2(sumSin / count, sumCos / count) * 180.0 / std::numbers::pi;
    return std::fmod(mean + 360.0, 360.0);
}

double TaxiGraph::ForwardEdgeBearing(int nodeId, const std::string& wayRef, double headingDeg) const
{
    if (nodeId < 0 || nodeId >= static_cast<int>(adj_.size()))
        return headingDeg;
    double bestBrng = headingDeg;
    double bestDiff = 361.0;
    for (const auto& e : adj_[nodeId])
    {
        if (e.wayRef != wayRef)
            continue;
        const double diff = BearingDiff(e.bearingDeg, headingDeg);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            bestBrng = e.bearingDeg;
        }
    }
    return bestBrng;
}

/// @brief Samples a cubic Bezier S-bend between ptA and ptB as a polyline.
/// Control points depart ptA along brngA and arrive at ptB from behind along brngB.
/// Returns @p samples intermediate points (ptA and ptB excluded — they are already in the caller's polyline).
static std::vector<GeoPoint> MakeSBend(const GeoPoint& ptA, double brngA,
                                       const GeoPoint& ptB, double brngB,
                                       double offsetM = 8.0,
                                       int    samples = 10)
{
    const double cosLat   = std::cos(ptA.lat * std::numbers::pi / 180.0);
    const double scaleLat = TAXI_EARTH_R * std::numbers::pi / 180.0;
    const double scaleLon = TAXI_EARTH_R * cosLat * std::numbers::pi / 180.0;

    // Scale control-point distance to 40 % of the span so arcs are pronounced regardless of distance.
    const double span = HaversineM(ptA, ptB);
    const double d    = std::max(offsetM, span * 0.4);

    auto offsetPt = [&](const GeoPoint& pt, double bearingDeg, double distM) -> GeoPoint
    {
        const double rad = bearingDeg * std::numbers::pi / 180.0;
        return {pt.lat + distM * std::cos(rad) / scaleLat,
                pt.lon + distM * std::sin(rad) / scaleLon};
    };

    // Cubic Bezier control points.
    const GeoPoint P1 = offsetPt(ptA, brngA, d);  // depart ptA forward along lane A
    const GeoPoint P2 = offsetPt(ptB, brngB, -d); // arrive at ptB from behind along lane B

    // Sample the curve at t = 1/N … (N-1)/N (skip endpoints).
    std::vector<GeoPoint> pts;
    pts.reserve(samples - 1);
    for (int i = 1; i < samples; ++i)
    {
        const double t   = static_cast<double>(i) / samples;
        const double mt  = 1.0 - t;
        const double mt2 = mt * mt;
        const double mt3 = mt2 * mt;
        const double t2  = t * t;
        const double t3  = t2 * t;
        pts.push_back({mt3 * ptA.lat + 3.0 * mt2 * t * P1.lat + 3.0 * mt * t2 * P2.lat + t3 * ptB.lat,
                       mt3 * ptA.lon + 3.0 * mt2 * t * P1.lon + 3.0 * mt * t2 * P2.lon + t3 * ptB.lon});
    }
    return pts;
}

TaxiGraph::SwingoverResult TaxiGraph::SwingoverSnap(
    const GeoPoint&                                rawPos,
    const std::string&                             currentRef,
    const std::vector<std::array<std::string, 2>>& pairs,
    double                                         wingspanM,
    double                                         headingDeg,
    double                                         maxM) const
{
    // Find partner ref from pairs.
    std::string partnerRef;
    for (const auto& pair : pairs)
    {
        if (pair[0] == currentRef)
        {
            partnerRef = pair[1];
            break;
        }
        if (pair[1] == currentRef)
        {
            partnerRef = pair[0];
            break;
        }
    }
    if (partnerRef.empty())
        return {};

    // Wingspan check on partner.
    if (wingspanM > 0.0)
    {
        auto it = apt_.taxiWingspanMax.find(partnerRef);
        if (it != apt_.taxiWingspanMax.end() && wingspanM > it->second)
            return {};
    }

    constexpr double START_MIN_M = 5.0;  ///< Minimum forward distance on current lane to startBend.
    constexpr double END_MIN_M   = 20.0; ///< Minimum forward distance on partner lane to endBend.
    constexpr double SBEND_TAN_M = 8.0;  ///< Tangent offset for MakeSBend control points.

    // Use aircraft heading as the forward direction for both forward filters.
    // NodeLaneBearing averages forward+backward edges and gives a nonsense result at interior nodes.
    const double cosLat   = std::cos(rawPos.lat * std::numbers::pi / 180.0);
    const double scaleLat = TAXI_EARTH_R * std::numbers::pi / 180.0;
    const double scaleLon = TAXI_EARTH_R * cosLat * std::numbers::pi / 180.0;
    const double hdgRad   = headingDeg * std::numbers::pi / 180.0;
    const double ux       = std::sin(hdgRad); // forward unit vector (lon component)
    const double uy       = std::cos(hdgRad); // forward unit vector (lat component)

    // Look up pre-indexed wayRef node lists (built by Build()).
    const auto curIt = wayRefNodes_.find(currentRef);
    const auto parIt = wayRefNodes_.find(partnerRef);
    if (curIt == wayRefNodes_.end() || parIt == wayRefNodes_.end())
        return {};
    const auto& curNodes = curIt->second;
    const auto& parNodes = parIt->second;

    // Step 1: startBend = nearest current-lane node >= START_MIN_M ahead of rawPos.
    double bestStartD = maxM;
    int    startNode  = -1;
    for (const int nid : curNodes)
    {
        const TaxiNode& n  = nodes_[nid];
        const double    dx = (n.pos.lon - rawPos.lon) * scaleLon;
        const double    dy = (n.pos.lat - rawPos.lat) * scaleLat;
        if (dx * ux + dy * uy < START_MIN_M)
            continue;
        const double d = HaversineM(n.pos, rawPos);
        if (d < bestStartD)
        {
            bestStartD = d;
            startNode  = nid;
        }
    }
    if (startNode < 0)
        return {};

    const GeoPoint& startBend   = nodes_[startNode].pos;
    const double    brngAtStart = ForwardEdgeBearing(startNode, currentRef, headingDeg);

    // Step 2: crossPt = nearest partner-lane node to startBend (lateral jump).
    double bestCrossD = maxM;
    int    crossNode  = -1;
    for (const int nid : parNodes)
    {
        const double d = HaversineM(nodes_[nid].pos, startBend);
        if (d < bestCrossD)
        {
            bestCrossD = d;
            crossNode  = nid;
        }
    }
    if (crossNode < 0)
        return {};

    const GeoPoint& crossPt = nodes_[crossNode].pos;

    // Step 3: endBend = nearest partner-lane node >= END_MIN_M ahead of crossPt.
    double bestEndD = maxM;
    int    endNode  = -1;
    for (const int nid : parNodes)
    {
        const TaxiNode& n  = nodes_[nid];
        const double    dx = (n.pos.lon - crossPt.lon) * scaleLon;
        const double    dy = (n.pos.lat - crossPt.lat) * scaleLat;
        if (dx * ux + dy * uy < END_MIN_M)
            continue;
        const double d = HaversineM(n.pos, crossPt);
        if (d < bestEndD)
        {
            bestEndD = d;
            endNode  = nid;
        }
    }
    if (endNode < 0)
        return {};

    SwingoverResult res;
    res.crossPt       = startBend;
    res.partnerPt     = nodes_[endNode].pos;
    res.brngAtCross   = brngAtStart;
    res.brngAtPartner = ForwardEdgeBearing(endNode, partnerRef, headingDeg);
    res.sbendPts      = MakeSBend(res.crossPt, res.brngAtCross, res.partnerPt, res.brngAtPartner, SBEND_TAN_M);
    res.partnerRef    = partnerRef;
    res.valid         = true;
    return res;
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

std::string TaxiGraph::WayRefAt(const GeoPoint& rawPos, double maxM) const
{
    double bestD  = maxM;
    int    bestId = -1;
    for (const auto& n : nodes_)
    {
        if (n.type != TaxiNodeType::Waypoint || n.wayRef.empty())
            continue;
        const double d = HaversineM(n.pos, rawPos);
        if (d < bestD)
        {
            bestD  = d;
            bestId = n.id;
        }
    }
    if (bestId < 0)
        return {};
    return nodes_[bestId].wayRef;
}

GeoPoint TaxiGraph::SnapForPlanning(const GeoPoint&  rawPos,
                                    const TaxiRoute& suggested) const
{
    const double HP_SNAP_M    = apt_.taxiNetworkConfig.snapping.holdingPointM;
    const double ISX_SNAP_M   = apt_.taxiNetworkConfig.snapping.intersectionM;
    const double ROUTE_SNAP_M = apt_.taxiNetworkConfig.snapping.suggestedRouteM;
    const double WP_SNAP_M    = apt_.taxiNetworkConfig.snapping.waypointM;

    // Priority 1: holding points / holding positions — O(k) via pre-built index.
    {
        double bestD  = HP_SNAP_M;
        int    bestId = -1;
        for (const int id : hpNodeIds_)
        {
            const double d = HaversineM(nodes_[id].pos, rawPos);
            if (d < bestD)
            {
                bestD  = d;
                bestId = id;
            }
        }
        if (bestId >= 0)
            return nodes_[bestId].pos;
    }

    // Priority 2: intersection waypoints (label contains "Exit") — O(k) via pre-built index.
    {
        double bestD  = ISX_SNAP_M;
        int    bestId = -1;
        for (const int id : isxNodeIds_)
        {
            const double d = HaversineM(nodes_[id].pos, rawPos);
            if (d < bestD)
            {
                bestD  = d;
                bestId = id;
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

    // Priority 4: nearest waypoint — O(cells in radius) via spatial grid.
    {
        double    bestD  = WP_SNAP_M;
        int       bestId = -1;
        const int rings  = static_cast<int>(std::ceil(WP_SNAP_M / GRID_CELL_M)) + 1;
        auto [cx0, cy0]  = GridCell(rawPos);
        for (int dx = -rings; dx <= rings; ++dx)
        {
            for (int dy = -rings; dy <= rings; ++dy)
            {
                auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
                if (it == grid_.end())
                    continue;
                for (const int id : it->second)
                {
                    if (nodes_[id].type != TaxiNodeType::Waypoint)
                        continue;
                    const double d = HaversineM(nodes_[id].pos, rawPos);
                    if (d < bestD)
                    {
                        bestD  = d;
                        bestId = id;
                    }
                }
            }
        }
        if (bestId >= 0)
            return nodes_[bestId].pos;
    }

    return rawPos;
}

// ─────────────────────────────────────────────────────────────────────────────
// Flow rule helpers
// ─────────────────────────────────────────────────────────────────────────────

std::vector<TaxiFlowRule> TaxiGraph::GetActiveFlowRules(
    const std::set<std::string>& activeDepRwys,
    const std::set<std::string>& activeArrRwys) const
{
    std::vector<TaxiFlowRule> rules = apt_.taxiFlowGeneric;
    const std::string         key   = BuildConfigKey(activeDepRwys, activeArrRwys);
    const auto                it    = apt_.taxiFlowConfigs.find(key);
    if (it != apt_.taxiFlowConfigs.end())
        rules.insert(rules.end(), it->second.begin(), it->second.end());
    return rules;
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
// TaxiGraph::NodesToBlock
// ─────────────────────────────────────────────────────────────────────────────

std::set<int> TaxiGraph::NodesToBlock(const std::vector<GeoPoint>& polyline,
                                      double                       radiusM) const
{
    std::set<int> result;
    const int     rings = static_cast<int>(std::ceil(radiusM / GRID_CELL_M)) + 1;
    for (const auto& pt : polyline)
    {
        auto [cx0, cy0] = GridCell(pt);
        for (int dx = -rings; dx <= rings; ++dx)
        {
            for (int dy = -rings; dy <= rings; ++dy)
            {
                auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
                if (it == grid_.end())
                    continue;
                for (const int nid : it->second)
                {
                    if (HaversineM(nodes_[nid].pos, pt) <= radiusM)
                        result.insert(nid);
                }
            }
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::DeadEndEdges
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::pair<GeoPoint, GeoPoint>> TaxiGraph::DeadEndEdges(
    const GeoPoint& dest, const std::set<int>& blockedNodes) const
{
    if (nodes_.empty() || blockedNodes.empty())
        return {};

    const int destId = NearestNode(dest);
    if (destId < 0 || blockedNodes.contains(destId))
        return {};

    // BFS from destId, never crossing blockedNodes.
    std::set<int>   reachable;
    std::queue<int> q;
    reachable.insert(destId);
    q.push(destId);
    while (!q.empty())
    {
        const int cur = q.front();
        q.pop();
        for (const auto& edge : adj_[cur])
        {
            if (blockedNodes.contains(edge.to) || reachable.contains(edge.to))
                continue;
            reachable.insert(edge.to);
            q.push(edge.to);
        }
    }

    // Check whether any blocked node acts as a "bridge" between the reachable set and
    // the rest of the graph. A blocked node bridges if it has at least one edge into
    // `reachable` AND at least one edge to an unblocked node outside `reachable`.
    // If no blocked node bridges, dest is still accessible from the full network.
    bool isCutOff = false;
    for (const int b : blockedNodes)
    {
        if (b < 0 || b >= static_cast<int>(adj_.size()))
            continue;
        bool intoReachable    = false;
        bool outsideReachable = false;
        for (const auto& edge : adj_[b])
        {
            if (reachable.contains(edge.to))
                intoReachable = true;
            else if (!blockedNodes.contains(edge.to))
                outsideReachable = true;
            if (intoReachable && outsideReachable)
                break;
        }
        if (intoReachable && outsideReachable)
        {
            isCutOff = true;
            break;
        }
    }
    if (!isCutOff)
        return {}; // dest is still reachable normally

    // Truly isolated — collect undirected edge segments within the reachable set.
    std::vector<std::pair<GeoPoint, GeoPoint>> edges;
    std::set<std::pair<int, int>>              seen;
    for (const int nodeId : reachable)
    {
        for (const auto& edge : adj_[nodeId])
        {
            if (!reachable.contains(edge.to))
                continue;
            const int lo = std::min(nodeId, edge.to);
            const int hi = std::max(nodeId, edge.to);
            if (seen.insert({lo, hi}).second)
                edges.emplace_back(nodes_[nodeId].pos, nodes_[edge.to].pos);
        }
    }
    return edges;
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
