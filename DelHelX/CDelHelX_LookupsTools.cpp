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
		return 0.0;

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
		return -1;

	EuroScopePlugIn::CPosition rwyThreshold;
	rwyThreshold.m_Latitude = rwyIt->second.thresholdLat;
	rwyThreshold.m_Longitude = rwyIt->second.thresholdLon;

	return rwyThreshold.DirectionTo(currentPosition);
}

/// @brief Checks whether a holding-point name prefix and index match any point on the runway.
/// @param rwy Runway designator.
/// @param hp Holding-point name to match (prefix comparison).
/// @param index Expected slot index.
/// @param runways Runway map for the airport.
/// @return True if a matching holding point is found.
bool CDelHelX_LookupsTools::MatchesRunwayHoldingPoint(const std::string& rwy, const std::string& hp, int index, const std::map<std::string, runway>& runways)
{
	auto rwyIt = runways.find(rwy);
	if (rwyIt == runways.end())
		return false;

	for (auto& [hpName, hpData] : rwyIt->second.holdingPoints)
	{
		if (hp.rfind(hpName, 0) == 0 && hpData.index == index)
			return true;
	}

	return false;
}

/// @brief Returns the name of the non-assignable holding point for the given slot index on a runway.
/// @param rwy Runway designator.
/// @param index Slot index (1–3 for HP1–HP3).
/// @param runways Runway map for the airport.
/// @return Holding-point name, or an empty string if none matches.
std::string CDelHelX_LookupsTools::GetRunwayHoldingPoint(const std::string& rwy, int index, const std::map<std::string, runway>& runways)
{
	auto rwyIt = runways.find(rwy);
	if (rwyIt == runways.end())
		return "";

	for (auto& [hpName, hpData] : rwyIt->second.holdingPoints)
	{
		if (hpData.index == index && !hpData.assignable)
			return hpName;
	}

	return "";
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
		return false;

	if (hp1 == hp2)
		return true;

	for (auto& [rwyName, rwyData] : runways)
	{
		for (auto& [hpName, hpData] : rwyData.holdingPoints)
		{
			if (!hpData.sameAs.empty())
			{
				if (hpName == hp1 && hpData.sameAs == hp2)
					return true;
				if (hpName == hp2 && hpData.sameAs == hp1)
					return true;
			}
		}
	}

	return false;
}

/// @brief Inserts a holding-point name into flight-strip annotation slot 8 after the two flag characters.
/// @param annotation Current annotation string (characters 0–1 are reserved flags).
/// @param hp Holding-point name to insert.
/// @return Updated annotation string with the HP encoded from position 2 onward.
std::string CDelHelX_LookupsTools::AppendHoldingPointToFlightStripAnnotation(const std::string& annotation, const std::string& hp)
{
	if (annotation.length() >= 2)
	{
		return annotation.substr(0, 2).append(hp);
	}

	if (!annotation.empty())
	{
		return annotation.substr(0, 1).append(" " + hp);
	}

	return "  " + hp;
}

/// @brief Converts a colour name string to the corresponding COLORREF constant.
/// @param colorName Colour name (e.g. "green", "orange").
/// @return Matching COLORREF, or TAG_COLOR_DEFAULT_GRAY for unrecognised names.
COLORREF CDelHelX_LookupsTools::ColorFromString(const std::string& colorName)
{
	if (colorName == "green")  return TAG_COLOR_GREEN;
	if (colorName == "orange") return TAG_COLOR_ORANGE;
	if (colorName == "turq")   return TAG_COLOR_TURQ;
	if (colorName == "purple") return TAG_COLOR_PURPLE;
	if (colorName == "red")    return TAG_COLOR_RED;
	if (colorName == "white")  return TAG_COLOR_WHITE;
	if (colorName == "yellow") return TAG_COLOR_YELLOW;
	return TAG_COLOR_DEFAULT_GRAY;
}
