#pragma once

#include <string>
#include <filesystem>

#include "CDelHelX_Tags.h"
#include "EuroScope/EuroScopePlugIn.h"

#include "config.h"

using namespace std::chrono_literals;

class CDelHelX : public CDelHelX_Tags
{
public:
	CDelHelX();

	bool OnCompileCommand(const char* sCommandLine) override;
	void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) override;
	void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override;
	void OnTimer(int Counter) override;

	void OnNewMetarReceived(const char* sStation, const char* sFullMetar) override;
	void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;
	void OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan fp, int dataType) override;

private:
	std::map<std::string, std::string> airportQNH;
	std::map<std::string, std::string> standAssignment;

	void RedoFlags();
};



