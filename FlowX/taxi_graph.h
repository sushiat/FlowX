/**
 * @file taxi_graph.h
 * @brief Taxiway routing graph: node/edge data structures, A* pathfinding, and snapping helpers.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include "config.h"
#include "osm_taxiways.h"

#include <map>
#include <set>
#include <string>
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
    std::vector<GeoPoint>    polyline;           ///< Screen-rendered path.
    std::vector<std::string> wayRefs;            ///< Taxiway refs traversed in order (for debug).
    double                   totalDistM  = 0.0;
    double                   exitBearing = -1.0; ///< True bearing (°) of the final path edge; -1 = unknown.
    bool                     valid       = false;
};

/// @brief Formats a TaxiRoute as a human-readable string for debug logging.
/// @example "[Q] → [Exit 5] → [A] → A12 (HP) | 1430 m"
std::string FormatTaxiRoute(const TaxiRoute& route);

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
    /// @param from          Origin position (aircraft current position or prev waypoint).
    /// @param to            Destination position.
    /// @param wingspanM     Aircraft wingspan in metres; edges on narrower taxiways are excluded.
    ///                      Pass 0 to disable wingspan filtering.
    /// @param activeDepRwys Active departure runway designators; their taxiFlowDep rules are applied.
    /// @return Populated TaxiRoute on success; TaxiRoute{valid=false} if no path found.
    [[nodiscard]] TaxiRoute FindRoute(const GeoPoint&              from,
                                      const GeoPoint&              to,
                                      double                       wingspanM,
                                      const std::set<std::string>& activeDepRwys,
                                      double                       initialBearingDeg = -1.0) const;

    /// @brief Concatenates multiple A* segments: origin → wp[0] → wp[1] → … → dest.
    /// @param origin         Route start.
    /// @param waypoints      Ordered mandatory via-points (may be empty).
    /// @param dest           Route end.
    /// @param cursorSnap     Current cursor snap position (appended as a trailing segment to dest).
    /// @param wingspanM      Passed to each FindRoute call.
    /// @param activeDepRwys  Passed to each FindRoute call.
    [[nodiscard]] TaxiRoute FindWaypointRoute(const GeoPoint&              origin,
                                              const std::vector<GeoPoint>& waypoints,
                                              const GeoPoint&              dest,
                                              double                       wingspanM,
                                              const std::set<std::string>& activeDepRwys,
                                              double                       initialBearingDeg = -1.0) const;

    /// @brief Snaps @p rawPos to the nearest graph node within @p maxM metres.
    /// @return {snapped position, node label}. Returns {rawPos, ""} if no node is within range.
    [[nodiscard]] std::pair<GeoPoint, std::string> SnapNearest(const GeoPoint& rawPos,
                                                               double          maxM) const;

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

  private:
    struct Edge
    {
        int         to;
        double      cost;       ///< Pre-multiplied cost including type and flow multipliers.
        std::string wayRef;     ///< For wingspan filtering at query time.
        double      bearingDeg; ///< Direction of travel (for turn-penalty calculation).
    };

    std::vector<TaxiNode>          nodes_;
    std::vector<std::vector<Edge>> adj_;
    airport                        apt_; ///< Snapshot of airport config used during build.

    // ── Build helpers ────────────────────────────────────────────────────────

    /// @brief Returns the id of the node within @p mergeThreshM metres of @p pos,
    ///        or creates a new node and returns its id.
    int FindOrCreateNode(const GeoPoint& pos, double mergeThreshM,
                         TaxiNodeType     type,
                         std::string_view label,
                         std::string_view wayRef);

    /// @brief Adds a single directed edge from → to with pre-computed cost.
    void AddEdge(int from, int to, double cost,
                 const std::string& wayRef, double bearingDeg);

    /// @brief Returns the flow-direction cost multiplier for an edge on @p wayRef
    ///        travelling at @p bearingDeg, given the currently active flow rules.
    [[nodiscard]] double FlowMult(double                       bearingDeg,
                                  const std::string&           wayRef,
                                  const std::set<std::string>& activeDepRwys) const;

    // ── A* helpers ───────────────────────────────────────────────────────────

    /// @brief Returns the index of the node nearest to @p pos, or -1 if graph is empty.
    [[nodiscard]] int NearestNode(const GeoPoint& pos) const;
};
