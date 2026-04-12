#include "pch.h"
// test_osm.cpp
// Unit tests for osm_taxiways.cpp: WayLengthM.

#include <doctest/doctest.h>
#include "osm_taxiways.h"
#include "taxi_graph.h" // for HaversineM

// ─── WayLengthM ──────────────────────────────────────────────────────────────

TEST_CASE("WayLengthM - empty way returns 0")
{
    OsmWay w;
    w.id   = 1;
    w.type = AerowayType::Taxiway;
    CHECK(WayLengthM(w) == doctest::Approx(0.0));
}

TEST_CASE("WayLengthM - single node returns 0")
{
    OsmWay w;
    w.geometry = {{48.110, 16.540}};
    CHECK(WayLengthM(w) == doctest::Approx(0.0));
}

TEST_CASE("WayLengthM - two nodes matches HaversineM")
{
    GeoPoint A{48.110, 16.540};
    GeoPoint B{48.120, 16.540};

    OsmWay w;
    w.geometry = {A, B};

    double expected = HaversineM(A, B);
    CHECK(WayLengthM(w) == doctest::Approx(expected).epsilon(0.001));
}

TEST_CASE("WayLengthM - three nodes sums both segments")
{
    GeoPoint A{48.110, 16.540};
    GeoPoint B{48.120, 16.540};
    GeoPoint C{48.120, 16.560};

    OsmWay w;
    w.geometry = {A, B, C};

    double expected = HaversineM(A, B) + HaversineM(B, C);
    CHECK(WayLengthM(w) == doctest::Approx(expected).epsilon(0.001));
}

TEST_CASE("WayLengthM - known ~200 m east-west segment")
{
    // At lat 48.11, 1° lon ≈ 74 000 m → 0.0027° ≈ 200 m
    GeoPoint A{48.1100, 16.5400};
    GeoPoint B{48.1100, 16.5427};

    OsmWay w;
    w.geometry = {A, B};

    CHECK(WayLengthM(w) == doctest::Approx(200.0).epsilon(0.05)); // within 5 %
}
