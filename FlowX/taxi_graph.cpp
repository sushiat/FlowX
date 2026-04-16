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
#include <format>
#include <map>
#include <numbers>
#include <queue>
#include <set>
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
                                std::string_view wayRef,
                                uint8_t          wayPriority,
                                int              waySubId)
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
                // Don't merge two different HP ways at crossing points — each HP has
                // independent connectivity (runway ↔ taxiway).  Crossing HPs (e.g.
                // B5 and B6) must not share graph nodes.
                if (wayPriority == 1 && nodes_[nid].wayPriority == 1 &&
                    !wayRef.empty() && !nodes_[nid].wayRef.empty() &&
                    nodes_[nid].wayRef != wayRef)
                    continue;
                // At a genuine OSM junction (d ≤ 1 m), resolve ref conflicts by
                // preferring the higher-priority way type (taxiway > taxilane >
                // intersection), and within the same priority prefer a parent name
                // over a lane-variant (e.g. "TL 40" over "TL 40 'Blue Line'"),
                // or the alphabetically smaller ref (e.g. "TL36" over "TL37").
                if (!wayRef.empty() && !nodes_[nid].wayRef.empty() &&
                    nodes_[nid].wayRef != wayRef && d <= 1.0)
                {
                    const bool higherPriority = wayPriority > nodes_[nid].wayPriority;
                    const bool samePriorityParent =
                        wayPriority == nodes_[nid].wayPriority &&
                        nodes_[nid].wayRef.starts_with(wayRef) &&
                        nodes_[nid].wayRef.size() > wayRef.size() &&
                        nodes_[nid].wayRef[wayRef.size()] == ' ';
                    const bool samePriorityAlpha =
                        wayPriority == nodes_[nid].wayPriority && wayRef < nodes_[nid].wayRef;
                    if (higherPriority || samePriorityParent || samePriorityAlpha)
                    {
                        nodes_[nid].wayRef      = std::string(wayRef);
                        nodes_[nid].wayPriority = wayPriority;
                        nodes_[nid].waySubId    = waySubId;
                    }
                }
                // Promote empty-ref nodes (stand/HP connectors) when a named
                // taxiway node merges into them across the full mergeThreshM
                // range.  Without this the connector swallows the named node
                // and the taxiway loses goal-pool coverage next to the stand.
                if (!wayRef.empty() && nodes_[nid].wayRef.empty())
                {
                    nodes_[nid].wayRef      = std::string(wayRef);
                    nodes_[nid].wayPriority = wayPriority;
                    nodes_[nid].waySubId    = waySubId;
                }
                return nid;
            }
        }
    }
    const int id = static_cast<int>(nodes_.size());
    nodes_.push_back({id, pos, type, std::string(label), std::string(wayRef), wayPriority, waySubId});
    adj_.emplace_back();
    GridInsert(id);
    return id;
}

void TaxiGraph::AddEdge(int from, int to, double cost, float flowMult,
                        const std::string& wayRef, double bearingDeg)
{
    adj_[from].push_back({to, cost, flowMult, wayRef, bearingDeg});
}

double TaxiGraph::FlowMult(double bearingDeg, const std::string& wayRef) const
{
    const double WITH_FLOW_MAX  = apt_.taxiNetworkConfig.flowRules.withFlowMaxDeg;
    const double WITH_FLOW_MULT = apt_.taxiNetworkConfig.flowRules.withFlowMult;
    const double AGAINST_FLOW   = apt_.taxiNetworkConfig.flowRules.againstFlowMinDeg;
    const double AGAINST_MULT   = apt_.taxiNetworkConfig.flowRules.againstFlowMult;

    for (const auto& rule : apt_.taxiFlowGeneric)
    {
        if (rule.taxiway != wayRef)
            continue;
        const double ruleBearing = CardinalToBearing(rule.direction);
        if (ruleBearing < 0.0)
            continue;
        const double diff = BearingDiff(bearingDeg, ruleBearing);
        if (diff <= WITH_FLOW_MAX)
            return WITH_FLOW_MULT;
        if (diff >= AGAINST_FLOW)
            return (rule.againstFlowMult > 0.0) ? rule.againstFlowMult : AGAINST_MULT;
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

std::string TaxiGraph::MakeRunwayConfigKey(const std::set<std::string>& activeDepRwys,
                                           const std::set<std::string>& activeArrRwys)
{
    return BuildConfigKey(activeDepRwys, activeArrRwys);
}

std::vector<std::string> TaxiGraph::ResolvePreferredSequence(
    const airport&               ap,
    const std::set<std::string>& activeDepRwys,
    const std::set<std::string>& activeArrRwys,
    const std::string&           destinationName,
    const std::string&           originWayRef)
{
    if (ap.preferredRoutes.empty() || destinationName.empty())
        return {};
    const std::string key = BuildConfigKey(activeDepRwys, activeArrRwys);
    auto              it  = ap.preferredRoutes.find(key);
    if (it == ap.preferredRoutes.end())
        return {};
    for (const auto& rule : it->second)
    {
        if (!std::regex_match(destinationName, rule.destinationRegex))
            continue;
        // Allow-list: origin must be known AND match.
        if (!rule.originPattern.empty() &&
            (originWayRef.empty() || !std::regex_match(originWayRef, rule.originRegex)))
            continue;
        // Deny-list: origin must be known AND match to exclude.  An unknown origin
        // bypasses the deny-list (default-safe behavior for rules scoped by exclusion).
        if (!rule.originExcludePattern.empty() && !originWayRef.empty() &&
            std::regex_match(originWayRef, rule.originExcludeRegex))
            continue;
        return rule.mustInclude;
    }
    return {};
}

bool TaxiGraph::WayRefSequenceSubsequence(const std::vector<std::string>& routeWayRefs,
                                          const std::vector<std::string>& sequence)
{
    if (sequence.empty())
        return true;
    size_t j = 0;
    for (const auto& ref : routeWayRefs)
    {
        if (ref == sequence[j])
        {
            if (++j == sequence.size())
                return true;
        }
    }
    return false;
}

std::vector<GeoPoint> TaxiGraph::RepresentativeWaypointsForWayRefs(
    const GeoPoint&                 from,
    const GeoPoint&                 to,
    const std::vector<std::string>& sequence) const
{
    const auto            ids = RepresentativeNodeIdsForWayRefs(from, to, sequence);
    std::vector<GeoPoint> out;
    out.reserve(ids.size());
    for (const int id : ids)
        out.push_back(nodes_[id].pos);
    return out;
}

std::vector<int> TaxiGraph::RepresentativeNodeIdsForWayRefs(
    const GeoPoint&                 from,
    const GeoPoint&                 to,
    const std::vector<std::string>& sequence,
    std::string*                    diagnostic) const
{
    std::vector<int> out;
    if (sequence.empty())
        return out;
    out.reserve(sequence.size());

    // Per-node set of wayRefs touched by any incident edge (incoming or
    // outgoing).  Used to identify junction nodes shared by two specific refs.
    const int                                    nodeCount = static_cast<int>(adj_.size());
    std::vector<std::unordered_set<std::string>> nodeRefs(nodeCount);
    for (int nid = 0; nid < nodeCount; ++nid)
    {
        for (const auto& e : adj_[nid])
        {
            if (e.wayRef.empty())
                continue;
            nodeRefs[nid].insert(e.wayRef);
            nodeRefs[e.to].insert(e.wayRef);
        }
    }

    // Chain semantics (backward walk — anchor from @p to):
    //   rep[N-1]               — nearest JUNCTION node touching edges on BOTH
    //                            seq[N-2] AND seq[N-1], anchored on @p to.
    //                            (If N==1, just nearest node on seq[0].)
    //   rep[k], 0 < k < N-1    — nearest JUNCTION node touching edges on BOTH
    //                            seq[k-1] AND seq[k], anchored on rep[k+1].
    //   rep[0]                 — nearest node touching seq[0], anchored on
    //                            rep[1] (if it exists).
    //
    // Walking backward is important when the destination side is more
    // geometrically constrained than the origin side: anchoring from @p to
    // first picks the junction actually adjacent to the target, then chains
    // predecessors back toward @p from along ways reachable to that junction.
    // Forward-walking can otherwise pick a same-name-but-disconnected junction
    // (e.g. two separate "TL 42" stretches that share a label but not a path).
    out.resize(sequence.size(), -1);
    GeoPoint anchor = to;
    for (int si = static_cast<int>(sequence.size()) - 1; si >= 0; --si)
    {
        const auto& ref = sequence[si];

        std::unordered_set<int> candidates;
        const bool              requireJunction = (si > 0);
        for (int nid = 0; nid < nodeCount; ++nid)
        {
            if (!nodeRefs[nid].count(ref))
                continue;
            if (requireJunction && !nodeRefs[nid].count(sequence[si - 1]))
                continue;
            candidates.insert(nid);
        }
        // Relax: if no node sits at the seq[si-1]/seq[si] junction, fall back to
        // any node touching seq[si].  Leg B_k may still fail, but the caller
        // then gets an actionable leg-level diagnostic instead of a blanket
        // "waypoints unavailable".
        if (candidates.empty() && requireJunction)
        {
            if (diagnostic)
            {
                if (!diagnostic->empty())
                    *diagnostic += "; ";
                *diagnostic += "no junction node for [" + sequence[si - 1] +
                               "]/[" + ref + "], relaxed";
            }
            for (int nid = 0; nid < nodeCount; ++nid)
                if (nodeRefs[nid].count(ref))
                    candidates.insert(nid);
        }
        if (candidates.empty())
        {
            if (diagnostic)
            {
                if (!diagnostic->empty())
                    *diagnostic += "; ";
                *diagnostic += "no node touches [" + ref + "]";
            }
            return {};
        }

        const bool isFirst = (si == 0);
        int        bestId  = -1;
        double     bestD   = std::numeric_limits<double>::max();
        for (const int id : candidates)
        {
            double d = HaversineM(nodes_[id].pos, anchor);
            if (isFirst)
                d += HaversineM(nodes_[id].pos, from);
            if (d < bestD)
            {
                bestD  = d;
                bestId = id;
            }
        }
        if (bestId < 0)
            return {};
        out[si] = bestId;
        anchor  = nodes_[bestId].pos;
    }
    return out;
}

TaxiRoute TaxiGraph::RouteBetweenNodes(int                          startId,
                                       int                          goalId,
                                       double                       wingspanM,
                                       const std::set<std::string>& activeDepRwys,
                                       const std::set<std::string>& activeArrRwys,
                                       const std::set<std::string>& allowedWayRefs,
                                       double                       initialBearingDeg,
                                       const std::set<int>&         blockedNodes,
                                       const std::string&           terminateOnWayRef) const
{
    if (startId < 0 || goalId < 0 ||
        startId >= static_cast<int>(nodes_.size()) ||
        goalId >= static_cast<int>(nodes_.size()))
        return {};

    std::set<std::string> excludedRefs;
    if (wingspanM > 0.0)
        for (const auto& [ref, maxWs] : apt_.taxiWingspanMax)
            if (wingspanM > maxWs)
                excludedRefs.insert(ref);

    std::set<std::string> avoidRefs;
    if (wingspanM > 0.0)
        for (const auto& [ref, maxWs] : apt_.taxiWingspanAvoid)
            if (wingspanM <= maxWs)
                avoidRefs.insert(ref);

    return RunAStar(startId, goalId, excludedRefs, avoidRefs, blockedNodes,
                    activeDepRwys, activeArrRwys, initialBearingDeg,
                    {}, false, {}, false, allowedWayRefs, terminateOnWayRef);
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
    hpWayRefs_.clear();
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
    // Track a per-ref sub-ID so multiple OSM ways sharing the same ref
    // (e.g. three intersection branches all named "Exit 3") get distinct
    // sub-IDs on their nodes.  This lets candidate selection keep one node
    // per branch instead of collapsing all branches into a single bucket.
    std::map<std::string, int> waySubIdCounter;
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

        const std::string& ref   = way.ref.empty() ? way.name : way.ref;
        const int          subId = ref.empty() ? 0 : waySubIdCounter[ref]++;

        if (way.type == AerowayType::Taxiway_Intersection && !ref.empty())
            isxWayRefs_.insert(ref);
        if (way.type == AerowayType::Taxiway_HoldingPoint && !ref.empty())
            hpWayRefs_.insert(ref);
        if (way.type == AerowayType::Runway && !ref.empty())
            rwyWayRefs_.insert(ref);

        // Ref priority for junction conflict resolution: higher value wins when
        // two ways share a node.  Taxiway > taxilane > intersection > runway > holding point.
        const uint8_t refPriority =
            (way.type == AerowayType::Taxiway)                ? 5
            : (way.type == AerowayType::Taxilane)             ? 4
            : (way.type == AerowayType::Taxiway_Intersection) ? 3
            : (way.type == AerowayType::Runway)               ? 2
            : (way.type == AerowayType::Taxiway_HoldingPoint) ? 1
                                                              : 0;

        for (size_t k = 1; k < way.geometry.size(); ++k)
        {
            const GeoPoint& a    = way.geometry[k - 1];
            const GeoPoint& b    = way.geometry[k];
            const double    dist = HaversineM(a, b);
            if (dist < 0.1)
                continue; // skip degenerate segments

            const double bAB    = BearingDeg(a, b);
            const double bBA    = BearingDeg(b, a);
            const float  flowAB = static_cast<float>(FlowMult(bAB, ref));
            const float  flowBA = static_cast<float>(FlowMult(bBA, ref));
            const double fmAB   = flowAB * typeMult;
            const double fmBA   = flowBA * typeMult;

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

            int prev = FindOrCreateNode(a, 1.0, TaxiNodeType::Waypoint, ref, ref, refPriority, subId);

            for (int s = 1; s < nSteps; ++s)
            {
                const double   t   = static_cast<double>(s) * SUBDIV_M / dist;
                const GeoPoint mid = {a.lat + (b.lat - a.lat) * t,
                                      a.lon + (b.lon - a.lon) * t};
                const int      cur = FindOrCreateNode(mid, 0.001, TaxiNodeType::Waypoint, ref, ref, refPriority, subId);
                if (cur == prev)
                    continue;
                const double d = HaversineM(nodes_[prev].pos, nodes_[cur].pos);
                AddEdge(prev, cur, d * fmAB, flowAB, ref, bAB);
                AddEdge(cur, prev, d * fmBA, flowBA, ref, bBA);
                prev = cur;
            }

            const int nB = FindOrCreateNode(b, 1.0, TaxiNodeType::Waypoint, ref, ref, refPriority, subId);
            if (nB != prev)
            {
                const double d = HaversineM(nodes_[prev].pos, nodes_[nB].pos);
                AddEdge(prev, nB, d * fmAB, flowAB, ref, bAB);
                AddEdge(nB, prev, d * fmBA, flowBA, ref, bBA);
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

    // ── Step 8: runway-approach penalty ──────────────────────────────────────
    // Penalise edges that ENTER a Taxiway_HoldingPoint wayRef (A1, B8, etc.)
    // from a different wayRef.  This discourages the router from cutting through
    // runway entry/exit paths.  Edges already on the HP wayRef (aircraft exiting
    // the runway) are unaffected.  OSM stop bars (W1, W2) are not HP wayRefs so
    // they are naturally exempt.
    const double approachMult = apt_.taxiNetworkConfig.edgeCosts.multRunwayApproach;
    if (approachMult > 1.0)
    {
        for (size_t fromId = 0; fromId < adj_.size(); ++fromId)
        {
            const std::string& fromRef = nodes_[fromId].wayRef;
            for (auto& edge : adj_[fromId])
            {
                if (!hpWayRefs_.count(edge.wayRef))
                    continue; // edge is not on a HP way
                if (fromRef == edge.wayRef)
                    continue; // staying on the same HP way — no penalty
                edge.cost *= approachMult;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// A* helpers
// ─────────────────────────────────────────────────────────────────────────────

int TaxiGraph::NearestNode(const GeoPoint& pos, double maxM) const
{
    if (nodes_.empty())
        return -1;

    // Expanding-ring search: start at the cell containing pos, expand one ring at a time.
    // Stop once the inner edge of the next ring is further than the current best distance,
    // or once the ring's minimum distance exceeds maxM (hard cap to avoid O(n²) expansion
    // when the query point is far from the graph).
    auto [cx0, cy0] = GridCell(pos);
    double bestD    = std::numeric_limits<double>::max();
    int    bestId   = -1;

    for (int ring = 0;; ++ring)
    {
        // Inner edge of this ring is at least (ring - 1) * GRID_CELL_M away.
        const double ringMinDist = (ring - 1) * GRID_CELL_M;
        if (ring > 0 && (ringMinDist >= bestD || ringMinDist > maxM))
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

std::string TaxiGraph::NearestWayRef(const GeoPoint& pos) const
{
    const int id = NearestNode(pos);
    if (id < 0)
        return {};
    return nodes_[id].wayRef;
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
    int maxBwd, double maxAngleDeg) const
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
                    if (diff <= maxAngleDeg && d <= maxFwdM)
                        fwd.push_back({d, id});
                    else if (diff > maxAngleDeg && d <= maxBwdM)
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

TaxiRoute TaxiGraph::RunAStar(int                             startId,
                              int                             goalId,
                              const std::set<std::string>&    excludedRefs,
                              const std::set<std::string>&    avoidRefs,
                              const std::set<int>&            blockedNodes,
                              const std::set<std::string>&    activeDepRwys,
                              const std::set<std::string>&    activeArrRwys,
                              double                          initialBearingDeg,
                              const std::set<std::string>&    suppressFlowWayRefs,
                              bool                            ignoreAllPenalties,
                              const std::set<int>&            preferredNodes,
                              bool                            emitDebugTrace,
                              const std::set<std::string>&    allowedWayRefs,
                              const std::string&              terminateOnWayRef,
                              const std::vector<std::string>& sequenceConstraint) const
{
    // State-augmented mode: when sequenceConstraint is non-empty, the A* state
    // is (nodeId, seqIdx) where seqIdx is the number of sequence elements
    // already traversed (0..S).  Only edges that advance, continue, or
    // pass-through the sequence are allowed once seqIdx > 0:
    //   - If seqIdx < S and edge.wayRef == sequence[seqIdx]: advance to seqIdx+1
    //   - Else if seqIdx == 0: unrestricted (pre-sequence)
    //   - Else if seqIdx == S: unrestricted (post-sequence)
    //   - Else if edge.wayRef is empty or intersection: pass-through
    //   - Else if edge.wayRef == sequence[seqIdx-1]: continue current ref
    //   - Else: blocked
    // Goal condition is (nodeId == goalId AND seqIdx == S).
    // When sequenceConstraint is empty the data structures are sized exactly
    // like the non-augmented case (stride=1) — zero overhead on the hot path.
    const int  S      = static_cast<int>(sequenceConstraint.size());
    const int  stride = S + 1;
    const bool useSeq = S > 0;
    // Flat state index: nodeId * stride + seqIdx.  When useSeq is false,
    // stride is 1 and the index collapses to nodeId — identical layout to
    // the original non-augmented code.
    auto sIdx = [stride](int n, int s)
    { return n * stride + s; };
    // Sequence-transition helper: computes the new seqIdx after traversing
    // an edge with wayRef w from state (_, curS).  Returns -1 when the
    // transition is blocked by the sequence rules.
    // Subsequence semantics: advance when the current required ref is matched;
    // otherwise stay at the same seqIdx.  The sequence entries must appear in
    // order in the final route's wayRefs list, but unrelated refs may appear
    // between them (verified post-A* by WayRefSequenceSubsequence).
    auto seqTransition = [&](int curS, const std::string& w) -> int
    {
        if (!useSeq)
            return 0;
        if (curS < S && w == sequenceConstraint[curS])
            return curS + 1;
        return curS;
    };
    // Multi-goal mode: when terminateOnWayRef is set, the first node reached
    // whose arriving-edge wayRef matches the target ref is treated as the goal.
    // The caller still supplies a @p goalId to drive the Euclidean heuristic
    // (biasing expansion toward that region), but the actual termination is
    // content-based.  Used by preferred-route inner legs which want "any node
    // on wayref X reachable under the whitelist" rather than a pre-picked ID.
    int        effectiveGoalId   = goalId;
    const bool useTerminateOnRef = !terminateOnWayRef.empty();
    if (startId == goalId && !useSeq)
    {
        TaxiRoute r;
        r.polyline   = {nodes_[startId].pos};
        r.totalDistM = 0.0;
        r.valid      = true;
        return r;
    }

    // State-augmented mode multiplies the reachable state space by (S+1),
    // so scale the expansion budget accordingly to avoid premature cut-off
    // when a multi-segment constrained route is being explored.
    const int    MAX_NODES             = apt_.taxiNetworkConfig.routing.maxNodeExpansions * stride;
    const double WAYREF_CHANGE_PENALTY = apt_.taxiNetworkConfig.routing.wayrefChangePenalty;
    const double SOFT_TURN_COST        = apt_.taxiNetworkConfig.routing.softTurnCostPerDeg;
    const double MIN_COST_PER_M        = apt_.taxiNetworkConfig.flowRules.withFlowMult; // admissibility bound
    const double HEURISTIC_W           = apt_.taxiNetworkConfig.routing.heuristicWeight * MIN_COST_PER_M;

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
        int    id;  // flat state index (nodeId * stride + seqIdx)
        int    nid; // nodeId (duplicated for fast access in the loop)
        int    sIx; // seqIdx
    };
    auto cmp = [](const PQEntry& a, const PQEntry& b)
    { return a.f > b.f; };
    std::priority_queue<PQEntry, std::vector<PQEntry>, decltype(cmp)> open(cmp);

    const size_t             stateCount = nodes_.size() * static_cast<size_t>(stride);
    std::vector<double>      g(stateCount, std::numeric_limits<double>::max());
    std::vector<int>         prev(stateCount, -1);
    std::vector<double>      incomingBearing(stateCount, -1.0);
    std::vector<std::string> incomingWayRef(stateCount, "");
    std::vector<bool>        closed(stateCount, false);
    std::vector<double>      dbgEdgeCost(emitDebugTrace ? stateCount : 0, 0.0);
    std::vector<double>      dbgTurnPenalty(emitDebugTrace ? stateCount : 0, 0.0);
    std::vector<double>      dbgSoftTurn(emitDebugTrace ? stateCount : 0, 0.0);
    std::vector<double>      dbgDepMult(emitDebugTrace ? stateCount : 0, 1.0);
    std::vector<double>      dbgBearingDiff(emitDebugTrace ? stateCount : 0, 0.0);
    int                      nodesExpanded = 0;
    int                      goalStateIdx  = -1;

    // Sequence diagnostic: max seqIdx ever reached + sample of wayRefs that
    // blocked further progress at that level.  Dumped into debugTrace on
    // useSeq failure so the caller can log *why* the constrained A* failed.
    int                                  dbgMaxSeq = 0;
    std::map<int, std::set<std::string>> dbgBlockedRefsAtSeq;
    std::map<int, int>                   dbgTurnBlocksAtSeq;
    std::map<int, std::set<std::string>> dbgTurnBlockedRefsAtSeq;
    std::map<int, int>                   dbgExpandedAtSeq;
    std::map<int, int>                   dbgRelaxedAtSeq;
    std::map<int, std::set<std::string>> dbgAdvancesAtSeq; // edges that advanced INTO this seq level

    // Seed the aircraft's current heading so the first edge is turn-checked.
    const int startStateIdx        = sIdx(startId, 0);
    incomingBearing[startStateIdx] = initialBearingDeg;

    g[startStateIdx] = 0.0;
    open.push({HEURISTIC_W * HaversineM(nodes_[startId].pos, goalPos), startStateIdx, startId, 0});

    while (!open.empty())
    {
        const auto entry = open.top();
        open.pop();
        const int cur     = entry.id;
        const int curNode = entry.nid;
        const int curSeq  = entry.sIx;

        // Skip stale priority-queue entries; once closed, g[cur] is optimal.
        if (closed[cur])
            continue;
        closed[cur] = true;
        if (useSeq)
            dbgExpandedAtSeq[curSeq]++;

        if (curNode == goalId && (!useSeq || curSeq == S))
        {
            effectiveGoalId = curNode;
            goalStateIdx    = cur;
            break;
        }
        if (useTerminateOnRef && curNode != startId && incomingWayRef[cur] == terminateOnWayRef)
        {
            // Guard against single-node-ref graph artifacts: a stub "TL 42"
            // edge embedded inside another way is not the real taxiway.
            // Require the node to have at least one OTHER outgoing edge on
            // the same ref, proving it's part of a real chain.  Intersection
            // refs (Exit NN) are naturally short — 1-2 edges — so the
            // continuation check is skipped for them, otherwise termination
            // on an intersection ref never fires.
            bool acceptable = isxWayRefs_.count(terminateOnWayRef) > 0;
            if (!acceptable)
            {
                for (const auto& e : adj_[curNode])
                {
                    if (e.to == (prev[cur] >= 0 ? prev[cur] / stride : -1))
                        continue;
                    if (e.wayRef == terminateOnWayRef)
                    {
                        acceptable = true;
                        break;
                    }
                }
            }
            if (acceptable)
            {
                effectiveGoalId = curNode;
                goalStateIdx    = cur;
                break;
            }
        }
        if (++nodesExpanded > MAX_NODES)
            break;

        for (const auto& edge : adj_[curNode])
        {
            if (!excludedRefs.empty() && excludedRefs.contains(edge.wayRef))
                continue;
            // Hard whitelist: edges on non-empty wayRefs must be in the allowed
            // set.  Empty-wayRef edges (junction connectors) and intersection
            // refs (Exit NN) always pass.  Kept for backward-compatibility with
            // the (now obsolete) leg-splicing path; state-augmented sequence
            // mode uses seqTransition instead.
            if (!allowedWayRefs.empty() && !edge.wayRef.empty() &&
                !allowedWayRefs.contains(edge.wayRef) && !isxWayRefs_.count(edge.wayRef))
                continue;
            if (!blockedNodes.empty() && blockedNodes.contains(edge.to) && edge.to != goalId)
                continue;

            // Sequence-transition gate: compute the new seqIdx for this edge.
            // In state-augmented mode this determines whether the edge is
            // allowed at all and where in state space it lands.
            const int newSeq = seqTransition(curSeq, edge.wayRef);
            if (newSeq < 0)
            {
                if (useSeq && curSeq >= dbgMaxSeq)
                {
                    if (curSeq > dbgMaxSeq)
                    {
                        dbgMaxSeq = curSeq;
                    }
                    auto& s = dbgBlockedRefsAtSeq[curSeq];
                    if (s.size() < 8)
                        s.insert(edge.wayRef);
                }
                continue;
            }
            if (useSeq && newSeq > dbgMaxSeq)
                dbgMaxSeq = newSeq;
            const int toStateIdx = sIdx(edge.to, newSeq);

            // Block HP-to-HP transitions: two different Taxiway_HoldingPoint
            // ways must not connect directly (shared junction nodes can create
            // such edges, e.g. B10→B9).  Each HP should only connect to/from
            // non-HP taxiways or intersections.
            if (hpWayRefs_.count(edge.wayRef) && !incomingWayRef[cur].empty() &&
                incomingWayRef[cur] != edge.wayRef && hpWayRefs_.count(incomingWayRef[cur]))
                continue;

            // Hard-turn block: any turn exceeding hardTurnDeg is physically
            // impossible and blocked outright.  OSM data shows max ~28° between
            // consecutive edges; LOWW's steepest legit junction (M↔E) is ~47°.
            // At the start node (no incomingWayRef) a looser 135° limit applies
            // because the aircraft heading reflects its stand/ramp orientation.
            if (incomingBearing[cur] >= 0.0)
            {
                const double bd_turn = BearingDiff(incomingBearing[cur], edge.bearingDeg);
                const double limit   = incomingWayRef[cur].empty()
                                           ? 135.0
                                           : apt_.taxiNetworkConfig.routing.hardTurnDeg;
                if (bd_turn > limit)
                {
                    if (useSeq)
                    {
                        dbgTurnBlocksAtSeq[curSeq]++;
                        auto& s = dbgTurnBlockedRefsAtSeq[curSeq];
                        if (s.size() < 8)
                            s.insert(std::format("{}->{}@{:.0f}",
                                                 incomingWayRef[cur].empty() ? "-" : incomingWayRef[cur],
                                                 edge.wayRef.empty() ? "-" : edge.wayRef, bd_turn));
                    }
                    continue;
                }
            }

            double           edgeCost;
            double           turnPenalty      = 0.0;
            double           depMult          = 1.0;
            double           bd               = 0.0;
            constexpr double SOFT_TURN_THRESH = 5.0;

            if (ignoreAllPenalties)
            {
                // Drawn-path mode: shortest geometric path — no flow, soft-turn, or wayref overhead.
                edgeCost = edge.cost / edge.flowMult;
                // Nodes collected during the draw gesture receive a strong discount to bias
                // the route toward the drawn path without hard-forcing it.
                constexpr double PREFERRED_NODE_DISCOUNT = 0.2;
                if (!preferredNodes.empty() && preferredNodes.contains(edge.to))
                    edgeCost *= PREFERRED_NODE_DISCOUNT;
            }
            else
            {
                // Normal mode: apply flow, turn, and wayref-change penalties.
                // suppressFlowWayRefs: set by FindWaypointRoute after a manual via-point
                // forced the route onto a taxiway; allows continuing against flow on that way.
                const bool suppress =
                    !suppressFlowWayRefs.empty() && suppressFlowWayRefs.count(edge.wayRef);
                if (!suppress && !edge.wayRef.empty() && hasConfigRules)
                {
                    for (const auto& rule : cfgFlowIt->second)
                    {
                        if (rule.taxiway != edge.wayRef)
                            continue;
                        const double rb = CardinalToBearing(rule.direction);
                        if (rb < 0.0)
                            continue;
                        const double diff = BearingDiff(edge.bearingDeg, rb);
                        if (diff >= apt_.taxiNetworkConfig.flowRules.againstFlowMinDeg)
                            depMult = (rule.againstFlowMult > 0.0) ? rule.againstFlowMult
                                                                   : apt_.taxiNetworkConfig.flowRules.againstFlowMult;
                        else if (diff <= apt_.taxiNetworkConfig.flowRules.withFlowMaxDeg)
                            depMult = apt_.taxiNetworkConfig.flowRules.withFlowMult;
                        break;
                    }
                }

                if (incomingBearing[cur] >= 0.0)
                    bd = BearingDiff(incomingBearing[cur], edge.bearingDeg);

                // Soft turn penalty: proportional to bearing change above a small
                // threshold.  Only applied when the incoming wayRef is a named
                // taxiway/taxilane (not an intersection) — this preserves the
                // natural cost ordering at junction curves so the arrival bearing
                // at junction nodes stays the same as baseline, avoiding
                // hard-turn blocks on the continuation due to a changed approach.
                if (bd > SOFT_TURN_THRESH && SOFT_TURN_COST > 0.0 &&
                    !isxWayRefs_.count(incomingWayRef[cur]))
                    turnPenalty += (bd - SOFT_TURN_THRESH) * SOFT_TURN_COST;

                // Penalty when changing wayRef.  Only intersection→intersection
                // transitions are exempt (chaining through a junction complex is
                // free, but exiting an intersection to a taxiway costs the penalty
                // just like any other wayRef change).
                //
                // A wayRef used for only a single edge (one-node pass-through) is a graph
                // artifact — no real taxiway or intersection consists of one node.
                // Suppress the penalty so routing isn't distorted by (e.g.) a single
                // TL42 node embedded at Exit 21.
                const bool singleNodeRef =
                    prev[cur] >= 0 && incomingWayRef[prev[cur]] != incomingWayRef[cur];
                if (!edge.wayRef.empty() &&
                    incomingWayRef[cur] != edge.wayRef &&
                    !(isxWayRefs_.count(incomingWayRef[cur]) && isxWayRefs_.count(edge.wayRef)) &&
                    !singleNodeRef)
                {
                    turnPenalty += WAYREF_CHANGE_PENALTY;
                }

                // When suppressed, remove only against-flow penalties (flowMult > 1)
                // but keep with-flow incentives (flowMult < 1).
                const double baseCost = (suppress && edge.flowMult > 1.0f) ? edge.cost / edge.flowMult : edge.cost;
                edgeCost              = baseCost * depMult;

                // Wingspan-avoid penalty: aircraft whose wingspan fits the narrow-lane limit
                // pay extra on refs listed in taxiWingspanAvoid, steering A* toward the
                // parallel narrower lane (e.g. TL40 Blue/Orange Line) when available.
                if (!avoidRefs.empty() && avoidRefs.contains(edge.wayRef))
                    edgeCost *= apt_.taxiNetworkConfig.edgeCosts.multWingspanAvoid;
            }

            const double ng = g[cur] + edgeCost + turnPenalty;

            if (ng < g[toStateIdx])
            {
                if (useSeq)
                {
                    dbgRelaxedAtSeq[newSeq]++;
                    if (newSeq > curSeq)
                    {
                        auto& s = dbgAdvancesAtSeq[newSeq];
                        if (s.size() < 4)
                            s.insert(std::format("{}->{}@{:.0f}",
                                                 incomingWayRef[cur].empty() ? "-" : incomingWayRef[cur],
                                                 edge.wayRef, incomingBearing[cur] >= 0 ? BearingDiff(incomingBearing[cur], edge.bearingDeg) : 0.0));
                    }
                }
                g[toStateIdx]               = ng;
                prev[toStateIdx]            = cur;
                incomingBearing[toStateIdx] = edge.bearingDeg;
                incomingWayRef[toStateIdx]  = edge.wayRef;
                if (emitDebugTrace)
                {
                    dbgEdgeCost[toStateIdx]    = edgeCost;
                    dbgTurnPenalty[toStateIdx] = turnPenalty;
                    dbgSoftTurn[toStateIdx]    = (bd > SOFT_TURN_THRESH && !isxWayRefs_.count(incomingWayRef[cur]))
                                                     ? (bd - SOFT_TURN_THRESH) * SOFT_TURN_COST
                                                     : 0.0;
                    dbgDepMult[toStateIdx]     = depMult;
                    dbgBearingDiff[toStateIdx] = bd;
                }
                open.push({ng + HEURISTIC_W * HaversineM(nodes_[edge.to].pos, goalPos),
                           toStateIdx, edge.to, newSeq});
            }
        }
    } // while

    // If the loop exited without finding a goal state, fail.  In state-
    // augmented mode the goal requires both nodeId and seqIdx to match; in
    // the non-augmented case goalStateIdx == sIdx(goalId, 0) when reached.
    if (goalStateIdx < 0)
    {
        // Fallback for the simple (non-sequence, non-terminate) case where
        // the outer loop exit condition preserved the original goalId-prev
        // invariant without setting goalStateIdx.
        if (!useSeq && !useTerminateOnRef)
        {
            const int flatGoal = sIdx(goalId, 0);
            if (prev[flatGoal] == -1 && goalId != startId)
                return {};
            goalStateIdx = flatGoal;
        }
        else
        {
            TaxiRoute r;
            r.valid = false;
            if (useSeq)
            {
                r.debugTrace = std::format("seqMaxReached={}/{} expanded={}", dbgMaxSeq, S, nodesExpanded);
                for (int lvl = 0; lvl <= S; ++lvl)
                {
                    int ex = dbgExpandedAtSeq.count(lvl) ? dbgExpandedAtSeq[lvl] : 0;
                    int rx = dbgRelaxedAtSeq.count(lvl) ? dbgRelaxedAtSeq[lvl] : 0;
                    if (ex == 0 && rx == 0)
                        continue;
                    r.debugTrace += std::format(" L{}:exp={}rx={}", lvl, ex, rx);
                    auto it = dbgAdvancesAtSeq.find(lvl);
                    if (it != dbgAdvancesAtSeq.end())
                    {
                        r.debugTrace += "adv[";
                        bool first = true;
                        for (const auto& a : it->second)
                        {
                            if (!first)
                                r.debugTrace += ",";
                            r.debugTrace += a;
                            first = false;
                        }
                        r.debugTrace += "]";
                    }
                }
                for (const auto& [lvl, cnt] : dbgTurnBlocksAtSeq)
                {
                    r.debugTrace += std::format(" turnBlk{}={}", lvl, cnt);
                    auto it = dbgTurnBlockedRefsAtSeq.find(lvl);
                    if (it != dbgTurnBlockedRefsAtSeq.end())
                    {
                        r.debugTrace += "[";
                        bool first = true;
                        for (const auto& rf : it->second)
                        {
                            if (!first)
                                r.debugTrace += ",";
                            r.debugTrace += rf;
                            first = false;
                        }
                        r.debugTrace += "]";
                    }
                }
                for (const auto& [lvl, refs] : dbgBlockedRefsAtSeq)
                {
                    r.debugTrace += std::format(" blockedAt{}=[", lvl);
                    bool first = true;
                    for (const auto& rf : refs)
                    {
                        if (!first)
                            r.debugTrace += ",";
                        r.debugTrace += rf.empty() ? "\"\"" : rf;
                        first = false;
                    }
                    r.debugTrace += "]";
                }
            }
            return r;
        }
    }

    // Reconstruct path.
    TaxiRoute route;
    route.valid     = true;
    route.totalCost = g[goalStateIdx];
    // Path is stored as state indices; convert to node IDs via `/ stride`.
    std::vector<int> statePath;
    for (int s = goalStateIdx; s != -1; s = prev[s])
        statePath.push_back(s);
    std::ranges::reverse(statePath);
    std::vector<int> path;
    path.reserve(statePath.size());
    for (const int s : statePath)
        path.push_back(s / stride);

    std::string lastRef;
    // Debug: accumulate per-wayRef segment stats for the trace.
    double      dbgSegDist     = 0.0; // distance accumulated on the current wayRef
    double      dbgSegCost     = 0.0; // edge cost accumulated on the current wayRef
    double      dbgSegPenalty  = 0.0; // turn/wayref penalties accumulated on the current wayRef
    double      dbgSegSoftTurn = 0.0; // soft turn cost accumulated on the current wayRef
    double      dbgSegMaxBd    = 0.0; // max bearing diff seen on the current wayRef
    double      dbgSegDepMult  = 1.0; // depMult on the current wayRef (constant per segment)
    std::string dbgSegRef;
    for (size_t pi = 0; pi < path.size(); ++pi)
    {
        const int nId      = path[pi];
        const int stateIdx = statePath[pi];
        route.polyline.push_back(nodes_[nId].pos);
        const std::string& ref = nodes_[nId].wayRef;
        if (!ref.empty() && ref != lastRef)
        {
            // Skip single-node pass-throughs: if the next node already has a
            // different wayRef, this ref covers only one node and is a graph
            // artifact (e.g. a lone "E" node between Exit 32 and Exit 22).
            const bool singleNode = pi + 1 < path.size() &&
                                    !nodes_[path[pi + 1]].wayRef.empty() &&
                                    nodes_[path[pi + 1]].wayRef != ref;
            if (!singleNode)
            {
                // Flush the previous debug segment.
                if (emitDebugTrace && !dbgSegRef.empty())
                    route.debugTrace += std::format("  [{}] {:.0f}m cost={:.0f} pen={:.0f} softTurn={:.0f} flow={:.1f}x maxTurn={:.0f}deg\n",
                                                    dbgSegRef, dbgSegDist, dbgSegCost, dbgSegPenalty,
                                                    dbgSegSoftTurn, dbgSegDepMult, dbgSegMaxBd);
                route.wayRefs.push_back(ref);
                lastRef        = ref;
                dbgSegRef      = ref;
                dbgSegDist     = 0.0;
                dbgSegCost     = 0.0;
                dbgSegPenalty  = 0.0;
                dbgSegSoftTurn = 0.0;
                dbgSegMaxBd    = 0.0;
                dbgSegDepMult  = 1.0;
            }
        }
        if (pi > 0)
        {
            const int    prevNode = path[pi - 1];
            const double stepDist = HaversineM(nodes_[prevNode].pos, nodes_[nId].pos);
            route.totalDistM += stepDist;
            if (emitDebugTrace)
            {
                dbgSegDist += stepDist;
                dbgSegCost += dbgEdgeCost[stateIdx];
                dbgSegPenalty += dbgTurnPenalty[stateIdx];
                dbgSegSoftTurn += dbgSoftTurn[stateIdx];
                dbgSegDepMult = dbgDepMult[stateIdx];
                if (dbgBearingDiff[stateIdx] > dbgSegMaxBd)
                    dbgSegMaxBd = dbgBearingDiff[stateIdx];
            }
        }
    }
    // Flush last segment.
    if (emitDebugTrace && !dbgSegRef.empty())
        route.debugTrace += std::format("  [{}] {:.0f}m cost={:.0f} pen={:.0f} softTurn={:.0f} flow={:.1f}x maxTurn={:.0f}deg\n",
                                        dbgSegRef, dbgSegDist, dbgSegCost, dbgSegPenalty,
                                        dbgSegSoftTurn, dbgSegDepMult, dbgSegMaxBd);

    if (path.size() >= 2)
    {
        const GeoPoint& a = nodes_[path[path.size() - 2]].pos;
        const GeoPoint& b = nodes_[path.back()].pos;
        route.exitBearing = BearingDeg(a, b);
    }

    return route;
}

TaxiRoute TaxiGraph::FindRoute(const GeoPoint&                 from,
                               const GeoPoint&                 to,
                               double                          wingspanM,
                               const std::set<std::string>&    activeDepRwys,
                               const std::set<std::string>&    activeArrRwys,
                               double                          initialBearingDeg,
                               const std::set<int>&            blockedNodes,
                               const std::set<std::string>&    suppressFlowWayRefs,
                               bool                            ignoreAllPenalties,
                               const std::set<int>&            preferredNodes,
                               bool                            emitDebugTrace,
                               bool                            forwardOnly,
                               double                          goalBearingDeg,
                               char                            wtc,
                               const std::string&              arrivalRunway,
                               const std::set<std::string>&    allowedWayRefs,
                               const std::vector<std::string>& sequenceConstraint) const
{
    if (nodes_.empty())
        return {};

    // Collect a broad pool of candidate start nodes, then diversify by wayRef so
    // that nearby nodes on different taxiways/intersections are always tested.
    const double FORWARD_SNAP_M  = apt_.taxiNetworkConfig.routing.forwardSnapM;
    const double BACKWARD_SNAP_M = apt_.taxiNetworkConfig.routing.backwardSnapM;

    // When forwardOnly is set (taxi-out stands), exclude backward candidates so
    // the route always starts on the taxiway ahead of the aircraft.  If no
    // forward candidates exist, fall back to the normal backward range.
    std::vector<int> pool = NearestCandidateNodes(from, initialBearingDeg,
                                                  FORWARD_SNAP_M,
                                                  forwardOnly ? 0.0 : BACKWARD_SNAP_M,
                                                  15, forwardOnly ? 0 : 5);
    if (pool.empty() && forwardOnly)
        pool = NearestCandidateNodes(from, initialBearingDeg,
                                     FORWARD_SNAP_M, BACKWARD_SNAP_M, 15, 5);
    if (pool.empty())
    {
        const int fallback = NearestNode(from);
        if (fallback >= 0)
            pool.push_back(fallback);
    }

    // If the aircraft is directly on an edge (perpendicular distance < 5 m and
    // heading aligned within 20°), restrict candidates to that wayRef.  This
    // prevents jumping to a parallel taxilane (e.g. TL40 centre vs Blue Line).
    // When the aircraft is off-network (at a stand), diversify candidates by
    // wayRef so that nearby intersections are also evaluated as potential starts.
    const std::string onEdgeRef = WayRefOnEdge(from, initialBearingDeg);

    std::vector<int> candidates;
    if (!onEdgeRef.empty())
    {
        // On-network: use candidates matching the detected wayRef.
        // Keep up to 3 forward and 2 backward so that a nearby junction node
        // behind the aircraft is still considered (e.g. just past a taxiway split).
        int nFwd = 0, nBwd = 0;
        for (const int id : pool)
        {
            if (nodes_[id].wayRef != onEdgeRef)
                continue;
            const bool isFwd =
                initialBearingDeg < 0.0 || BearingDiff(BearingDeg(from, nodes_[id].pos), initialBearingDeg) <= 90.0;
            if (isFwd && nFwd < 3)
            {
                candidates.push_back(id);
                ++nFwd;
            }
            else if (!isFwd && nBwd < 2)
            {
                candidates.push_back(id);
                ++nBwd;
            }
            if (nFwd >= 3 && nBwd >= 2)
                break;
        }

        // When on a runway, also include nearby HP-wayRef nodes so the
        // router can start from an exit the aircraft is approaching.
        // Vacation exit config excludes HP refs not whitelisted for this runway.
        // Limited to 30 m so the aircraft must be very close to the exit —
        // further HP nodes are reached naturally via runway edges.
        if (rwyWayRefs_.count(onEdgeRef))
        {
            constexpr double HP_START_SNAP_M = 30.0;
            std::set<int>    seenCands(candidates.begin(), candidates.end());
            int              nHp = 0;
            for (const int id : pool)
            {
                if (seenCands.count(id))
                    continue;
                if (!hpWayRefs_.count(nodes_[id].wayRef))
                    continue;
                if (HaversineM(from, nodes_[id].pos) > HP_START_SNAP_M)
                    continue;
                if (initialBearingDeg >= 0.0 &&
                    BearingDiff(BearingDeg(from, nodes_[id].pos), initialBearingDeg) > 90.0)
                    continue; // behind the aircraft
                candidates.push_back(id);
                if (++nHp >= 3)
                    break;
            }
        }
    }
    else
    {
        // Off-network: one candidate per (wayRef, waySubId), closest to
        // the aircraft.  Each intersection branch keeps its own nearest
        // node so an expensive arm can't shadow a cheaper one.
        using BranchKey = std::pair<std::string, int>;
        std::map<BranchKey, int> bestStart;
        for (const int id : pool)
        {
            BranchKey  key{nodes_[id].wayRef, nodes_[id].waySubId};
            const auto dist = HaversineM(from, nodes_[id].pos);
            auto       it   = bestStart.find(key);
            if (it == bestStart.end() || dist < HaversineM(from, nodes_[it->second].pos))
                bestStart[key] = id;
        }
        for (const auto& [_, id] : bestStart)
            candidates.push_back(id);
    }

    // Per-edge wingspan check: build a set of excluded wayRefs.
    // Built before goal candidates so that goals on excluded refs are skipped
    // when non-excluded alternatives exist.
    std::set<std::string> excludedRefs;
    if (wingspanM > 0.0)
        for (const auto& [ref, maxWs] : apt_.taxiWingspanMax)
            if (wingspanM > maxWs)
                excludedRefs.insert(ref);

    // Soft-avoidance refs: aircraft whose wingspan fits the narrow-lane limit are
    // steered away from these refs via a cost penalty so A* prefers a parallel
    // narrower lane (e.g. TL40 Blue/Orange Line) when available.
    std::set<std::string> avoidRefs;
    if (wingspanM > 0.0)
        for (const auto& [ref, maxWs] : apt_.taxiWingspanAvoid)
            if (wingspanM <= maxWs)
                avoidRefs.insert(ref);

    // Vacation exit restrictions: when vacating a known arrival runway,
    // exclude HP refs not listed in that runway's vacatePoints config,
    // and apply per-WTC exit and ref exclusions.
    // Activates when the aircraft is on a runway OR already on an HP exit way.
    if (!arrivalRunway.empty() && (rwyWayRefs_.count(onEdgeRef) || hpWayRefs_.count(onEdgeRef)))
    {
        auto rwyIt = apt_.runways.find(arrivalRunway);
        if (rwyIt != apt_.runways.end() && !rwyIt->second.vacatePoints.empty())
        {
            const auto& exits = rwyIt->second.vacatePoints;
            for (const auto& hpRef : hpWayRefs_)
            {
                auto exitIt = exits.find(hpRef);
                if (exitIt == exits.end())
                {
                    excludedRefs.insert(hpRef);
                }
                else if (wtc != 0)
                {
                    const auto& ve = exitIt->second;
                    if (std::find(ve.excludeWtc.begin(), ve.excludeWtc.end(), wtc) != ve.excludeWtc.end())
                        excludedRefs.insert(hpRef);
                }
            }
            if (wtc != 0)
            {
                for (const auto& [_, ve] : exits)
                {
                    auto refIt = ve.excludeRef.find(wtc);
                    if (refIt != ve.excludeRef.end())
                        for (const auto& ref : refIt->second)
                            excludedRefs.insert(ref);
                }
            }
        }
    }

    // Collect goal candidates: nearest nodes diversified by wayRef, so A*
    // can reach stands accessible from multiple taxilanes (e.g. F37 between
    // TL 36 and TL 37).  Nodes on wingspan-excluded wayRefs are skipped when
    // non-excluded alternatives exist (e.g. prefer TL 40 over Blue Line for
    // wide-body aircraft); if all candidates are excluded, keep them anyway.
    std::vector<int> goalCandidates;
    {
        const double goalSnapR = apt_.taxiNetworkConfig.snapping.goalSnapM;
        const auto&  ts        = apt_.taxiNetworkConfig.targetSelection;

        // Two-tier search (near then far) within the 90° wide cone, keeping
        // only the node closest to a reference point per (wayRef, waySubId).
        // Grouping by sub-ID means each OSM way branch (e.g. three fillet
        // arms all named "Exit 3") retains its own closest node; one
        // unreachable arm can't eliminate the usable one from the pool.
        using BranchKey    = std::pair<std::string, int>; // (wayRef, waySubId)
        auto bestPerBranch = [&](const std::vector<int>& pool, const GeoPoint& ref) -> std::vector<int>
        {
            std::map<BranchKey, int> best;
            for (const int id : pool)
            {
                BranchKey  key{nodes_[id].wayRef, nodes_[id].waySubId};
                const auto dist = HaversineM(ref, nodes_[id].pos);
                auto       it   = best.find(key);
                if (it == best.end() || dist < HaversineM(ref, nodes_[it->second].pos))
                    best[key] = id;
            }
            std::vector<int> result;
            result.reserve(best.size());
            for (const auto& [_, id] : best)
                result.push_back(id);
            return result;
        };

        std::vector<int> goalPool;
        if (goalBearingDeg >= 0.0)
        {
            goalPool = bestPerBranch(
                NearestCandidateNodes(to, goalBearingDeg, ts.nearRadiusM, 0.0, 20, 0, ts.wideConeDeg), to);
            if (goalPool.empty())
                goalPool = bestPerBranch(
                    NearestCandidateNodes(to, goalBearingDeg, ts.farRadiusM, 0.0, 20, 0, ts.wideConeDeg), to);
        }
        if (goalPool.empty())
            goalPool = bestPerBranch(NearestCandidateNodes(to, -1.0, goalSnapR, 0.0, 20, 0), to);
        if (goalPool.empty())
        {
            const int fb = NearestNode(to);
            if (fb < 0)
                return {};
            goalPool.push_back(fb);
        }

        // Separate excluded-wayRef candidates; use them only when nothing else is available.
        std::vector<int> goalFallback;
        for (const int id : goalPool)
        {
            if (!excludedRefs.empty() && excludedRefs.contains(nodes_[id].wayRef))
                goalFallback.push_back(id);
            else
                goalCandidates.push_back(id);
        }
        if (goalCandidates.empty())
            goalCandidates = std::move(goalFallback);
    }

    // Try A* from each (start, goal) combination; keep the cheapest result
    // by total cost plus snap distances from the aircraft's actual position
    // and the stand's approach point.
    //
    // Two-pass: first try only the closest start per (wayRef, waySubId) —
    // the aircraft should depart from the node it's actually sitting on.
    // If no valid route is found (e.g. the closest node is past a blocked
    // intersection), retry with the full candidate set.
    using BranchKey = std::pair<std::string, int>;
    std::map<BranchKey, int> closestStart;
    for (const int id : candidates)
    {
        BranchKey  key{nodes_[id].wayRef, nodes_[id].waySubId};
        const auto dist = HaversineM(from, nodes_[id].pos);
        auto       it   = closestStart.find(key);
        if (it == closestStart.end() || dist < HaversineM(from, nodes_[it->second].pos))
            closestStart[key] = id;
    }
    std::vector<int> primaryStarts;
    primaryStarts.reserve(closestStart.size());
    for (const auto& [_, id] : closestStart)
        primaryStarts.push_back(id);

    auto runAStarPass = [&](const std::vector<int>& starts) -> std::pair<TaxiRoute, std::string>
    {
        TaxiRoute   best;
        double      bestScore = std::numeric_limits<double>::max();
        std::string seqTrace;
        for (const int startId : starts)
        {
            for (const int goalId : goalCandidates)
            {
                TaxiRoute r = RunAStar(startId, goalId, excludedRefs, avoidRefs, blockedNodes, activeDepRwys,
                                       activeArrRwys, initialBearingDeg, suppressFlowWayRefs,
                                       ignoreAllPenalties, preferredNodes, emitDebugTrace, allowedWayRefs, "",
                                       sequenceConstraint);
                if (!r.valid)
                {
                    if (!sequenceConstraint.empty() && seqTrace.empty() && !r.debugTrace.empty())
                        seqTrace = std::format("start#{}[{}] goal#{}[{}] {}",
                                               startId, nodes_[startId].wayRef,
                                               goalId, nodes_[goalId].wayRef, r.debugTrace);
                    continue;
                }
                const double snapDist = HaversineM(from, nodes_[startId].pos) + HaversineM(to, nodes_[goalId].pos);
                const double score    = r.totalCost + snapDist;
                if (emitDebugTrace)
                    r.debugTrace =
                        std::format("start #{} [{}] goal #{} [{}] score={:.0f} (cost={:.0f} snap={:.0f})\n",
                                    startId, nodes_[startId].wayRef, goalId, nodes_[goalId].wayRef, score,
                                    r.totalCost, snapDist) +
                        r.debugTrace;
                if (score < bestScore)
                {
                    bestScore = score;
                    best      = std::move(r);
                }
            }
        }
        return {std::move(best), std::move(seqTrace)};
    };

    auto [best, seqFailureTrace] = runAStarPass(primaryStarts);
    if (!best.valid && candidates.size() > primaryStarts.size())
    {
        auto [wider, widerTrace] = runAStarPass(candidates);
        if (wider.valid)
            best = std::move(wider);
        if (seqFailureTrace.empty())
            seqFailureTrace = std::move(widerTrace);
    }
    if (emitDebugTrace)
    {
        std::string candidateInfo =
            std::format("{} starts x {} goals, from=({:.6f},{:.6f}) to=({:.6f},{:.6f})\n",
                        candidates.size(), goalCandidates.size(), from.lat, from.lon, to.lat, to.lon);
        for (const int sid : candidates)
            candidateInfo += std::format("  start #{} [{}] at ({:.6f},{:.6f}) dist={:.0f}m\n",
                                         sid, nodes_[sid].wayRef, nodes_[sid].pos.lat, nodes_[sid].pos.lon,
                                         HaversineM(from, nodes_[sid].pos));
        for (const int gid : goalCandidates)
            candidateInfo += std::format("  goal #{} [{}] at ({:.6f},{:.6f}) dist={:.0f}m\n",
                                         gid, nodes_[gid].wayRef, nodes_[gid].pos.lat, nodes_[gid].pos.lon,
                                         HaversineM(to, nodes_[gid].pos));
        if (best.valid)
            best.debugTrace = candidateInfo + best.debugTrace;
        else
            best.debugTrace = "FindRoute FAILED: " + candidateInfo;
    }
    if (!best.valid && !seqFailureTrace.empty())
        best.debugTrace = seqFailureTrace + (best.debugTrace.empty() ? "" : "\n" + best.debugTrace);
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph::FindWaypointRoute
// ─────────────────────────────────────────────────────────────────────────────

TaxiRoute TaxiGraph::FindWaypointRoute(const GeoPoint&                           origin,
                                       const std::vector<GeoPoint>&              waypoints,
                                       const GeoPoint&                           dest,
                                       double                                    wingspanM,
                                       const std::set<std::string>&              activeDepRwys,
                                       const std::set<std::string>&              activeArrRwys,
                                       double                                    initialBearingDeg,
                                       const std::set<int>&                      blockedNodes,
                                       bool                                      ignoreAllPenalties,
                                       const std::set<int>&                      preferredNodes,
                                       bool                                      emitDebugTrace,
                                       bool                                      forwardOnly,
                                       double                                    goalBearingDeg,
                                       char                                      wtc,
                                       const std::string&                        arrivalRunway,
                                       const std::vector<std::set<std::string>>& legAllowedWayRefs) const
{
    std::vector<GeoPoint> stops;
    stops.push_back(origin);
    for (const auto& wp : waypoints)
        stops.push_back(wp);
    stops.push_back(dest);

    TaxiRoute combined;
    combined.valid        = true;
    double segInitBearing = initialBearingDeg; // propagated across segment boundaries

    // Accumulated set of wayRefs for which the per-config flow penalty is suppressed.
    // Both the segment's destination wayRef (pre-added before routing so the segment
    // heading toward it is already suppressed) and its final wayRef (post-added so the
    // next segment can continue freely) are included.
    std::set<std::string> suppressFlowWayRefs;

    for (size_t i = 1; i < stops.size(); ++i)
    {
        // Pre-suppress: snap the destination to the graph and add its wayRef so
        // that routing toward a user-chosen taxiway doesn't fight the flow rule.
        if (i < stops.size() - 1)
        {
            const int destNode = NearestNode(stops[i]);
            if (destNode >= 0 && !nodes_[destNode].wayRef.empty())
                suppressFlowWayRefs.insert(nodes_[destNode].wayRef);
        }

        const bool        isLastSeg   = (i == stops.size() - 1);
        const double      segGoalBrng = isLastSeg ? goalBearingDeg : -1.0;
        const char        segWtc      = (i == 1) ? wtc : char{0};
        const std::string segArrRwy   = (i == 1) ? arrivalRunway : std::string{};
        // Per-leg whitelist: if supplied, restricts edge wayRefs for this segment
        // to force contiguity across preferred-route sequence elements.
        const std::set<std::string> segAllowed =
            (legAllowedWayRefs.size() == stops.size() - 1) ? legAllowedWayRefs[i - 1] : std::set<std::string>{};
        TaxiRoute seg = FindRoute(stops[i - 1], stops[i], wingspanM, activeDepRwys, activeArrRwys,
                                  segInitBearing, blockedNodes, suppressFlowWayRefs,
                                  ignoreAllPenalties, preferredNodes, emitDebugTrace,
                                  (i == 1) ? forwardOnly : false, segGoalBrng, segWtc, segArrRwy,
                                  segAllowed);
        if (!seg.valid)
        {
            combined.valid = false;
            return combined;
        }
        segInitBearing = seg.exitBearing;

        // Post-suppress: record the segment's actual final wayRef so the next
        // segment can continue on that taxiway against flow.
        if (i < stops.size() - 1 && !seg.wayRefs.empty())
            suppressFlowWayRefs.insert(seg.wayRefs.back());

        // Avoid duplicate junction points when concatenating.
        if (!combined.polyline.empty() && !seg.polyline.empty())
            seg.polyline.erase(seg.polyline.begin());
        combined.polyline.insert(combined.polyline.end(), seg.polyline.begin(), seg.polyline.end());
        for (const auto& ref : seg.wayRefs)
            if (combined.wayRefs.empty() || combined.wayRefs.back() != ref)
                combined.wayRefs.push_back(ref);
        // totalDistM is recomputed from the polyline at the end (see below).
        combined.totalCost += seg.totalCost;
        combined.debugTrace += seg.debugTrace;
        combined.exitBearing = seg.exitBearing;
    }

    // Recompute totalDistM from the combined polyline.  Accumulating
    // seg.totalDistM would undercount when a segment's A* start node doesn't
    // align exactly with the previous segment's end node (which can happen due
    // to goal-candidate snapping): the polyline contains the junction gap as a
    // direct edge and therefore always sums to the true geometric path length.
    combined.totalDistM = 0.0;
    for (size_t pi = 1; pi < combined.polyline.size(); ++pi)
        combined.totalDistM += HaversineM(combined.polyline[pi - 1], combined.polyline[pi]);

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

std::string TaxiGraph::PrefixedLabel(const GeoPoint& rawPos, double maxM) const
{
    // Priority 1: nearest HoldingPoint / HoldingPosition within an extended
    // radius.  A taxiway Waypoint node that is closer should not shadow a
    // nearby HP — the HP is always the more meaningful label.
    constexpr double HP_RADIUS_M = 80.0;
    double           bestHpD     = HP_RADIUS_M;
    int              bestHpId    = -1;
    for (const int id : hpNodeIds_)
    {
        const double d = HaversineM(nodes_[id].pos, rawPos);
        if (d < bestHpD)
        {
            bestHpD  = d;
            bestHpId = id;
        }
    }
    if (bestHpId >= 0)
        return "HP:" + nodes_[bestHpId].label;

    // Priority 2: nearest node of any type within maxM.
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
        return std::format("GEO:{:.6f},{:.6f}", rawPos.lat, rawPos.lon);

    const TaxiNode& n = nodes_[bestId];
    if (n.type == TaxiNodeType::Stand)
    {
        // Stand labels are stored as "ICAO:designator" — strip the ICAO prefix.
        std::string label = n.label;
        if (const auto colon = label.find(':'); colon != std::string::npos)
            label = label.substr(colon + 1);
        return "STAND:" + label;
    }

    // Waypoint: no canonical single position — use coordinates.
    return std::format("GEO:{:.6f},{:.6f}", rawPos.lat, rawPos.lon);
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

std::string TaxiGraph::WayRefOnEdge(const GeoPoint& pos, double headingDeg,
                                    double maxDistM, double maxBearingDiff) const
{
    if (headingDeg < 0.0)
        return {};

    const int rings   = static_cast<int>(std::ceil(maxDistM / GRID_CELL_M)) + 1;
    auto [cx0, cy0]   = GridCell(pos);
    double      bestD = maxDistM;
    std::string bestRef;

    for (int dx = -rings; dx <= rings; ++dx)
    {
        for (int dy = -rings; dy <= rings; ++dy)
        {
            auto it = grid_.find(GridKey(cx0 + dx, cy0 + dy));
            if (it == grid_.end())
                continue;
            for (const int id : it->second)
            {
                for (const auto& edge : adj_[id])
                {
                    if (edge.wayRef.empty())
                        continue;
                    // Check heading alignment (either direction along the edge).
                    const double bd = BearingDiff(headingDeg, edge.bearingDeg);
                    if (bd > maxBearingDiff && (180.0 - bd) > maxBearingDiff)
                        continue;
                    // Check perpendicular distance to the edge segment.
                    const double d = PointToSegmentDistM(pos, nodes_[id].pos, nodes_[edge.to].pos);
                    if (d < bestD)
                    {
                        bestD   = d;
                        bestRef = edge.wayRef;
                    }
                }
            }
        }
    }
    return bestRef;
}

int TaxiGraph::NearestNodeId(const GeoPoint& rawPos, double maxM) const
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
    return bestId;
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
                                    const airport&               ap,
                                    std::string*                 outName) const
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
        {
            if (outName)
                *outName = best->name;
            return {best->centerLat, best->centerLon};
        }
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

GeoPoint TaxiGraph::StandApproachPoint(const std::string&                    icaoStandKey,
                                       const std::map<std::string, grStand>& grStands)
{
    GeoPoint centroid = StandCentroid(icaoStandKey, grStands);
    if (centroid.lat == 0.0 && centroid.lon == 0.0)
        return centroid;
    auto it = grStands.find(icaoStandKey);
    if (it == grStands.end() || !it->second.heading.has_value())
        return centroid;
    // Aircraft parks nose-in at heading H, so it approached from bearing H+180.
    // Offset the target toward the approach side so NearestNode picks the correct taxiway.
    const double approachBearing = std::fmod(it->second.heading.value() + 180.0, 360.0);
    return OffsetPoint(centroid, approachBearing, 20.0);
}

GeoPoint TaxiGraph::HoldingPositionByLabel(const std::string& label) const
{
    for (const int id : hpNodeIds_)
    {
        if (nodes_[id].label == label)
            return nodes_[id].pos;
    }
    return {0.0, 0.0};
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

    // Truly isolated — collect undirected edge segments in the UNREACHABLE part
    // (nodes that cannot reach destId without crossing blockedNodes).
    std::vector<std::pair<GeoPoint, GeoPoint>> edges;
    std::set<std::pair<int, int>>              seen;
    for (int nodeId = 0; nodeId < static_cast<int>(adj_.size()); ++nodeId)
    {
        if (reachable.contains(nodeId) || blockedNodes.contains(nodeId))
            continue;
        for (const auto& edge : adj_[nodeId])
        {
            if (reachable.contains(edge.to) || blockedNodes.contains(edge.to))
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
    s += std::format(" | {:.0f} m | cost {:.0f}", route.totalDistM, route.totalCost);
    return s;
}
