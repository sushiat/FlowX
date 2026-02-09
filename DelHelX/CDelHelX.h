#pragma once

#include <string>
#include <filesystem>

#include "CDelHelX_LookupsTools.h"
#include "CDelHelX_Settings.h"
#include "EuroScope/EuroScopePlugIn.h"

#include "validation.h"
#include "config.h"

using namespace std::chrono_literals;

class CDelHelX : public CDelHelX_LookupsTools
{
public:
	CDelHelX();

	bool OnCompileCommand(const char* sCommandLine) override;
	void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) override;
	void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override;
	void OnTimer(int Counter) override;
	
	void OnNewMetarReceived(const char* sStation, const char* sFullMetar) override;
	void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;

private:
	bool groundOverride;
	bool towerOverride;
	bool noChecks;
	
	std::map<std::string, std::string> airportQNH;
	std::map<std::string, unsigned long> twrSameSID_flightPlans;
	std::map<std::string, std::string> twrSameSID_lastDeparted;
	std::map<std::string, std::string> flightStripAnnotation;

	void UpdateTowerSameSID();
	void AutoUpdateDepartureHoldingPoints();
	void UpdateRadarTargetDepartureInfo();

	validation CheckPushStartStatus(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);
	void RedoFlags();
};



