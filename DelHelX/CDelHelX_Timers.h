#pragma once
#include "CDelHelX_LookupsTools.h"

class CDelHelX_Timers : public CDelHelX_LookupsTools
{
protected:
	std::map<std::string, ULONGLONG> twrSameSID_flightPlans;
	std::map<std::string, std::string> twrSameSID_lastDeparted;
	std::map<std::string, std::string> flightStripAnnotation;
	std::map<std::string, runway> ttt_flightPlans;
	std::map<std::string, double> ttt_distanceToRunway;
	std::map<std::string, std::vector<std::string>> ttt_sortedByRunway;
	std::map<std::string, std::string> standAssignment;
	bool blinking = false;

	void CheckAirportNAPReminder();
	void UpdateTTTInbounds();
	void UpdateTowerSameSID();
	void UpdateRadarTargetDepartureInfo();
	void AutoUpdateDepartureHoldingPoints();
};
