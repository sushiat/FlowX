#include "pch.h"
#include "CDelHelX_LookupsTools.h"

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

COLORREF CDelHelX_LookupsTools::ColorFromString(const std::string& colorName)
{
	if (colorName == "green")  return TAG_COLOR_GREEN;
	if (colorName == "orange") return TAG_COLOR_ORANGE;
	if (colorName == "turq")   return TAG_COLOR_TURQ;
	if (colorName == "purple") return TAG_COLOR_PURPLE;
	if (colorName == "red")    return TAG_COLOR_RED;
	if (colorName == "white")  return TAG_COLOR_WHITE;
	if (colorName == "yellow") return TAG_COLOR_YELLOW;
	return TAG_COLOR_NONE;
}
