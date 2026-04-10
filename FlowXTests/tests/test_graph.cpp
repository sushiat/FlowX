#include "pch.h"
// test_graph.cpp
// Unit tests for TaxiGraph::Build() and TaxiGraph::FindRoute().
//
// Uses a minimal synthetic OsmAirportData so the tests are self-contained
// and do not depend on any network access or disk files.

#include <doctest/doctest.h>
#include "taxi_graph.h"
#include "config.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Build a minimal airport config with the given taxiway refs allowed.
static airport MakeAirport(std::vector<std::string> taxiways  = {"M"},
                           std::vector<std::string> taxilanes = {})
{
    airport ap;
    ap.icao      = "TEST";
    ap.taxiWays  = taxiways;
    ap.taxiLanes = taxilanes;
    // Leave TaxiNetworkConfig at its defaults (subdivision 15 m, etc.)
    return ap;
}

// Build a straight east-west taxiway:  A ──── B ──── C
// Each segment is 200 m (total 400 m).
// Returns a node coordinate map: "A", "B", "C".
static OsmAirportData MakeStraightWay()
{
    // At lat 48.110, 1° lon ≈ 74 000 m  →  200 m ≈ 0.002703°
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427}; // ~200 m east of A
    GeoPoint C{48.1100, 16.5454}; // ~200 m east of B

    OsmWay way;
    way.id       = 1;
    way.type     = AerowayType::Taxiway;
    way.ref      = "M";
    way.geometry = {A, B, C};

    OsmAirportData osm;
    osm.ways.push_back(way);
    return osm;
}

// Build an L-shaped network:  A ──── B ──── C
//                                   |
//                                   D   (B-D goes north ~200 m)
static OsmAirportData MakeLShapedWay()
{
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427}; // ~200 m east of A
    GeoPoint C{48.1100, 16.5454}; // ~200 m east of B
    GeoPoint D{48.1118, 16.5427}; // ~200 m north of B  (1° lat ≈ 111 195 m → 200 m ≈ 0.0018°)

    OsmWay mainWay;
    mainWay.id       = 1;
    mainWay.type     = AerowayType::Taxiway;
    mainWay.ref      = "M";
    mainWay.geometry = {A, B, C};

    OsmWay branchWay;
    branchWay.id       = 2;
    branchWay.type     = AerowayType::Taxiway;
    branchWay.ref      = "M";
    branchWay.geometry = {B, D};

    OsmAirportData osm;
    osm.ways.push_back(mainWay);
    osm.ways.push_back(branchWay);
    return osm;
}

// ─── Build ───────────────────────────────────────────────────────────────────

TEST_CASE("TaxiGraph::Build - produces nodes from a single straight way")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());
    CHECK(g.IsBuilt());
    // Two 200 m segments at 15 m subdivision → ~13 nodes per segment, shared endpoint B
    CHECK(g.NodeCount() > 10);
}

TEST_CASE("TaxiGraph::Build - L-shaped network produces more nodes than single way")
{
    TaxiGraph straight, lshaped;
    straight.Build(MakeStraightWay(), MakeAirport());
    lshaped.Build(MakeLShapedWay(), MakeAirport());
    CHECK(lshaped.NodeCount() > straight.NodeCount());
}

TEST_CASE("TaxiGraph::Build - empty OSM data produces no nodes")
{
    TaxiGraph      g;
    OsmAirportData empty;
    g.Build(empty, MakeAirport());
    CHECK_FALSE(g.IsBuilt());
}

// ─── FindRoute ───────────────────────────────────────────────────────────────

TEST_CASE("TaxiGraph::FindRoute - straight path A to C is valid")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint from{48.1100, 16.5400}; // near A
    GeoPoint to{48.1100, 16.5454};   // near C

    TaxiRoute route = g.FindRoute(from, to, 0.0, {}, {});
    CHECK(route.valid);
    // Total distance should be roughly 400 m (±20%)
    CHECK(route.totalDistM == doctest::Approx(400.0).epsilon(0.25));
}

TEST_CASE("TaxiGraph::FindRoute - reverse path C to A is also valid")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint from{48.1100, 16.5454};
    GeoPoint to{48.1100, 16.5400};

    TaxiRoute route = g.FindRoute(from, to, 0.0, {}, {});
    CHECK(route.valid);
}

TEST_CASE("TaxiGraph::FindRoute - position far from graph returns invalid route")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint from{48.1100, 16.5400}; // on the graph
    GeoPoint to{48.2000, 17.0000};   // far away — unreachable

    TaxiRoute route = g.FindRoute(from, to, 0.0, {}, {});
    CHECK_FALSE(route.valid);
}

TEST_CASE("TaxiGraph::FindRoute - L-shaped: A to C routes through shared node B")
{
    // The L-shape shares node B between the two ways.  A→C is a straight run
    // through B (~400 m) with no bearing change, so it is reachable.
    TaxiGraph g;
    g.Build(MakeLShapedWay(), MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute route = g.FindRoute(A, C, 0.0, {}, {});
    CHECK(route.valid);
    CHECK(route.totalDistM == doctest::Approx(400.0).epsilon(0.25));
}

TEST_CASE("TaxiGraph::FindRoute - L-shaped: 90 degree branch is hard-blocked")
{
    // The A→D path requires a ~90° turn at B, which exceeds hardTurnDeg (50°).
    // The router should not find a valid route.
    TaxiGraph g;
    g.Build(MakeLShapedWay(), MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint D{48.1118, 16.5427};

    TaxiRoute route = g.FindRoute(A, D, 0.0, {}, {});
    CHECK_FALSE(route.valid);
}

TEST_CASE("TaxiGraph::FindRoute - wingspan 0 does not exclude any taxiway")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    TaxiRoute route = g.FindRoute({48.1100, 16.5400}, {48.1100, 16.5454}, 0.0, {}, {});
    CHECK(route.valid);
}

TEST_CASE("TaxiGraph::FindRoute - wingspan restriction blocks narrow taxilane")
{
    // Create a narrow taxilane (max wingspan 10 m) alongside a wider taxiway.
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};

    OsmWay narrowLane;
    narrowLane.id       = 1;
    narrowLane.type     = AerowayType::Taxilane;
    narrowLane.ref      = "TL1";
    narrowLane.geometry = {A, B};

    OsmAirportData osm;
    osm.ways.push_back(narrowLane);

    airport ap         = MakeAirport({}, {"TL1"});
    ap.taxiWingspanMax = {{"TL1", 10.0}};

    TaxiGraph g;
    g.Build(osm, ap);

    // A 30 m wingspan aircraft cannot use the narrow lane
    TaxiRoute blocked = g.FindRoute(A, B, 30.0, {}, {});
    CHECK_FALSE(blocked.valid);

    // A 9 m wingspan aircraft can use it
    TaxiRoute allowed = g.FindRoute(A, B, 9.0, {}, {});
    CHECK(allowed.valid);
}

// ─── Flow rules ──────────────────────────────────────────────────────────────

// Straight way split into two named refs: "M" (A→B) and "N" (B→C).
// Same geometry as MakeStraightWay() but the second half has a different ref,
// so routing A→C triggers exactly one wayRef change at B.
static OsmAirportData MakeSplitRefWay()
{
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};
    GeoPoint C{48.1100, 16.5454};

    OsmWay first;
    first.id       = 1;
    first.type     = AerowayType::Taxiway;
    first.ref      = "M";
    first.geometry = {A, B};

    OsmWay second;
    second.id       = 2;
    second.type     = AerowayType::Taxiway;
    second.ref      = "N";
    second.geometry = {B, C};

    OsmAirportData osm;
    osm.ways.push_back(first);
    osm.ways.push_back(second);
    return osm;
}

// Straight way using Taxiway_Intersection type (same geometry as MakeStraightWay).
static OsmAirportData MakeIntersectionWay()
{
    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    OsmWay way;
    way.id       = 1;
    way.type     = AerowayType::Taxiway_Intersection;
    way.ref      = "ISX";
    way.geometry = {A, C};

    OsmAirportData osm;
    osm.ways.push_back(way);
    return osm;
}

TEST_CASE("TaxiGraph flow - against-flow route costs more than with-flow (taxiFlowGeneric)")
{
    // Straight way runs East.  A generic flow rule for "M" heading "E" bakes
    // withFlowMult (0.9) into A→C edges and againstFlowMult (3.0) into C→A edges.
    airport ap = MakeAirport();
    ap.taxiFlowGeneric.push_back({"M", "E"});

    TaxiGraph g;
    g.Build(MakeStraightWay(), ap);

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute withFlow    = g.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute againstFlow = g.FindRoute(C, A, 0.0, {}, {});

    REQUIRE(withFlow.valid);
    REQUIRE(againstFlow.valid);

    // totalCost / totalDistM equals the flow multiplier regardless of which snap node
    // FindRoute chose — the snap offset cancels out of the ratio.
    CHECK(withFlow.totalCost == doctest::Approx(withFlow.totalDistM * 0.9).epsilon(0.05));
    CHECK(againstFlow.totalCost == doctest::Approx(againstFlow.totalDistM * 3.0).epsilon(0.05));
}

TEST_CASE("TaxiGraph flow - no flow rule means both directions cost the same")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute fwd = g.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute rev = g.FindRoute(C, A, 0.0, {}, {});

    REQUIRE(fwd.valid);
    REQUIRE(rev.valid);
    // Without flow rules every edge cost equals its geometric length (1.0×).
    CHECK(fwd.totalCost == doctest::Approx(fwd.totalDistM).epsilon(0.01));
    CHECK(rev.totalCost == doctest::Approx(rev.totalDistM).epsilon(0.01));
}

TEST_CASE("TaxiGraph flow - runway-conditional flow rule raises against-flow cost (taxiFlowConfigs)")
{
    // Config key for dep runway "29", no arr runway: "29_"
    airport ap = MakeAirport();
    ap.taxiFlowConfigs["29_"].push_back({"M", "E"});

    TaxiGraph g;
    g.Build(MakeStraightWay(), ap);

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    // Without the runway config active: cost/distM = 1.0 for both directions.
    TaxiRoute fwdOff = g.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute revOff = g.FindRoute(C, A, 0.0, {}, {});
    REQUIRE(fwdOff.valid);
    REQUIRE(revOff.valid);
    CHECK(fwdOff.totalCost == doctest::Approx(fwdOff.totalDistM).epsilon(0.01));
    CHECK(revOff.totalCost == doctest::Approx(revOff.totalDistM).epsilon(0.01));

    // With dep runway "29" active: A→C (with flow) stays at 1.0×; C→A (against flow) rises to 3.0×.
    TaxiRoute fwdOn = g.FindRoute(A, C, 0.0, {"29"}, {});
    TaxiRoute revOn = g.FindRoute(C, A, 0.0, {"29"}, {});
    REQUIRE(fwdOn.valid);
    REQUIRE(revOn.valid);
    CHECK(fwdOn.totalCost == doctest::Approx(fwdOn.totalDistM).epsilon(0.01));
    CHECK(revOn.totalCost == doctest::Approx(revOn.totalDistM * 3.0).epsilon(0.05));
}

TEST_CASE("TaxiGraph flow - wayRef change adds penalty once per transition")
{
    // Single-ref route (no wayRef change):
    TaxiGraph single;
    single.Build(MakeStraightWay(), MakeAirport());

    // Split-ref route (one "M"→"N" transition at B, wayrefChangePenalty = 200):
    TaxiGraph split;
    split.Build(MakeSplitRefWay(), MakeAirport({"M", "N"}));

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute routeSingle = single.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute routeSplit  = split.FindRoute(A, C, 0.0, {}, {});

    REQUIRE(routeSingle.valid);
    REQUIRE(routeSplit.valid);

    // cost - distM isolates flat penalties: the snap offset cancels out of both terms.
    // Single ref: no penalty.
    CHECK(routeSingle.totalCost == doctest::Approx(routeSingle.totalDistM).epsilon(0.01));
    // Split ref: exactly one wayrefChangePenalty (200) above geometric distance.
    CHECK(routeSplit.totalCost - routeSplit.totalDistM ==
          doctest::Approx(200.0).epsilon(0.01));
}

TEST_CASE("TaxiGraph flow - leaving an intersection wayRef carries no penalty")
{
    // Three-segment route: "M" A→B, intersection "ISX" B→C, "M" C→D.
    // Entering ISX from M costs the wayref penalty; leaving ISX to M does not.
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};
    GeoPoint C{48.1100, 16.5454};
    GeoPoint D{48.1100, 16.5481}; // ~200 m east of C

    OsmWay wA;
    wA.id       = 1;
    wA.type     = AerowayType::Taxiway;
    wA.ref      = "M";
    wA.geometry = {A, B};

    OsmWay wB;
    wB.id       = 2;
    wB.type     = AerowayType::Taxiway_Intersection;
    wB.ref      = "ISX";
    wB.geometry = {B, C};

    OsmWay wC;
    wC.id       = 3;
    wC.type     = AerowayType::Taxiway;
    wC.ref      = "M";
    wC.geometry = {C, D};

    OsmAirportData osm;
    osm.ways.push_back(wA);
    osm.ways.push_back(wB);
    osm.ways.push_back(wC);

    TaxiGraph g;
    g.Build(osm, MakeAirport());

    // Single "M" baseline A→D with no wayRef changes.
    OsmWay straight;
    straight.id       = 1;
    straight.type     = AerowayType::Taxiway;
    straight.ref      = "M";
    straight.geometry = {A, D};
    OsmAirportData osmStraight;
    osmStraight.ways.push_back(straight);
    TaxiGraph gStraight;
    gStraight.Build(osmStraight, MakeAirport());

    TaxiRoute routeViaIsx   = g.FindRoute(A, D, 0.0, {}, {});
    TaxiRoute routeStraight = gStraight.FindRoute(A, D, 0.0, {}, {});

    REQUIRE(routeViaIsx.valid);
    REQUIRE(routeStraight.valid);

    // cost - distM = flat penalties + edge-multiplier extras (snap offset cancels).
    // One wayref penalty entering ISX (200), zero leaving ISX, plus the 1.1× multiplier
    // on the 200 m ISX segment adds 200 * 0.1 = 20 above its geometric length.
    // Straight baseline has no penalties or multipliers: cost - distM = 0.
    CHECK(routeStraight.totalCost == doctest::Approx(routeStraight.totalDistM).epsilon(0.01));
    CHECK(routeViaIsx.totalCost - routeViaIsx.totalDistM ==
          doctest::Approx(200.0 + 200.0 * 0.1).epsilon(0.05));
}

TEST_CASE("TaxiGraph flow - Taxiway_Intersection edges cost more than plain taxiway edges")
{
    // Same 400 m geometry; intersection way uses multIntersection (1.1).
    TaxiGraph regular, intersection;
    regular.Build(MakeStraightWay(), MakeAirport());
    intersection.Build(MakeIntersectionWay(), MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute rRegular      = regular.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute rIntersection = intersection.FindRoute(A, C, 0.0, {}, {});

    REQUIRE(rRegular.valid);
    REQUIRE(rIntersection.valid);

    // cost/distM equals the edge type multiplier; snap offset cancels out of the ratio.
    CHECK(rRegular.totalCost == doctest::Approx(rRegular.totalDistM * 1.0).epsilon(0.01));
    CHECK(rIntersection.totalCost == doctest::Approx(rIntersection.totalDistM * 1.1).epsilon(0.05));
}

// ─── NearestNodeId ───────────────────────────────────────────────────────────

TEST_CASE("TaxiGraph::NearestNodeId - finds node within radius")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    // Query right at the start of the way
    int id = g.NearestNodeId({48.1100, 16.5400}, 20.0);
    CHECK(id >= 0);
}

TEST_CASE("TaxiGraph::NearestNodeId - returns -1 beyond radius")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    int id = g.NearestNodeId({48.2000, 17.0000}, 100.0);
    CHECK(id == -1);
}
