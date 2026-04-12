#include "pch.h"
// test_geometry.cpp
// Unit tests for the inline geometry helpers declared in taxi_graph.h:
//   HaversineM, BearingDeg, BearingDiff, PointToSegmentDistM, SegmentIntersectGeo
//
// This file also owns the doctest main() entry point.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "taxi_graph.h"

// ─── HaversineM ──────────────────────────────────────────────────────────────

TEST_CASE("HaversineM - same point is zero")
{
    GeoPoint p{48.110, 16.570};
    CHECK(HaversineM(p, p) == doctest::Approx(0.0));
}

TEST_CASE("HaversineM - known E-W distance at LOWW (≈ 2900 m)")
{
    // Two points along the same latitude ~2.9 km apart (RWY 11/29 approximate ends)
    GeoPoint west{48.1103, 16.533};
    GeoPoint east{48.1103, 16.575};
    double d = HaversineM(west, east);
    CHECK(d == doctest::Approx(3137.0).epsilon(0.01)); // within 1 %
}

TEST_CASE("HaversineM - known N-S distance (~1100 m)")
{
    GeoPoint north{48.120, 16.557};
    GeoPoint south{48.110, 16.557};
    double d = HaversineM(north, south);
    CHECK(d == doctest::Approx(1112.0).epsilon(0.01));
}

TEST_CASE("HaversineM - symmetry")
{
    GeoPoint a{48.115, 16.540};
    GeoPoint b{48.122, 16.568};
    CHECK(HaversineM(a, b) == doctest::Approx(HaversineM(b, a)));
}

// ─── BearingDeg ──────────────────────────────────────────────────────────────

TEST_CASE("BearingDeg - due North")
{
    GeoPoint from{48.100, 16.557};
    GeoPoint to  {48.200, 16.557};
    CHECK(BearingDeg(from, to) == doctest::Approx(0.0).epsilon(0.001));
}

TEST_CASE("BearingDeg - due South")
{
    GeoPoint from{48.200, 16.557};
    GeoPoint to  {48.100, 16.557};
    CHECK(BearingDeg(from, to) == doctest::Approx(180.0).epsilon(0.001));
}

TEST_CASE("BearingDeg - due East")
{
    GeoPoint from{48.110, 16.500};
    GeoPoint to  {48.110, 16.600};
    // At this latitude East bearing is very close to 90°
    CHECK(BearingDeg(from, to) == doctest::Approx(90.0).epsilon(0.1));
}

TEST_CASE("BearingDeg - due West")
{
    GeoPoint from{48.110, 16.600};
    GeoPoint to  {48.110, 16.500};
    CHECK(BearingDeg(from, to) == doctest::Approx(270.0).epsilon(0.1));
}

TEST_CASE("BearingDeg - NE diagonal is near 45°")
{
    GeoPoint from{48.100, 16.540};
    GeoPoint to  {48.110, 16.557}; // roughly equal delta-lat and delta-lon
    double b = BearingDeg(from, to);
    CHECK(b > 30.0);
    CHECK(b < 60.0);
}

// ─── BearingDiff ─────────────────────────────────────────────────────────────

TEST_CASE("BearingDiff - same bearing is 0")
{
    CHECK(BearingDiff(90.0, 90.0) == doctest::Approx(0.0));
}

TEST_CASE("BearingDiff - opposite bearings is 180")
{
    CHECK(BearingDiff(0.0, 180.0)   == doctest::Approx(180.0));
    CHECK(BearingDiff(45.0, 225.0)  == doctest::Approx(180.0));
}

TEST_CASE("BearingDiff - wrap-around: 350° to 10° is 20°")
{
    CHECK(BearingDiff(350.0, 10.0) == doctest::Approx(20.0));
    CHECK(BearingDiff(10.0, 350.0) == doctest::Approx(20.0));
}

TEST_CASE("BearingDiff - symmetry")
{
    CHECK(BearingDiff(30.0, 270.0) == doctest::Approx(BearingDiff(270.0, 30.0)));
    CHECK(BearingDiff(5.0,  355.0) == doctest::Approx(BearingDiff(355.0, 5.0)));
}

TEST_CASE("BearingDiff - result always in [0, 180]")
{
    for (double a = 0; a < 360; a += 37.3)
        for (double b = 0; b < 360; b += 41.7)
        {
            double d = BearingDiff(a, b);
            CHECK(d >= 0.0);
            CHECK(d <= 180.0);
        }
}

// ─── PointToSegmentDistM ─────────────────────────────────────────────────────

TEST_CASE("PointToSegmentDistM - foot projects inside segment")
{
    // Horizontal segment due East; point is 100 m north of the midpoint.
    // midpoint ≈ 48.110, 16.557; point straight north of it
    GeoPoint A{48.110, 16.540};
    GeoPoint B{48.110, 16.574};
    GeoPoint P{48.111, 16.557}; // ~111 m north of segment
    double d = PointToSegmentDistM(P, A, B);
    CHECK(d == doctest::Approx(111.2).epsilon(0.02));
}

TEST_CASE("PointToSegmentDistM - foot beyond end clamps to endpoint")
{
    GeoPoint A{48.110, 16.540};
    GeoPoint B{48.110, 16.574};
    GeoPoint P{48.110, 16.500}; // west of A
    double d    = PointToSegmentDistM(P, A, B);
    double dToA = HaversineM(P, A);
    CHECK(d == doctest::Approx(dToA).epsilon(0.001));
}

TEST_CASE("PointToSegmentDistM - point on segment is (near) zero")
{
    GeoPoint A{48.110, 16.540};
    GeoPoint B{48.120, 16.540};
    GeoPoint P{48.115, 16.540}; // midpoint
    CHECK(PointToSegmentDistM(P, A, B) == doctest::Approx(0.0).epsilon(0.5));
}

// ─── SegmentIntersectGeo ─────────────────────────────────────────────────────

TEST_CASE("SegmentIntersectGeo - crossing segments return true with correct intersection point")
{
    // Two segments forming a cross; intersection should be near the midpoint of each
    GeoPoint A{48.110, 16.550};
    GeoPoint B{48.120, 16.550};
    GeoPoint C{48.115, 16.545};
    GeoPoint D{48.115, 16.555};
    GeoPoint pt{};
    CHECK(SegmentIntersectGeo(A, B, C, D, pt) == true);
    // Intersection point should be close to (48.115, 16.550)
    CHECK(pt.lat == doctest::Approx(48.115).epsilon(0.001));
    CHECK(pt.lon == doctest::Approx(16.550).epsilon(0.001));
}

TEST_CASE("SegmentIntersectGeo - parallel segments return false")
{
    GeoPoint A{48.110, 16.540};
    GeoPoint B{48.120, 16.540};
    GeoPoint C{48.110, 16.550};
    GeoPoint D{48.120, 16.550};
    GeoPoint pt{};
    CHECK(SegmentIntersectGeo(A, B, C, D, pt) == false);
}

TEST_CASE("SegmentIntersectGeo - non-overlapping collinear segments return false")
{
    GeoPoint A{48.110, 16.540};
    GeoPoint B{48.112, 16.540};
    GeoPoint C{48.118, 16.540};
    GeoPoint D{48.120, 16.540};
    GeoPoint pt{};
    CHECK(SegmentIntersectGeo(A, B, C, D, pt) == false);
}
