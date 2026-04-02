#include "pch.h"
#include "CDelHelX_LookupsTools.h"

/// @brief Tests whether point (x, y) lies inside the given polygon using ray casting.
/// @param polyCorners Number of polygon vertices.
/// @param polyX Array of X (longitude) coordinates.
/// @param polyY Array of Y (latitude) coordinates.
/// @param x X coordinate of the test point.
/// @param y Y coordinate of the test point.
/// @return True if the point is inside the polygon.
bool CDelHelX_LookupsTools::PointInsidePolygon(int polyCorners, double polyX[], double polyY[], double x, double y) {
    int   i, j = polyCorners - 1;
    bool  oddNodes = false;

    for (i = 0; i < polyCorners; i++) {
        if (polyY[i] < y && polyY[j] >= y
            || polyY[j] < y && polyY[i] >= y) {
            if (polyX[i] + (y - polyY[i]) / (polyY[j] - polyY[i]) * (polyX[j] - polyX[i]) < x) {
                oddNodes = !oddNodes;
            }
        }
        j = i;
    }

    return oddNodes;
}

/// @brief Returns the distance in NM from a runway threshold to the given position.
/// @param rwy Runway designator.
/// @param currentPosition Aircraft or point position.
/// @param runways Runway map for the airport.
/// @return Distance in NM, or 0.0 if the runway is not found.
double CDelHelX_LookupsTools::DistanceFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition, const std::map<std::string, runway>& runways)
{
    auto rwyIt = runways.find(rwy);
    if (rwyIt == runways.end())
    {
        return 0.0;
    }

    EuroScopePlugIn::CPosition rwyThreshold;
    rwyThreshold.m_Latitude = rwyIt->second.thresholdLat;
    rwyThreshold.m_Longitude = rwyIt->second.thresholdLon;

    return currentPosition.DistanceTo(rwyThreshold);
}

/// @brief Returns the bearing in degrees from a runway threshold to the given position.
/// @param rwy Runway designator.
/// @param currentPosition Aircraft or point position.
/// @param runways Runway map for the airport.
/// @return Bearing in degrees (0–360), or -1 if the runway is not found.
double CDelHelX_LookupsTools::DirectionFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition, const std::map<std::string, runway>& runways)
{
    auto rwyIt = runways.find(rwy);
    if (rwyIt == runways.end())
    {
        return -1;
    }

    EuroScopePlugIn::CPosition rwyThreshold;
    rwyThreshold.m_Latitude = rwyIt->second.thresholdLat;
    rwyThreshold.m_Longitude = rwyIt->second.thresholdLon;

    return rwyThreshold.DirectionTo(currentPosition);
}

/// @brief Returns a numeric ranking for a wake-turbulence category character.
/// @param wtc Wake-turbulence category (J, H, M, L; case-insensitive).
/// @return 4 for J, 3 for H, 2 for M, 1 for L, 0 for unknown.
int CDelHelX_LookupsTools::GetAircraftWeightCategoryRanking(char wtc)
{
    switch (wtc)
    {
    case 'J':
    case 'j':
        return 4;
    case 'H':
    case 'h':
        return 3;
    case 'M':
    case 'm':
        return 2;
    case 'L':
    case 'l':
        return 1;
    default:
        return 0;
    }
}

/// @brief Checks whether two holding-point names refer to the same physical point.
/// @param hp1 First holding-point name.
/// @param hp2 Second holding-point name.
/// @param runways Runway map for the airport.
/// @return Non-zero if the names are equal or linked via a sameAs relationship.
int CDelHelX_LookupsTools::IsSameHoldingPoint(std::string hp1, std::string hp2, const std::map<std::string, runway>& runways)
{
    if (hp1.empty() || hp2.empty())
    {
        return false;
    }

    if (hp1 == hp2)
    {
        return true;
    }

    for (auto& [rwyName, rwyData] : runways)
    {
        for (auto& [hpName, hpData] : rwyData.holdingPoints)
        {
            if (!hpData.sameAs.empty())
            {
                if (hpName == hp1 && hpData.sameAs == hp2)
                {
                    return true;
                }
                if (hpName == hp2 && hpData.sameAs == hp1)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

/// @brief Inserts a holding-point name into flight-strip annotation slot 8 after the seven header characters.
/// @param annotation Current annotation string ([0] = QNH flag, [1..6] = 6-char transfer frequency with dot removed).
/// @param hp Holding-point name to insert.
/// @return Updated annotation string with the HP encoded from position 7 onward.
std::string CDelHelX_LookupsTools::AppendHoldingPointToFlightStripAnnotation(const std::string& annotation, const std::string& hp)
{
    std::string prefix = annotation.substr(0, 7);
    prefix.resize(7, ' ');
    return prefix + hp;
}

/// @brief Tests whether a position lies within the physical bounds of a runway.
/// @param rwy The runway struct providing the near threshold, width, and opposite designator.
/// @param runways Full runway map used to look up the opposite threshold.
/// @param pos Position to test.
/// @return True if the position falls within the runway rectangle.
bool CDelHelX_LookupsTools::IsPositionOnRunway(const runway& rwy, const std::map<std::string, runway>& runways, const EuroScopePlugIn::CPosition& pos)
{
    if (rwy.widthMeters <= 0 || rwy.opposite.empty())
    {
        return false;
    }

    auto oppIt = runways.find(rwy.opposite);
    if (oppIt == runways.end())
    {
        return false;
    }

    // Flat-earth projection centred on the near threshold.
    // At typical runway lengths (<5 km) the error is negligible.
    const double DEG_TO_RAD  = 3.14159265358979323846 / 180.0;
    const double M_PER_DEG_LAT = 111195.0;
    double midLat = (rwy.thresholdLat + oppIt->second.thresholdLat) / 2.0;
    double mPerDegLon = M_PER_DEG_LAT * std::cos(midLat * DEG_TO_RAD);

    // Near threshold is origin; far threshold is B; aircraft position is P — all in metres.
    double bx = (oppIt->second.thresholdLon - rwy.thresholdLon) * mPerDegLon;
    double by = (oppIt->second.thresholdLat - rwy.thresholdLat) * M_PER_DEG_LAT;
    double px = (pos.m_Longitude - rwy.thresholdLon) * mPerDegLon;
    double py = (pos.m_Latitude  - rwy.thresholdLat) * M_PER_DEG_LAT;

    double runwayLength = std::sqrt(bx * bx + by * by);
    if (runwayLength < 1.0)
    {
        return false;
    }

    // Unit vector along the centerline.
    double ux = bx / runwayLength;
    double uy = by / runwayLength;

    double alongTrack = px * ux + py * uy;
    double crossTrack = std::abs(px * uy - py * ux);  // perp distance via 2-D cross product

    return alongTrack >= 0.0
        && alongTrack <= runwayLength
        && crossTrack <= rwy.widthMeters / 2.0;
}

/// @brief Converts a colour name string to the corresponding COLORREF constant.
/// @param colorName Colour name (e.g. "green", "orange").
/// @return Matching COLORREF, or TAG_COLOR_DEFAULT_GRAY for unrecognised names.
COLORREF CDelHelX_LookupsTools::ColorFromString(const std::string& colorName)
{
    if (colorName == "green")  { return TAG_COLOR_GREEN; }
    if (colorName == "orange") { return TAG_COLOR_ORANGE; }
    if (colorName == "turq")   { return TAG_COLOR_TURQ; }
    if (colorName == "purple") { return TAG_COLOR_PURPLE; }
    if (colorName == "red")    { return TAG_COLOR_RED; }
    if (colorName == "white")  { return TAG_COLOR_WHITE; }
    if (colorName == "yellow") { return TAG_COLOR_YELLOW; }
    return TAG_COLOR_DEFAULT_GRAY;
}
