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

double CDelHelX_LookupsTools::DistanceFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition)
{
	EuroScopePlugIn::CPosition rwyThreshold;
	if (rwy == "29")
	{
		rwyThreshold.m_Latitude = 48.109137371047005;
		rwyThreshold.m_Longitude = 16.57538568208911;
	}

	if (rwy == "11")
	{
		rwyThreshold.m_Latitude = 48.122766803767036;
		rwyThreshold.m_Longitude = 16.53361062366087;
	}

	if (rwy == "34")
	{
		rwyThreshold.m_Latitude = 48.088822783854226;
		rwyThreshold.m_Longitude = 16.5912652809662;
	}

	if (rwy == "16")
	{
		rwyThreshold.m_Latitude = 48.119602230239316;
		rwyThreshold.m_Longitude = 16.57825221198715;
	}

	return currentPosition.DistanceTo(rwyThreshold);
}

bool CDelHelX_LookupsTools::MatchesRunwayHoldingPoint(const std::string& rwy, const std::string& hp, int index)
{
	if (rwy == "29")
	{
		if (hp.rfind("A1", 0) == 0 && index == 1)
			return true;
		if (hp.rfind("A2", 0) == 0 && index == 2)
			return true;
		if (hp.rfind("A3", 0) == 0 && index == 3)
			return true;
		if (hp.rfind("A4", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("A6", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("A8", 0) == 0 && index == 4)
			return true;
	}

	if (rwy == "11")
	{
		if (hp.rfind("A12", 0) == 0 && index == 1)
			return true;
		if (hp.rfind("A11", 0) == 0 && index == 2)
			return true;
		if (hp.rfind("A10", 0) == 0 && index == 3)
			return true;
		if (hp.rfind("A9", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("A7", 0) == 0 && index == 4)
			return true;
	}

	if (rwy == "16")
	{
		if (hp.rfind("B1", 0) == 0 && index == 1)
			return true;
		if (hp.rfind("B2", 0) == 0 && index == 2)
			return true;
		if (hp.rfind("B4", 0) == 0 && index == 3)
			return true;
		if (hp.rfind("B5", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("B7", 0) == 0 && index == 4)
			return true;
	}

	if (rwy == "34")
	{
		if (hp.rfind("B12", 0) == 0 && index == 1)
			return true;
		if (hp.rfind("B11", 0) == 0 && index == 2)
			return true;
		if (hp.rfind("B10", 0) == 0 && index == 3)
			return true;
		if (hp.rfind("B8", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("B6", 0) == 0 && index == 4)
			return true;
	}

	return false;
}

std::string CDelHelX_LookupsTools::GetRunwayHoldingPoint(const std::string& rwy, int index)
{
	if (rwy == "29")
	{
		if (index == 1)
			return "A1";
		if (index == 2)
			return "A2";
		if (index == 3)
			return "A3";
	}

	if (rwy == "11")
	{
		if (index == 1)
			return "A12";
		if (index == 2)
			return "A11";
		if (index == 3)
			return "A10";
	}

	if (rwy == "16")
	{
		if (index == 1)
			return "B1";
		if (index == 2)
			return "B2";
		if (index == 3)
			return "B4";
	}

	if (rwy == "34")
	{
		if (index == 1)
			return "B12";
		if (index == 2)
			return "B11";
		if (index == 3)
			return "B10";
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

int CDelHelX_LookupsTools::IsSameHoldingPoint(std::string hp1, std::string hp2)
{
	if (hp1.empty() || hp2.empty())
	{
		return false;
	}

	if (hp1 == hp2)
	{
		return true;
	}

	if (hp1 == "A1" && hp2 == "A2")
	{
		return true;
	}

	if (hp1 == "A11" && hp2 == "A12")
	{
		return true;
	}

	if (hp1 == "B1" && hp2 == "B2")
	{
		return true;
	}

	if (hp1 == "B11" && hp2 == "B12")
	{
		return true;
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