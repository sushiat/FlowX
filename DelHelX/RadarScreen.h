#pragma once

#include <map>
#include <set>
#include <string>
#include <windowsx.h>

#include "EuroScope/EuroScopePlugIn.h"

class RadarScreen : public EuroScopePlugIn::CRadarScreen
{
public:
	RadarScreen();
	virtual ~RadarScreen();

	bool debug;
	std::set<std::string> groundStations;
	std::set<std::string> towerStations;
	std::set<std::string> approachStations;
	std::map<std::string, std::string> centerStations;
	std::map<std::string, std::string> radarTargetDepartureInfos;
	std::map<std::string, POINT> radarTargetScreenPositions;
	std::map<std::string, COLORREF> radarTargetDepartureInfoColors;

	inline void OnAsrContentToBeClosed() override { delete this; }
	void OnControllerPositionUpdate(EuroScopePlugIn::CController Controller) override;
	void OnControllerDisconnect(EuroScopePlugIn::CController Controller) override;
	void OnRefresh(HDC hDC, int Phase) override;
	void OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget) override;
};