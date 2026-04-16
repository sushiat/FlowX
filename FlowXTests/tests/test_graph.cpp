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

// Build a minimal airport config.
static airport MakeAirport()
{
    airport ap;
    ap.icao = "TEST";
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

    airport ap         = MakeAirport();
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

    // totalCost / totalDistM approximates the flow multiplier; the wayrefChangePenalty
    // at route start (empty → named transition) adds a fixed offset to both routes.
    const double wrp = ap.taxiNetworkConfig.routing.wayrefChangePenalty;
    CHECK(withFlow.totalCost == doctest::Approx(withFlow.totalDistM * ap.taxiNetworkConfig.flowRules.withFlowMult + wrp).epsilon(0.05));
    CHECK(againstFlow.totalCost == doctest::Approx(againstFlow.totalDistM * ap.taxiNetworkConfig.flowRules.againstFlowMult + wrp).epsilon(0.05));
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
    // Without flow rules every edge cost equals its geometric length (1.0×)
    // plus the wayrefChangePenalty at route start (empty → named transition).
    const double wrp = MakeAirport().taxiNetworkConfig.routing.wayrefChangePenalty;
    CHECK(fwd.totalCost == doctest::Approx(fwd.totalDistM + wrp).epsilon(0.01));
    CHECK(rev.totalCost == doctest::Approx(rev.totalDistM + wrp).epsilon(0.01));
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

    const double wrp = ap.taxiNetworkConfig.routing.wayrefChangePenalty;

    // Without the runway config active: cost/distM = 1.0 for both directions (+ start wayref penalty).
    TaxiRoute fwdOff = g.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute revOff = g.FindRoute(C, A, 0.0, {}, {});
    REQUIRE(fwdOff.valid);
    REQUIRE(revOff.valid);
    CHECK(fwdOff.totalCost == doctest::Approx(fwdOff.totalDistM + wrp).epsilon(0.01));
    CHECK(revOff.totalCost == doctest::Approx(revOff.totalDistM + wrp).epsilon(0.01));

    // With dep runway "29" active: A→C (with flow) gets 0.9× discount; C→A (against flow) rises to 3.0×.
    TaxiRoute fwdOn = g.FindRoute(A, C, 0.0, {"29"}, {});
    TaxiRoute revOn = g.FindRoute(C, A, 0.0, {"29"}, {});
    REQUIRE(fwdOn.valid);
    REQUIRE(revOn.valid);
    CHECK(fwdOn.totalCost == doctest::Approx(fwdOn.totalDistM * ap.taxiNetworkConfig.flowRules.withFlowMult + wrp).epsilon(0.05));
    CHECK(revOn.totalCost == doctest::Approx(revOn.totalDistM * ap.taxiNetworkConfig.flowRules.againstFlowMult + wrp).epsilon(0.05));
}

TEST_CASE("TaxiGraph flow - wayRef change adds penalty once per transition")
{
    // Single-ref route (no wayRef change):
    TaxiGraph single;
    single.Build(MakeStraightWay(), MakeAirport());

    // Split-ref route (one "M"→"N" transition at B, wayrefChangePenalty = 200):
    TaxiGraph split;
    split.Build(MakeSplitRefWay(), MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute routeSingle = single.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute routeSplit  = split.FindRoute(A, C, 0.0, {}, {});

    REQUIRE(routeSingle.valid);
    REQUIRE(routeSplit.valid);

    const double wrp = MakeAirport().taxiNetworkConfig.routing.wayrefChangePenalty;

    // cost - distM isolates flat penalties: the snap offset cancels out of both terms.
    // Single ref: one penalty at route start (empty → named).
    CHECK(routeSingle.totalCost == doctest::Approx(routeSingle.totalDistM + wrp).epsilon(0.01));
    // Split ref: start penalty (empty → "M") + one wayrefChangePenalty ("M" → "N") = 2 × wrp.
    CHECK(routeSplit.totalCost - routeSplit.totalDistM ==
          doctest::Approx(wrp * 2).epsilon(0.01));
}

TEST_CASE("TaxiGraph flow - only intersection-to-intersection wayRef transitions are free")
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

    const double wrp = MakeAirport().taxiNetworkConfig.routing.wayrefChangePenalty;

    // cost - distM = flat penalties + edge-multiplier extras (snap offset cancels).
    // Start penalty (empty → "M") + wayref penalty entering ISX from M + wayref
    // penalty leaving ISX to M, plus the 1.1× multiplier on the ISX segment.
    // Only intersection→intersection transitions are free; ISX→taxiway is not.
    // Straight baseline: only the start penalty (empty → "M").
    CHECK(routeStraight.totalCost == doctest::Approx(routeStraight.totalDistM + wrp).epsilon(0.01));
    CHECK(routeViaIsx.totalCost - routeViaIsx.totalDistM ==
          doctest::Approx(wrp * 3 + 200.0 * 0.1).epsilon(0.05));
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

    // cost/distM equals the edge type multiplier + start wayref penalty; snap offset cancels out of the ratio.
    const double wrp = MakeAirport().taxiNetworkConfig.routing.wayrefChangePenalty;
    CHECK(rRegular.totalCost == doctest::Approx(rRegular.totalDistM * 1.0 + wrp).epsilon(0.01));
    CHECK(rIntersection.totalCost == doctest::Approx(rIntersection.totalDistM * 1.1 + wrp).epsilon(0.05));
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

// ─── Additional helpers ───────────────────────────────────────────────────────

// A→B typed Taxilane (~200 m, ref "TL1").
static OsmAirportData MakeTaxilaneWay()
{
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};

    OsmWay way;
    way.id       = 1;
    way.type     = AerowayType::Taxilane;
    way.ref      = "TL1";
    way.geometry = {A, B};

    OsmAirportData osm;
    osm.ways.push_back(way);
    return osm;
}

// Two separate short ways with no shared nodes and coordinates far apart
// (way 1: A→B east;  way 2: P→Q east at lat 48.20).
static OsmAirportData MakeDisconnectedWays()
{
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};
    GeoPoint P{48.2000, 16.5400};
    GeoPoint Q{48.2000, 16.5427};

    OsmWay way1;
    way1.id       = 1;
    way1.type     = AerowayType::Taxiway;
    way1.ref      = "M";
    way1.geometry = {A, B};

    OsmWay way2;
    way2.id       = 2;
    way2.type     = AerowayType::Taxiway;
    way2.ref      = "M";
    way2.geometry = {P, Q};

    OsmAirportData osm;
    osm.ways.push_back(way1);
    osm.ways.push_back(way2);
    return osm;
}

// Fork: A is the junction.  East branch A→B is a dead end.  North branch A→C
// leads to the destination.  Blocking B does not disconnect C.
static OsmAirportData MakeForkWays()
{
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427}; // east dead end
    GeoPoint C{48.1118, 16.5400}; // north destination (~200 m north of A)

    OsmWay eastWay;
    eastWay.id       = 1;
    eastWay.type     = AerowayType::Taxiway;
    eastWay.ref      = "E";
    eastWay.geometry = {A, B};

    OsmWay northWay;
    northWay.id       = 2;
    northWay.type     = AerowayType::Taxiway;
    northWay.ref      = "N";
    northWay.geometry = {A, C};

    OsmAirportData osm;
    osm.ways.push_back(eastWay);
    osm.ways.push_back(northWay);
    return osm;
}

// ─── Start-node turn limit ────────────────────────────────────────────────────

TEST_CASE("TaxiGraph - 90 degree turn at start node is allowed (135 degree start limit)")
{
    // L-shaped graph: A–B–C (east), B–D (north).
    // Start exactly at B with initialBearingDeg=70° (ENE).  The route must turn
    // ~70° to reach D (north, ~0°).  70° < 135° (start-node limit) → valid.
    //
    // Contrast: the existing "L-shaped: 90 degree branch is hard-blocked" test
    // routes A→D where the 90° turn occurs mid-route at B (uses hardTurnDeg 50°)
    // and is therefore blocked.
    TaxiGraph g;
    g.Build(MakeLShapedWay(), MakeAirport());

    GeoPoint B{48.1100, 16.5427};
    GeoPoint D{48.1118, 16.5427};

    TaxiRoute route = g.FindRoute(B, D, 0.0, {}, {}, 70.0);
    CHECK(route.valid);
}

// ─── Taxilane multiplier ──────────────────────────────────────────────────────

TEST_CASE("TaxiGraph - Taxilane edges cost 3x base distance (multTaxilane)")
{
    // Plain Taxiway baseline vs. Taxilane way of identical 200 m geometry.
    // multTaxilane default = 3.0; cost/distM ratio should reflect the multiplier.
    TaxiGraph taxiway, taxilane;

    OsmWay plain;
    plain.id       = 1;
    plain.type     = AerowayType::Taxiway;
    plain.ref      = "TL1";
    plain.geometry = {{48.1100, 16.5400}, {48.1100, 16.5427}};
    OsmAirportData osmPlain;
    osmPlain.ways.push_back(plain);
    taxiway.Build(osmPlain, MakeAirport());

    taxilane.Build(MakeTaxilaneWay(), MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};

    TaxiRoute rTaxiway  = taxiway.FindRoute(A, B, 0.0, {}, {});
    TaxiRoute rTaxilane = taxilane.FindRoute(A, B, 0.0, {}, {});

    REQUIRE(rTaxiway.valid);
    REQUIRE(rTaxilane.valid);

    // Taxiway: cost/distM = 1.0;  Taxilane: cost/distM ≈ 3.0 (both + start wayref penalty)
    const double wrp = MakeAirport().taxiNetworkConfig.routing.wayrefChangePenalty;
    CHECK(rTaxiway.totalCost == doctest::Approx(rTaxiway.totalDistM * 1.0 + wrp).epsilon(0.05));
    CHECK(rTaxilane.totalCost == doctest::Approx(rTaxilane.totalDistM * 3.0 + wrp).epsilon(0.10));
}

// ─── Holding point node promotion ────────────────────────────────────────────

TEST_CASE("TaxiGraph - OSM holding position snapped to way node does not incur approach penalty")
{
    // Baseline: straight way A–B–C with no holding positions.
    TaxiGraph baseline;
    baseline.Build(MakeStraightWay(), MakeAirport());

    // With HP: same way but an OsmHoldingPosition placed exactly at B.
    // B is a way node → within the 25 m snap radius → B is promoted to
    // HoldingPoint type, but its wayRef remains "M" (a plain taxiway).
    // The approach penalty (step 8) only fires on Taxiway_HoldingPoint wayRefs
    // (e.g. "A1", "B4"), so stop bars on plain taxiways are NOT penalised.
    OsmAirportData     osmWithHp = MakeStraightWay();
    OsmHoldingPosition osmHp;
    osmHp.id  = 100;
    osmHp.ref = "M1";
    osmHp.pos = GeoPoint{48.1100, 16.5427}; // exactly at B
    osmWithHp.holdingPositions.push_back(osmHp);

    TaxiGraph withHp;
    withHp.Build(osmWithHp, MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute rBaseline = baseline.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute rWithHp   = withHp.FindRoute(A, C, 0.0, {}, {});

    REQUIRE(rBaseline.valid);
    REQUIRE(rWithHp.valid);

    // Cost must be equal: snapping an OSM stop bar to a plain taxiway node adds
    // no routing penalty.
    CHECK(rWithHp.totalCost == doctest::Approx(rBaseline.totalCost).epsilon(0.05));
}

TEST_CASE("TaxiGraph - HP node beyond snap radius does not incur approach penalty")
{
    // OSM HP placed ~30 m north of B (> osmHoldingPositionSnapM = 25 m).
    // No snap → no cost penalty; route cost equals the baseline.
    // 30 m ≈ 30 / 111 195 ≈ 0.000270° of latitude.
    OsmAirportData     osmFarHp = MakeStraightWay();
    OsmHoldingPosition farHp;
    farHp.id  = 101;
    farHp.ref = "FAR";
    farHp.pos = GeoPoint{48.1103, 16.5427}; // ~30 m north of B, outside 25 m snap
    osmFarHp.holdingPositions.push_back(farHp);

    TaxiGraph baseline, withFarHp;
    baseline.Build(MakeStraightWay(), MakeAirport());
    withFarHp.Build(osmFarHp, MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute rBaseline = baseline.FindRoute(A, C, 0.0, {}, {});
    TaxiRoute rFarHp    = withFarHp.FindRoute(A, C, 0.0, {}, {});

    REQUIRE(rBaseline.valid);
    REQUIRE(rFarHp.valid);

    // Costs must be identical — the far HP changes nothing.
    CHECK(rFarHp.totalCost == doctest::Approx(rBaseline.totalCost).epsilon(0.01));
}

TEST_CASE("TaxiGraph - route to a promoted HoldingPoint node succeeds")
{
    // OSM HP at B promotes B to a HoldingPoint node.  Routing A→B must still
    // return a valid route (approach cost is high but the route exists).
    OsmAirportData     osmWithHp = MakeStraightWay();
    OsmHoldingPosition osmHp;
    osmHp.id  = 100;
    osmHp.ref = "M1";
    osmHp.pos = GeoPoint{48.1100, 16.5427}; // exactly at B
    osmWithHp.holdingPositions.push_back(osmHp);

    TaxiGraph g;
    g.Build(osmWithHp, MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};

    TaxiRoute route = g.FindRoute(A, B, 0.0, {}, {});
    REQUIRE(route.valid);
    CHECK(route.totalDistM == doctest::Approx(200.0).epsilon(0.25));
}

TEST_CASE("TaxiGraph - edge-split HP node placed at accurate position")
{
    // Place an HP centroid midway between two subdivision nodes (not on any
    // existing waypoint).  The edge-splitting logic must inject a new node at the
    // projected position within 1 m accuracy, and routing through it must work.
    // At lat 48.11, 1° lon ≈ 74 000 m.  Midpoint between A and B is at
    // lon = (16.5400 + 16.5427) / 2 = 16.54135, ~100 m from A.
    OsmAirportData     osmWithHp = MakeStraightWay();
    OsmHoldingPosition osmHp;
    osmHp.id  = 100;
    osmHp.ref = "MID";
    osmHp.pos = GeoPoint{48.1100, 16.54135}; // ~100 m from A, between subdivision nodes
    osmWithHp.holdingPositions.push_back(osmHp);

    TaxiGraph g;
    g.Build(osmWithHp, MakeAirport());

    // Verify routing from A through the HP position works.
    GeoPoint A{48.1100, 16.5400};
    GeoPoint hpPos{48.1100, 16.54135};

    TaxiRoute route = g.FindRoute(A, hpPos, 0.0, {}, {});
    REQUIRE(route.valid);
    CHECK(route.totalDistM == doctest::Approx(100.0).epsilon(0.15));

    // Verify the HP node exists in the graph within 1 m of the target position.
    GeoPoint resolved = g.HoldingPointByLabel("MID");
    CHECK(HaversineM(resolved, hpPos) < 1.0);
}

// ─── Start == goal ────────────────────────────────────────────────────────────

TEST_CASE("TaxiGraph - start equals goal returns valid zero-distance route")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint  A{48.1100, 16.5400};
    TaxiRoute route = g.FindRoute(A, A, 0.0, {}, {});

    CHECK(route.valid);
    CHECK(route.totalDistM == doctest::Approx(0.0).epsilon(1.0));
}

// ─── Junction wayRef priority ─────────────────────────────────────────────────

TEST_CASE("TaxiGraph - taxiway ref wins over intersection ref at shared junction node")
{
    // Taxiway "L":            A ──── B ──── C   (B is the shared junction node)
    // Intersection "Exit 14":        B ──── D
    // Node B is shared (d = 0 m).  Taxiway priority (3) > intersection priority (1),
    // so routing A→C should produce wayRefs containing only "L", not "Exit 14".
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};
    GeoPoint C{48.1100, 16.5454};
    GeoPoint D{48.1118, 16.5427}; // ~200 m north of B

    OsmWay taxiway;
    taxiway.id       = 1;
    taxiway.type     = AerowayType::Taxiway;
    taxiway.ref      = "L";
    taxiway.geometry = {A, B, C};

    OsmWay intersection;
    intersection.id       = 2;
    intersection.type     = AerowayType::Taxiway_Intersection;
    intersection.ref      = "Exit 14";
    intersection.geometry = {B, D};

    OsmAirportData osm;
    osm.ways.push_back(taxiway);
    osm.ways.push_back(intersection);

    TaxiGraph g;
    g.Build(osm, MakeAirport());

    GeoPoint from{48.1100, 16.5400};
    GeoPoint to{48.1100, 16.5454};

    TaxiRoute route = g.FindRoute(from, to, 0.0, {}, {});
    REQUIRE(route.valid);

    // Junction node B must carry the taxiway ref, not the intersection ref.
    const auto& refs = route.wayRefs;
    CHECK(std::find(refs.begin(), refs.end(), "L") != refs.end());
    CHECK(std::find(refs.begin(), refs.end(), "Exit 14") == refs.end());
}

TEST_CASE("TaxiGraph - taxiway ref wins over taxilane ref at shared junction node")
{
    // Taxiway "M":   A ──── B ──── C   (B is the shared junction node)
    // Taxilane "TL": D ──── B
    // Taxiway priority (3) > taxilane priority (2), so the junction node B
    // gets labelled "M" and routing A→C produces only "M" in wayRefs.
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};
    GeoPoint C{48.1100, 16.5454};
    GeoPoint D{48.1118, 16.5427}; // ~200 m north of B

    OsmWay taxiway;
    taxiway.id       = 1;
    taxiway.type     = AerowayType::Taxiway;
    taxiway.ref      = "M";
    taxiway.geometry = {A, B, C};

    OsmWay taxilane;
    taxilane.id       = 2;
    taxilane.type     = AerowayType::Taxilane;
    taxilane.ref      = "TL";
    taxilane.geometry = {D, B};

    OsmAirportData osm;
    osm.ways.push_back(taxiway);
    osm.ways.push_back(taxilane);

    TaxiGraph g;
    g.Build(osm, MakeAirport());

    GeoPoint from{48.1100, 16.5400};
    GeoPoint to{48.1100, 16.5454};

    TaxiRoute route = g.FindRoute(from, to, 0.0, {}, {});
    REQUIRE(route.valid);

    // Junction node B must carry the taxiway ref, not the taxilane ref.
    const auto& refs = route.wayRefs;
    CHECK(std::find(refs.begin(), refs.end(), "M") != refs.end());
    CHECK(std::find(refs.begin(), refs.end(), "TL") == refs.end());
}

TEST_CASE("TaxiGraph - parent taxiway ref wins over lane-variant at shared junction node")
{
    // Taxiway "TL 40":             A ──── B ──── C
    // Taxiway "TL 40 'Blue Line'": D ──── B
    // Same priority (both Taxiway), but "TL 40" is a prefix of the variant name,
    // so B gets the parent ref "TL 40" regardless of OSM processing order.
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};
    GeoPoint C{48.1100, 16.5454};
    GeoPoint D{48.1118, 16.5427}; // ~200 m north of B

    OsmWay parent;
    parent.id       = 1;
    parent.type     = AerowayType::Taxiway;
    parent.ref      = "TL 40";
    parent.geometry = {A, B, C};

    OsmWay variant;
    variant.id       = 2;
    variant.type     = AerowayType::Taxiway;
    variant.ref      = "TL 40 'Blue Line'";
    variant.geometry = {D, B};

    OsmAirportData osm;
    osm.ways.push_back(parent);
    osm.ways.push_back(variant);

    TaxiGraph g;
    g.Build(osm, MakeAirport());

    GeoPoint from{48.1100, 16.5400};
    GeoPoint to{48.1100, 16.5454};

    TaxiRoute route = g.FindRoute(from, to, 0.0, {}, {});
    REQUIRE(route.valid);

    const auto& refs = route.wayRefs;
    CHECK(std::find(refs.begin(), refs.end(), "TL 40") != refs.end());
    CHECK(std::find(refs.begin(), refs.end(), "TL 40 'Blue Line'") == refs.end());
}

// ─── Disconnected components ──────────────────────────────────────────────────

TEST_CASE("TaxiGraph - route between disconnected components returns invalid")
{
    // Both components have valid nodes; the goal node exists in the graph but
    // is unreachable from the start node.
    TaxiGraph g;
    g.Build(MakeDisconnectedWays(), MakeAirport());

    GeoPoint A{48.1100, 16.5400}; // component 1
    GeoPoint Q{48.2000, 16.5427}; // component 2 — exists in graph, unreachable

    TaxiRoute route = g.FindRoute(A, Q, 0.0, {}, {});
    CHECK_FALSE(route.valid);
}

// ─── FindWaypointRoute ────────────────────────────────────────────────────────

TEST_CASE("TaxiGraph::FindWaypointRoute - routes through forced intermediate waypoint")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};
    GeoPoint C{48.1100, 16.5454};

    TaxiRoute route = g.FindWaypointRoute(A, {B}, C, 0.0, {}, {});
    REQUIRE(route.valid);
    // Total distance through the forced waypoint equals the direct distance (same path).
    CHECK(route.totalDistM == doctest::Approx(400.0).epsilon(0.25));
}

TEST_CASE("TaxiGraph::FindWaypointRoute - unreachable waypoint makes route invalid")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint A{48.1100, 16.5400};
    GeoPoint C{48.1100, 16.5454};
    GeoPoint unreachable{48.5000, 17.0000}; // nowhere near the graph

    TaxiRoute route = g.FindWaypointRoute(A, {unreachable}, C, 0.0, {}, {});
    CHECK_FALSE(route.valid);
}

// ─── WalkGraph ────────────────────────────────────────────────────────────────

TEST_CASE("TaxiGraph::WalkGraph - stops at distance limit")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    // Walk east from A for 150 m — the way is 400 m long, so the limit triggers first.
    TaxiRoute walk = g.WalkGraph({48.1100, 16.5400}, 90.0, 150.0);
    CHECK(walk.totalDistM == doctest::Approx(150.0).epsilon(0.15));
}

TEST_CASE("TaxiGraph::WalkGraph - stops at dead end before reaching distance limit")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    // Walk east from A with a 600 m budget.  The way ends at C (~400 m); the
    // walk must stop there, not crash or return 600 m.
    TaxiRoute walk = g.WalkGraph({48.1100, 16.5400}, 90.0, 600.0);
    CHECK(walk.totalDistM == doctest::Approx(400.0).epsilon(0.20));
}

TEST_CASE("TaxiGraph::WalkGraph - no forward edges produces degenerate route")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    // Start at C (east terminus) and walk east.  No edges leave C eastward.
    TaxiRoute walk = g.WalkGraph({48.1100, 16.5454}, 90.0, 300.0);
    CHECK(walk.totalDistM < 1.0);
}

// ─── DeadEndEdges ─────────────────────────────────────────────────────────────

TEST_CASE("TaxiGraph::DeadEndEdges - no blocked nodes returns empty")
{
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint C{48.1100, 16.5454};
    auto     result = g.DeadEndEdges(C, {});
    CHECK(result.empty());
}

TEST_CASE("TaxiGraph::DeadEndEdges - bridge node blocked exposes unreachable sub-graph")
{
    // Straight A–B–C.  Blocking B cuts C off from A.
    // DeadEndEdges(C, {B}) must return a non-empty set of edges (the C side).
    TaxiGraph g;
    g.Build(MakeStraightWay(), MakeAirport());

    GeoPoint B{48.1100, 16.5427};
    GeoPoint C{48.1100, 16.5454};

    int bId = g.NearestNodeId(B, 5.0);
    REQUIRE(bId >= 0);

    auto result = g.DeadEndEdges(C, {bId});
    CHECK_FALSE(result.empty());
}

TEST_CASE("TaxiGraph::DeadEndEdges - non-bridge dead-end blocked leaves dest reachable")
{
    // Fork graph: A is the junction.  East branch A→B is a dead end; north
    // branch A→C leads to the destination.  Blocking B does not disconnect C.
    TaxiGraph g;
    g.Build(MakeForkWays(), MakeAirport());

    GeoPoint B{48.1100, 16.5427}; // east dead end
    GeoPoint C{48.1118, 16.5400}; // north destination

    int bId = g.NearestNodeId(B, 5.0);
    REQUIRE(bId >= 0);

    auto result = g.DeadEndEdges(C, {bId});
    CHECK(result.empty()); // C still reachable via A→C; no dead-end sub-graph exposed
}
