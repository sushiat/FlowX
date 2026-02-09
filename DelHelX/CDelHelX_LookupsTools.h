#pragma once
#include "CDelHelX_Settings.h"

class CDelHelX_LookupsTools : public CDelHelX_Settings
{
protected:
	static bool PointInsidePolygon(int polyCorners, double polyX[], double polyY[], double x, double y);
	static double DistanceFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition);
	static bool MatchesRunwayHoldingPoint(const std::string& rwy, const std::string& hp, int index);
	static std::string GetRunwayHoldingPoint(const std::string& rwy, int index);
	static int GetAircraftWeightCategoryRanking(char wtc);
	static int IsSameHoldingPoint(std::string hp1, std::string hp2);
	static std::string AppendHoldingPointToFlightStripAnnotation(const std::string& annotation, const std::string& hp);
};
