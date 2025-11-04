#pragma once

#include <map>
#include <set>
#include <string>
#include <windowsx.h>

#include "EuroScope/EuroScopePlugIn.h"

struct depInfo
{
	std::string dep_info;
	POINT pos;
	COLORREF dep_color;
	POINT lastDrag;
	int dragX;
	int dragY;
	std::string hp_info;
	COLORREF hp_color;
};

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

	std::map<std::string, depInfo> radarTargetDepartureInfos;

	inline void OnAsrContentToBeClosed() override { delete this; }
	void OnControllerPositionUpdate(EuroScopePlugIn::CController Controller) override;
	void OnControllerDisconnect(EuroScopePlugIn::CController Controller) override;
	void OnRefresh(HDC hDC, int Phase) override;
	void OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget) override;
	void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;
	void OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released) override;
	void OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;
};