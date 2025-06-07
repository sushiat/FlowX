#pragma once

#include <string>
#include <sstream>
#include <future>
#include <fstream>
#include <filesystem>

#include "EuroScope/EuroScopePlugIn.h"
#include "semver/semver.hpp"
#include "nlohmann/json.hpp"

#include "constants.h"
#include "helpers.h"
#include "validation.h"
#include "RadarScreen.h"
#include "point.h"
#include "config.h"

using json = nlohmann::json;
using namespace std::chrono_literals;

class CDelHelX : public EuroScopePlugIn::CPlugIn
{
public:
	CDelHelX();
	virtual ~CDelHelX();

	bool OnCompileCommand(const char* sCommandLine) override;
	void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) override;
	void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override;
	void OnTimer(int Counter) override;
	EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;
	void OnNewMetarReceived(const char* sStation, const char* sFullMetar) override;

private:
	bool debug;
	bool updateCheck;
	bool flashOnMessage;
	bool groundOverride;
	bool towerOverride;
	bool noChecks;
	std::future<std::string> latestVersion;
	RadarScreen* radarScreen;
	std::map<std::string, airport> airports;
	std::map<std::string, std::string> airportQNH;
	EuroScopePlugIn::CFlightPlanList twrSameSID;
	std::map<std::string, unsigned long> twrSameSID_flightPlans;

	void LoadSettings();
	void SaveSettings();
	void LoadConfig();
	void UpdateTowerSameSID();

	validation CheckPushStartStatus(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);
	static bool PointInsidePolygon(int polyCorners, double polyX[], double polyY[], double x, double y);
	void RedoFlags();

	void LogMessage(const std::string& message, const std::string& type);
	void LogDebugMessage(const std::string& message, const std::string& type);
	void CheckForUpdate();
};



