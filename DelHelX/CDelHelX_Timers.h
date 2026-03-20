#pragma once
#include "CDelHelX_LookupsTools.h"

class CDelHelX_Timers : public CDelHelX_LookupsTools
{
protected:
	std::map<std::string, ULONGLONG> twrSameSID_flightPlans;
	std::map<std::string, std::string> twrSameSID_lastDeparted;
	std::map<std::string, char> dep_prevWtc;
	std::map<std::string, std::string> dep_prevSid;
	std::map<std::string, ULONGLONG> dep_prevTakeoffOffset;
	std::map<std::string, double> dep_prevDistanceAtTakeoff;
	std::map<std::string, int> dep_timeRequired;
	int dep_sequenceCounter = 0;
	std::map<std::string, int> dep_sequenceNumber;
	std::map<std::string, std::string> flightStripAnnotation;
	std::map<std::string, runway> ttt_flightPlans;
	std::map<std::string, double> ttt_distanceToRunway;
	std::map<std::string, std::vector<std::string>> ttt_sortedByRunway;
	std::map<std::string, ULONGLONG> ttt_goAround;
	std::map<std::string, ULONGLONG> ttt_recentlyRemoved;
	std::map<std::string, std::string> standAssignment;
	std::map<std::string, std::string> groundStatus;
	bool blinking = false;

	void CheckAirportNAPReminder();
	void UpdateTTTInbounds();
	void UpdateTowerSameSID();
	void UpdateRadarTargetDepartureInfo();
	void AutoUpdateDepartureHoldingPoints();
};
