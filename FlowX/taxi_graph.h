/**
 * @file taxi_graph.h
 * @brief Taxiway routing graph: node/edge data structures, A* pathfinding, and snapping helpers.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include "config.h"
#include "osm_taxiways.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Public data types
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Classification of a node in the taxi routing graph.
enum class TaxiNodeType
{
    Waypoint,        ///< Generic geometry point along a taxiway/taxilane way.
    HoldingPoint,    ///< Config-defined holding-point centroid (from holdingPoints polygon).
    HoldingPosition, ///< OSM node with aeroway=holding_position.
    Stand,           ///< Stand polygon centroid derived from GRpluginStands data.
};

/// @brief A single vertex in the taxi routing graph.
struct TaxiNode
{
    int          id;
    GeoPoint     pos;
    TaxiNodeType type;
    std::string  label;  ///< ref or name (for debug output and snap labels).
    std::string  wayRef; ///< Taxiway ref this node belongs to; empty for stand/HP nodes.
};

/// @brief An ordered sequence of geographic positions forming a taxi path.
struct TaxiRoute
{
    std::vector<GeoPoint>    polyline;   ///< Screen-rendered path.
    std::vector<std::string> wayRefs;    ///< Taxiway refs traversed in order (for debug).
    std::string              debugTrace; ///< Per-edge breakdown of costs and penalties (populated only when debug is on).
    double                   totalDistM  = 0.0;
    double                   totalCost   = 0.0;  ///< Sum of A* edge costs + penalties (for debug cost comparison).
    double                   exitBearing = -1.0; ///< True bearing (°) of the final path edge; -1 = unknown.
    bool                     valid       = false;
};

/// @brief Formats a TaxiRoute as a human-readable string for debug logging.
/// @example "[Q] → [Exit 5] → [A] → A12 (HP) | 1430 m"
std::string FormatTaxiRoute(const TaxiRoute& route);

// ─────────────────────────────────────────────────────────────────────────────
// Geometry helpers (also used by RadarScreen for safety monitoring)
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr double TAXI_EARTH_R = 6'371'000.0; ///< Earth radius in metres.

/// @brief Haversine distance between two WGS-84 points, in metres.
inline double HaversineM(const GeoPoint& a, const GeoPoint& b)
{
    const double dLat = (b.lat - a.lat) * std::numbers::pi / 180.0;
    const double dLon = (b.lon - a.lon) * std::numbers::pi / 180.0;
    const double cosA = std::cos(a.lat * std::numbers::pi / 180.0);
    const double cosB = std::cos(b.lat * std::numbers::pi / 180.0);
    const double h    = std::sin(dLat / 2) * std::sin(dLat / 2) + cosA * cosB * std::sin(dLon / 2) * std::sin(dLon / 2);
    return TAXI_EARTH_R * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
}

/// @brief Forward bearing in degrees [0, 360) from @p a to @p b.
inline double BearingDeg(const GeoPoint& a, const GeoPoint& b)
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
inline double BearingDiff(double a, double b)
{
    double d = std::fmod(std::abs(a - b), 360.0);
    return d > 180.0 ? 360.0 - d : d;
}

/// @brief Flat-earth perpendicular distance from point @p P to segment [@p A, @p B], in metres.
inline double PointToSegmentDistM(const GeoPoint& P, const GeoPoint& A, const GeoPoint& B)
{
    const double cosLat   = std::cos(A.lat * std::numbers::pi / 180.0);
    const double scaleLat = TAXI_EARTH_R * std::numbers::pi / 180.0;
    const double scaleLon = TAXI_EARTH_R * cosLat * std::numbers::pi / 180.0;

    const double abx = (B.lon - A.lon) * scaleLon;
    const double aby = (B.lat - A.lat) * scaleLat;
    const double apx = (P.lon - A.lon) * scaleLon;
    const double apy = (P.lat - A.lat) * scaleLat;

    const double len2 = abx * abx + aby * aby;
    if (len2 < 1e-10)
        return HaversineM(P, A);

    const double   t = std::clamp((apx * abx + apy * aby) / len2, 0.0, 1.0);
    const GeoPoint proj{A.lat + aby * t / scaleLat, A.lon + abx * t / scaleLon};
    return HaversineM(P, proj);
}

/// @brief Parametric intersection of two geo line segments using flat-earth projection centred on @p a0.
/// @param[out] outPt  Set to the intersection GeoPoint when the function returns true.
/// @return True if the segments properly intersect within their bounds.
inline bool SegmentIntersectGeo(const GeoPoint& a0, const GeoPoint& a1,
                                const GeoPoint& b0, const GeoPoint& b1,
                                GeoPoint& outPt)
{
    const double cosLat   = std::cos(a0.lat * std::numbers::pi / 180.0);
    const double scaleLat = TAXI_EARTH_R * std::numbers::pi / 180.0;
    const double scaleLon = TAXI_EARTH_R * cosLat * std::numbers::pi / 180.0;

    // Vectors in flat-earth metres relative to a0.
    const double rx = (a1.lon - a0.lon) * scaleLon;
    const double ry = (a1.lat - a0.lat) * scaleLat;
    const double qx = (b0.lon - a0.lon) * scaleLon;
    const double qy = (b0.lat - a0.lat) * scaleLat;
    const double sx = (b1.lon - b0.lon) * scaleLon;
    const double sy = (b1.lat - b0.lat) * scaleLat;

    const double denom = rx * sy - ry * sx; // cross(r, s)
    if (std::abs(denom) < 1e-9)
        return false; // parallel or collinear

    const double t = (qx * sy - qy * sx) / denom; // cross(q, s) / denom
    const double u = (qx * ry - qy * rx) / denom; // cross(q, r) / denom

    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0)
        return false;

    outPt.lat = a0.lat + ry * t / scaleLat;
    outPt.lon = a0.lon + rx * t / scaleLon;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TaxiGraph
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Directed weighted graph of the taxiway network.
///
/// Build once after OSM data is loaded/annotated; reuse for all route queries.
/// Edges are directed: each physical taxiway segment produces two directed edges
/// (forward and reverse). Edges going against an active flow rule receive a ×10
/// cost multiplier; edges on wingspan-restricted taxiways are excluded entirely
/// for oversized aircraft.
class TaxiGraph
{
  public:
    /// @brief (Re)builds the graph from annotated OSM data and airport configuration.
    /// @note Clears and replaces any previously built graph.
    void Build(const OsmAirportData& osm, const airport& ap);

    /// @brief Returns true if the graph has been successfully built (at least one node).
    [[nodiscard]] bool IsBuilt() const
    {
        return !nodes_.empty();
    }

    /// @brief Returns the total number of nodes (for logging).
    [[nodiscard]] int NodeCount() const
    {
        return static_cast<int>(nodes_.size());
    }

    /// @brief Finds the least-cost path from @p from to @p to using A*.
    ///
    /// Both endpoints are snapped to the nearest graph node before searching.
    /// @param from              Origin position (aircraft current position or prev waypoint).
    /// @param to                Destination position.
    /// @param wingspanM         Aircraft wingspan in metres; edges on narrower taxiways are excluded.
    ///                          Pass 0 to disable wingspan filtering.
    /// @param activeDepRwys     Active departure runway designators.
    /// @param activeArrRwys     Active arrival runway designators.
    ///                          Together dep+arr form the taxiFlowConfigs key applied during A*.
    /// @param initialBearingDeg Aircraft heading for forward-node selection; -1 = unknown.
    /// @param blockedNodes      Node IDs that A* may not expand through (e.g. active push routes).
    /// @return Populated TaxiRoute on success; TaxiRoute{valid=false} if no path found.
    [[nodiscard]] TaxiRoute FindRoute(const GeoPoint&              from,
                                      const GeoPoint&              to,
                                      double                       wingspanM,
                                      const std::set<std::string>& activeDepRwys,
                                      const std::set<std::string>& activeArrRwys,
                                      double                       initialBearingDeg   = -1.0,
                                      const std::set<int>&         blockedNodes        = {},
                                      const std::set<std::string>& suppressFlowWayRefs = {},
                                      bool                         ignoreAllPenalties  = false,
                                      const std::set<int>&         preferredNodes      = {},
                                      bool                         emitDebugTrace      = false) const;

    /// @brief Concatenates multiple A* segments: origin → wp[0] → wp[1] → … → dest.
    /// @param origin            Route start.
    /// @param waypoints         Ordered mandatory via-points (may be empty).
    /// @param dest              Route end.
    /// @param wingspanM         Passed to each FindRoute call.
    /// @param activeDepRwys     Passed to each FindRoute call.
    /// @param activeArrRwys     Passed to each FindRoute call.
    /// @param initialBearingDeg Aircraft heading; forwarded to the first FindRoute segment.
    /// @param blockedNodes      Forwarded to every FindRoute call.
    [[nodiscard]] TaxiRoute FindWaypointRoute(const GeoPoint&              origin,
                                              const std::vector<GeoPoint>& waypoints,
                                              const GeoPoint&              dest,
                                              double                       wingspanM,
                                              const std::set<std::string>& activeDepRwys,
                                              const std::set<std::string>& activeArrRwys,
                                              double                       initialBearingDeg  = -1.0,
                                              const std::set<int>&         blockedNodes       = {},
                                              bool                         ignoreAllPenalties = false,
                                              const std::set<int>&         preferredNodes     = {},
                                              bool                         emitDebugTrace     = false) const;

    /// @brief Returns the merged set of flow rules for the given active runway configuration.
    /// Combines taxiFlowGeneric with the taxiFlowConfigs entry whose key matches dep+arr.
    /// Used by the taxi graph overlay to render directional chevrons.
    [[nodiscard]] std::vector<TaxiFlowRule> GetActiveFlowRules(
        const std::set<std::string>& activeDepRwys,
        const std::set<std::string>& activeArrRwys) const;

    /// @brief Snaps @p rawPos to the nearest graph node within @p maxM metres.
    /// @return {snapped position, node label}. Returns {rawPos, ""} if no node is within range.
    [[nodiscard]] std::pair<GeoPoint, std::string> SnapNearest(const GeoPoint& rawPos,
                                                               double          maxM) const;

    /// @brief Returns the wayRef of the nearest Waypoint node within @p maxM metres of @p rawPos.
    /// @return Empty string if no Waypoint node is within range.
    [[nodiscard]] std::string WayRefAt(const GeoPoint& rawPos, double maxM) const;

    /// @brief Returns the wayRef of the edge the aircraft is currently on, detected by
    /// perpendicular distance (< @p maxDistM) and heading alignment (< @p maxBearingDiff).
    /// Uses the spatial grid for efficient lookup.
    /// @return Empty string if no matching edge is found.
    [[nodiscard]] std::string WayRefOnEdge(const GeoPoint& pos, double headingDeg,
                                           double maxDistM = 5.0, double maxBearingDiff = 20.0) const;

    /// @brief Returns the ID of the nearest node of any type within @p maxM metres of @p rawPos.
    /// @return Node ID, or -1 if no node is within range.
    [[nodiscard]] int NearestNodeId(const GeoPoint& rawPos, double maxM) const;

    /// @brief Determines the best snap point for interactive planning mode.
    ///
    /// Priority order:
    ///   1. Nearest HoldingPoint / HoldingPosition node within 30 m.
    ///   2. Nearest Waypoint that is a pre-intersection position within 15 m.
    ///   3. Nearest point on @p suggested route polyline within 20 m.
    ///   4. Nearest Waypoint node within 40 m.
    ///   5. @p rawPos unchanged.
    [[nodiscard]] GeoPoint SnapForPlanning(const GeoPoint&  rawPos,
                                           const TaxiRoute& suggested) const;

    /// @brief Returns the centroid of the first (lowest config-order) assignable holding point
    ///        on the first active departure runway found in @p activeDepRwys.
    /// @return {0,0} if no suitable holding point is found.
    [[nodiscard]] GeoPoint BestDepartureHP(const std::set<std::string>& activeDepRwys,
                                           const airport&               ap) const;

    /// @brief Computes the centroid of a stand polygon.
    /// @param icaoStandKey  Key in the grStands map, e.g. "LOWW:B67".
    /// @param grStands      The stands map from CFlowX_Settings.
    /// @return {0,0} if the key is not found.
    [[nodiscard]] static GeoPoint StandCentroid(const std::string&                    icaoStandKey,
                                                const std::map<std::string, grStand>& grStands);

    /// @brief Returns the position of the HoldingPoint or HoldingPosition node whose label
    ///        matches @p label (case-sensitive), or {0,0} if not found.
    [[nodiscard]] GeoPoint HoldingPositionByLabel(const std::string& label) const;

    /// @brief Returns node IDs within @p radiusM metres of any point in @p polyline.
    /// Use to convert a push-route polyline into a blocked-node set for FindRoute.
    [[nodiscard]] std::set<int> NodesToBlock(const std::vector<GeoPoint>& polyline,
                                             double                       radiusM = 3.0) const;

    /// @brief Walks the taxiway network greedily from @p from in @p bearingDeg for up to @p maxDistM metres.
    ///
    /// At each node the neighbour edge whose bearing is closest to the current travel bearing is
    /// selected (±90° maximum deviation); the travel bearing updates to each chosen edge so curves
    /// are followed naturally. Stops at dead ends or when maxDistM is reached (final segment is
    /// interpolated). The start position is snapped to the nearest graph node.
    /// Edges on wingspan-restricted taxiways are skipped when @p wingspanM > 0.
    /// @return Polyline starting at the snapped origin; valid = true when at least one node was found.
    [[nodiscard]] TaxiRoute WalkGraph(const GeoPoint& from, double bearingDeg,
                                      double maxDistM, double wingspanM = 0.0) const;

    /// @brief A candidate taxiway node for push-zone pivot selection.
    struct PushPivotCandidate
    {
        GeoPoint    pos;         ///< Graph node position on the taxiway.
        double      distM = 0.0; ///< Distance along the push axis from the stand origin.
        std::string wayRef;      ///< Taxiway ref of the candidate node.
    };

    /// @brief Finds candidate taxiway pivot points ahead of @p origin in @p bearingDeg.
    ///
    /// Scans all Waypoint nodes within @p maxDistM and ±60 m lateral of the push axis.
    /// Returns one representative node per distinct taxiway ref (the one closest to the axis),
    /// sorted by along-axis distance. Nodes on wingspan-restricted taxiways are excluded.
    [[nodiscard]] std::vector<PushPivotCandidate> PushCandidates(const GeoPoint& origin,
                                                                 double          bearingDeg,
                                                                 double          wingspanM,
                                                                 double          maxDistM) const;

    /// @brief Result of a swingover snap: cross-point on the current lane, partner point,
    ///        pre-computed s-bend intermediate points, and lane bearings.
    ///        valid = false when no swingover applies.
    struct SwingoverResult
    {
        GeoPoint              crossPt;             ///< Nearest node on the current lane to the partner snap.
        GeoPoint              partnerPt;           ///< Nearest node on the partner lane to the cursor.
        double                brngAtCross   = 0.0; ///< Mean bearing of current-lane edges at crossPt.
        double                brngAtPartner = 0.0; ///< Mean bearing of partner-lane edges at partnerPt.
        std::vector<GeoPoint> sbendPts;            ///< Two intermediate GeoPoints forming the s-bend crossover from crossPt to partnerPt.
        std::string           partnerRef;          ///< WayRef of the partner taxilane.
        bool                  valid = false;
    };

    /// @brief Finds the swingover partner snap for @p rawPos on @p currentRef.
    ///
    /// Looks up the swingover pair containing @p currentRef, checks wingspan restriction on the
    /// partner, then returns the nearest partner-lane node and the cross-point on the current lane.
    /// Returns SwingoverResult{valid=false} if no pair matches, partner is wingspan-restricted,
    /// or no partner node is within @p maxM metres.
    [[nodiscard]] SwingoverResult SwingoverSnap(
        const GeoPoint&                                rawPos,
        const std::string&                             currentRef,
        const std::vector<std::array<std::string, 2>>& pairs,
        double                                         wingspanM,
        double                                         headingDeg,
        double                                         maxM = 80.0) const;

    /// @brief Returns edge geometry (GeoPoint pairs) forming the dead-end sub-graph that
    ///        contains @p dest but is cut off by @p blockedNodes.
    /// Returns empty when @p dest is still reachable through unblocked paths.
    [[nodiscard]] std::vector<std::pair<GeoPoint, GeoPoint>> DeadEndEdges(
        const GeoPoint& dest, const std::set<int>& blockedNodes) const;

    /// @brief Derived runway centreline polylines (one per runway pair), populated by Build().
    /// Used for overlay rendering; not part of the OSM way data set.
    std::vector<std::vector<GeoPoint>> runwayCentrelines;

    /// @brief A directed edge in the routing graph.
    struct Edge
    {
        int         to;
        double      cost;            ///< Pre-multiplied cost including type and flow multipliers.
        float       flowMult = 1.0f; ///< The generic flow multiplier baked into cost (1.0 = no penalty). Used by RunAStar to divide it out when flow suppression is active.
        std::string wayRef;          ///< For wingspan filtering at query time.
        double      bearingDeg;      ///< Direction of travel (for turn-penalty calculation).
    };

    /// @brief Read-only access to the node list; used for graph overlay rendering.
    [[nodiscard]] const std::vector<TaxiNode>& Nodes() const
    {
        return nodes_;
    }

    /// @brief Read-only access to the adjacency list; used for graph overlay rendering.
    [[nodiscard]] const std::vector<std::vector<Edge>>& Adj() const
    {
        return adj_;
    }

  private:
    std::vector<TaxiNode>          nodes_;
    std::vector<std::vector<Edge>> adj_;
    airport                        apt_;           ///< Snapshot of airport config used during build.
    double                         gridLatStep_{}; ///< Grid cell height in degrees (set in Build).
    double                         gridLonStep_{}; ///< Grid cell width in degrees (set in Build).
    std::unordered_map<int64_t, std::vector<int>>
                     grid_;       ///< Spatial hash: packed(cx,cy) → node IDs; rebuilt every Build().
    std::vector<int> hpNodeIds_;  ///< IDs of all HoldingPoint/HoldingPosition nodes; built in Build() for O(k) SnapForPlanning priority-1 scan.
    std::vector<int> isxNodeIds_; ///< IDs of all intersection Waypoint nodes (label contains "Exit"); built in Build() for O(k) SnapForPlanning priority-2 scan.
    std::unordered_map<std::string, std::vector<int>>
        wayRefNodes_; ///< wayRef → node IDs for all Waypoint nodes on that ref; built in Build() for O(k) SwingoverSnap scans.
    std::unordered_set<std::string>
        isxWayRefs_; ///< Set of wayRef strings for intersection ways (from apt_.taxiIntersections); built in Build() to suppress WAYREF_CHANGE_PENALTY at Exit transitions in RunAStar.

    // ── Build helpers ────────────────────────────────────────────────────────

    /// @brief Returns the id of the node within @p mergeThreshM metres of @p pos,
    ///        or creates a new node and returns its id.
    int FindOrCreateNode(const GeoPoint& pos, double mergeThreshM,
                         TaxiNodeType     type,
                         std::string_view label,
                         std::string_view wayRef);

    /// @brief Returns the (cx, cy) spatial grid cell for @p p.
    [[nodiscard]] std::pair<int, int> GridCell(const GeoPoint& p) const;

    /// @brief Packs (cx, cy) into a single int64 hash key.
    [[nodiscard]] static int64_t GridKey(int cx, int cy);

    /// @brief Inserts node @p id into the spatial grid.
    void GridInsert(int id);

    /// @brief Adds a single directed edge from → to with pre-computed cost.
    /// @param flowMult The generic flow multiplier already baked into @p cost (1.0 = no penalty).
    void AddEdge(int from, int to, double cost, float flowMult,
                 const std::string& wayRef, double bearingDeg);

    /// @brief Returns the generic flow-direction cost multiplier for @p wayRef at @p bearingDeg.
    /// Only taxiFlowGeneric rules are evaluated here; config-specific rules from taxiFlowConfigs
    /// are applied separately at A* query time.
    [[nodiscard]] double FlowMult(double bearingDeg, const std::string& wayRef) const;

    // ── A* helpers ───────────────────────────────────────────────────────────

    /// @brief Returns the index of the node nearest to @p pos within @p maxM metres,
    ///        or -1 if the graph is empty or no node falls within that radius.
    [[nodiscard]] int NearestNode(const GeoPoint& pos, double maxM = 5000.0) const;

    /// @brief Returns the nearest node within @p maxM metres that lies in the forward hemisphere
    ///        (bearing from @p pos to node within ±90° of @p headingDeg).
    /// @return Node index, or -1 if no qualifying node is found.
    [[nodiscard]] int NearestForwardNode(const GeoPoint& pos, double headingDeg, double maxM) const;

    /// @brief Returns the nearest node within @p maxM metres that lies in the backward hemisphere
    ///        (bearing from @p pos to node more than ±90° from @p headingDeg).
    /// @return Node index, or -1 if no qualifying node is found.
    [[nodiscard]] int NearestBackwardNode(const GeoPoint& pos, double headingDeg, double maxM) const;

    /// @brief Returns up to @p maxFwd nearest forward nodes within @p maxFwdM metres,
    ///        followed by up to @p maxBwd nearest backward nodes within @p maxBwdM metres.
    ///        Forward = bearing from @p pos to node within ±90° of @p headingDeg.
    ///        Falls back to unconstrained nearest when @p headingDeg < 0.
    [[nodiscard]] std::vector<int> NearestCandidateNodes(const GeoPoint& pos,
                                                         double          headingDeg,
                                                         double          maxFwdM,
                                                         double          maxBwdM,
                                                         int             maxFwd = 3,
                                                         int             maxBwd = 2) const;

    /// @brief Returns mean bearing of all edges on @p wayRef leaving node @p nodeId.
    /// @return 0.0 if no matching edges found.
    [[nodiscard]] double NodeLaneBearing(int nodeId, const std::string& wayRef) const;

    /// @brief Returns the bearing of the outgoing edge on @p wayRef at @p nodeId
    ///        most closely aligned with @p headingDeg.
    /// @return @p headingDeg if no matching edges found.
    [[nodiscard]] double ForwardEdgeBearing(int nodeId, const std::string& wayRef, double headingDeg) const;

    /// @brief Core A* search from a pre-resolved start node to a goal node.
    /// @param suppressFlowWayRefs WayRefs for which both the baked-in and runtime flow
    ///        multipliers are ignored; used to allow continued travel on a taxiway after
    ///        a manual via-point forced the route onto it against flow.
    /// @param preferredNodes Node IDs collected from a middle-drag draw gesture; edges
    ///        leading to these nodes receive a cost discount to bias routing toward the
    ///        drawn path.
    /// @return Populated TaxiRoute on success; invalid TaxiRoute if no path found.
    [[nodiscard]] TaxiRoute RunAStar(int                          startId,
                                     int                          goalId,
                                     const std::set<std::string>& excludedRefs,
                                     const std::set<int>&         blockedNodes,
                                     const std::set<std::string>& activeDepRwys,
                                     const std::set<std::string>& activeArrRwys,
                                     double                       initialBearingDeg,
                                     const std::set<std::string>& suppressFlowWayRefs = {},
                                     bool                         ignoreAllPenalties  = false,
                                     const std::set<int>&         preferredNodes      = {},
                                     bool                         emitDebugTrace      = false) const;
};
