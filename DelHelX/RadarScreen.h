#pragma once

#include <map>
#include <set>
#include <string>

#include "constants.h"
#include "EuroScope/EuroScopePlugIn.h"

struct depInfo
{
	std::string dep_info = std::string("");
	POINT pos = { -1,-1 };
	COLORREF dep_color = TAG_COLOR_TURQ;
	POINT lastDrag = { -1,-1 };
	int dragX = 0;
	int dragY = 0;
	std::string hp_info = std::string("");
	COLORREF hp_color = TAG_COLOR_TURQ;
	COLORREF sid_color = TAG_COLOR_TURQ;
};

class RadarScreen : public EuroScopePlugIn::CRadarScreen
{
public:
	explicit RadarScreen(RadarScreen** ownerPtr);
	virtual ~RadarScreen();

	bool debug;
	std::set<std::string> groundStations;
	std::set<std::string> towerStations;
	std::set<std::string> approachStations;
	std::map<std::string, std::string> centerStations;

	std::map<std::string, depInfo> radarTargetDepartureInfos;

	inline void OnAsrContentToBeClosed() override { if (m_ownerPtr) *m_ownerPtr = nullptr; delete this; }

private:
	RadarScreen** m_ownerPtr;
	void OnControllerPositionUpdate(EuroScopePlugIn::CController Controller) override;
	void OnControllerDisconnect(EuroScopePlugIn::CController Controller) override;
	void OnRefresh(HDC hDC, int Phase) override;
	void OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget) override;
	void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;
	void OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released) override;
};