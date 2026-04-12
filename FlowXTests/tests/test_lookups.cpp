#include "pch.h"
// test_lookups.cpp
// Unit tests for the pure-logic functions from CFlowX_LookupsTools.
//
// Because CFlowX_LookupsTools inherits from the full EuroScope plugin chain,
// the class methods cannot be called directly from a console test binary.
// The pure implementations are reproduced as free functions here so the
// logic can be exercised without any EuroScope dependency.

#include <doctest/doctest.h>
#include <string>
#include <windows.h>

// ─── Standalone reimplementations ────────────────────────────────────────────

// Mirrors CFlowX_LookupsTools::PointInsidePolygon (winding-number algorithm).
static bool PointInsidePolygon(int polyCorners, double polyX[], double polyY[], double x, double y)
{
    int winding = 0;
    for (int i = 0, j = polyCorners - 1; i < polyCorners; j = i++)
    {
        if (polyY[j] <= y)
        {
            if (polyY[i] > y)
            {
                double cross = (polyX[i] - polyX[j]) * (y - polyY[j])
                             - (x        - polyX[j]) * (polyY[i] - polyY[j]);
                if (cross > 0.0) ++winding;
            }
        }
        else
        {
            if (polyY[i] <= y)
            {
                double cross = (polyX[i] - polyX[j]) * (y - polyY[j])
                             - (x        - polyX[j]) * (polyY[i] - polyY[j]);
                if (cross < 0.0) --winding;
            }
        }
    }
    return winding != 0;
}

// Mirrors CFlowX_LookupsTools::GetAircraftWeightCategoryRanking.
static int GetAircraftWeightCategoryRanking(char wtc)
{
    switch (wtc)
    {
    case 'J': case 'j': return 4;
    case 'H': case 'h': return 3;
    case 'M': case 'm': return 2;
    case 'L': case 'l': return 1;
    default:            return 0;
    }
}

// Mirrors CFlowX_LookupsTools::AppendHoldingPointToFlightStripAnnotation.
static std::string AppendHoldingPointToFlightStripAnnotation(const std::string& annotation, const std::string& hp)
{
    std::string prefix = annotation.substr(0, 7);
    prefix.resize(7, ' ');
    return prefix + hp;
}

// ─── PointInsidePolygon ──────────────────────────────────────────────────────

TEST_CASE("PointInsidePolygon - point inside a square returns true")
{
    // Unit square: (0,0)→(1,0)→(1,1)→(0,1) CCW in X/Y
    double px[] = {0.0, 1.0, 1.0, 0.0};
    double py[] = {0.0, 0.0, 1.0, 1.0};
    CHECK(PointInsidePolygon(4, px, py, 0.5, 0.5) == true);
}

TEST_CASE("PointInsidePolygon - point outside a square returns false")
{
    double px[] = {0.0, 1.0, 1.0, 0.0};
    double py[] = {0.0, 0.0, 1.0, 1.0};
    CHECK(PointInsidePolygon(4, px, py, 2.0, 2.0) == false);
}

TEST_CASE("PointInsidePolygon - point outside a concave polygon in the recess")
{
    // L-shaped polygon (CCW): missing top-right corner
    //  (0,2)──(1,2)
    //    |      |
    //  (0,1)  (1,1)──(2,1)
    //    |              |
    //  (0,0)──────────(2,0)
    double px[] = {0.0, 2.0, 2.0, 1.0, 1.0, 0.0};
    double py[] = {0.0, 0.0, 1.0, 1.0, 2.0, 2.0};
    // Point in the missing corner
    CHECK(PointInsidePolygon(6, px, py, 1.5, 1.5) == false);
    // Point inside the L
    CHECK(PointInsidePolygon(6, px, py, 0.5, 1.5) == true);
}

TEST_CASE("PointInsidePolygon - degenerate 2-vertex polygon does not crash")
{
    double px[] = {0.0, 1.0};
    double py[] = {0.0, 0.0};
    // Should return false for any point (not a polygon)
    CHECK_NOTHROW(PointInsidePolygon(2, px, py, 0.5, 0.0));
    CHECK(PointInsidePolygon(2, px, py, 0.5, 0.0) == false);
}

// ─── GetAircraftWeightCategoryRanking ────────────────────────────────────────

TEST_CASE("GetAircraftWeightCategoryRanking - ordering L < M < H < J")
{
    CHECK(GetAircraftWeightCategoryRanking('L') < GetAircraftWeightCategoryRanking('M'));
    CHECK(GetAircraftWeightCategoryRanking('M') < GetAircraftWeightCategoryRanking('H'));
    CHECK(GetAircraftWeightCategoryRanking('H') < GetAircraftWeightCategoryRanking('J'));
}

TEST_CASE("GetAircraftWeightCategoryRanking - case insensitive")
{
    CHECK(GetAircraftWeightCategoryRanking('j') == GetAircraftWeightCategoryRanking('J'));
    CHECK(GetAircraftWeightCategoryRanking('h') == GetAircraftWeightCategoryRanking('H'));
    CHECK(GetAircraftWeightCategoryRanking('m') == GetAircraftWeightCategoryRanking('M'));
    CHECK(GetAircraftWeightCategoryRanking('l') == GetAircraftWeightCategoryRanking('L'));
}

TEST_CASE("GetAircraftWeightCategoryRanking - unknown character returns 0")
{
    CHECK(GetAircraftWeightCategoryRanking('X') == 0);
    CHECK(GetAircraftWeightCategoryRanking(' ') == 0);
}

// ─── AppendHoldingPointToFlightStripAnnotation ───────────────────────────────

TEST_CASE("AppendHoldingPointToFlightStripAnnotation - empty annotation")
{
    // Empty annotation: prefix padded to 7 spaces, then HP appended
    std::string result = AppendHoldingPointToFlightStripAnnotation("", "M1");
    CHECK(result.size() == 9);        // 7 spaces + "M1"
    CHECK(result.substr(0, 7) == "       ");
    CHECK(result.substr(7) == "M1");
}

TEST_CASE("AppendHoldingPointToFlightStripAnnotation - short annotation is padded")
{
    // [0] = 'Q' (QNH flag), rest empty → prefix "Q" padded to 7 chars
    std::string result = AppendHoldingPointToFlightStripAnnotation("Q", "R29");
    CHECK(result[0] == 'Q');
    CHECK(result.substr(1, 6) == "      ");
    CHECK(result.substr(7) == "R29");
}

TEST_CASE("AppendHoldingPointToFlightStripAnnotation - existing data in positions 0-6 preserved")
{
    // Full prefix: QNH flag + 6-char freq (no dot)
    std::string annotation = "Q121750";  // 7 chars
    std::string result     = AppendHoldingPointToFlightStripAnnotation(annotation, "C5");
    CHECK(result.substr(0, 7) == "Q121750");
    CHECK(result.substr(7) == "C5");
}

TEST_CASE("AppendHoldingPointToFlightStripAnnotation - old HP is replaced")
{
    // Annotation already has a previous HP beyond position 7
    std::string annotation = "Q121750M1";
    std::string result     = AppendHoldingPointToFlightStripAnnotation(annotation, "R29");
    CHECK(result.substr(0, 7) == "Q121750");
    CHECK(result.substr(7) == "R29"); // M1 discarded
}
