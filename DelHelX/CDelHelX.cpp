#include "pch.h"
#include "CDelHelX.h"

#include <fstream>
#include <iostream>

#include "helpers.h"
#include "date/tz.h"

CDelHelX::CDelHelX()
{
	this->groundOverride = false;
	this->towerOverride = false;
	this->noChecks = false;
}

bool CDelHelX::OnCompileCommand(const char* sCommandLine)
{
	std::vector<std::string> args = split(sCommandLine);

	if (starts_with(args[0], ".delhelx"))
	{
		if (args.size() == 1)
		{
			std::ostringstream msg;
			msg << "Version " << PLUGIN_VERSION << " loaded. Available commands: gnd, twr, nocheck, reset, update, flash, redoflags, testqnh";

			this->LogMessage(msg.str(), "Init");

			return true;
		}

		if (args[1] == "debug") {
			if (this->debug) {
				this->LogMessage("Disabling debug mode", "Debug");
			}
			else {
				this->LogMessage("Enabling debug mode", "Debug");
			}

			this->debug = !this->debug;
			if (this->radarScreen != nullptr)
			{
				this->radarScreen->debug = this->debug;
			}

			this->SaveSettings();

			return true;
		}
		else if (args[1] == "update")
		{
			if (this->updateCheck)
			{
				this->LogMessage("Disabling update check", "Update");
			}
			else {
				this->LogMessage("Enabling update check", "Update");
			}

			this->updateCheck = !this->updateCheck;

			this->SaveSettings();

			return true;
		}
		else if (args[1] == "flash")
		{
			if (this->flashOnMessage)
			{
				this->LogMessage("No longer flashing on DelHelX message", "Config");
			}
			else {
				this->LogMessage("Flashing on DelHelX message", "Config");
			}

			this->flashOnMessage = !this->flashOnMessage;

			this->SaveSettings();

			return true;
		}
		else if (args[1] == "gnd")
		{
			if (this->groundOverride)
			{
				this->LogMessage("GND freq override OFF", "GND");
			}
			else {
				this->LogMessage("GND freq override ON", "GND");
			}

			this->groundOverride = !this->groundOverride;

			return true;
		}
		else if (args[1] == "twr")
		{
			if (this->towerOverride)
			{
				this->LogMessage("TWR freq override OFF", "TWR");
			}
			else {
				this->LogMessage("TWR freq override ON", "TWR");
			}

			this->towerOverride = !this->towerOverride;

			return true;
		}
		else if (args[1] == "nocheck")
		{
			if (this->noChecks)
			{
				this->LogMessage("Flight plan checks turned ON", "Checks");
			}
			else {
				this->LogMessage("Flight plan checks turned OFF, use only for testing!!!", "Checks");
			}

			this->noChecks = !this->noChecks;

			return true;
		}
		else if (args[1] == "reset")
		{
			this->LogMessage("Resetting DelHelX plugin to defaults", "Defaults");
			this->updateCheck = false;
			this->flashOnMessage = false;
			this->groundOverride = false;
			this->towerOverride = false;
			this->noChecks = false;

			this->SaveSettings();

			return true;
		}
		else if (args[1] == "redoflags")
		{
			this->LogMessage("Redoing clearance flags...", "Flags");
			this->RedoFlags();

			return true;
		}
		else if (args[1] == "testqnh")
		{
			this->OnNewMetarReceived("LOWW", "LOWW 231805Z 26011KT CAVOK 15/07 Q2000 TEMPO 32015KT");

			return true;
		}
	}

	return false;
}

void CDelHelX::RedoFlags()
{
	for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
		EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
		// Skip if aircraft is not on the ground (currently using ground speed threshold)
		if (!pos.IsValid() || pos.GetReportedGS() > 40) {
			continue;
		}

		EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
		// Skip if aircraft is tracked (except for aircraft tracked by current controller)
		if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe())) {
			continue;
		}

		std::string dep = fp.GetFlightPlanData().GetOrigin();
		to_upper(dep);

		std::string arr = fp.GetFlightPlanData().GetDestination();
		to_upper(arr);

		std::string cs = fp.GetCallsign();

		// Skip aircraft without a valid flight plan (no departure/destination airport)
		if (dep.empty() || arr.empty()) {
			continue;
		}

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
		{
			// Airport not in config
			continue;
		}

		int depElevation = airport->second.fieldElevation;
		if (pos.GetPressureAltitude() >= depElevation + 50) {
			continue;
		}

		if (fp.GetClearenceFlag() && this->radarScreen != nullptr)
		{
			// Toggle off and back on
			this->radarScreen->StartTagFunction(cs.c_str(), nullptr, 0, cs.c_str(), nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
			this->radarScreen->StartTagFunction(cs.c_str(), nullptr, 0, cs.c_str(), nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
		}
	}
}

void CDelHelX::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
	if (!FlightPlan.IsValid())
	{
		return;
	}

	std::string callSign = FlightPlan.GetCallsign();
	tagInfo tag;

	if (ItemCode == TAG_ITEM_PS_HELPER)
	{
		tag = this->GetPushStartHelperTag(FlightPlan, RadarTarget);
	}
	else if (ItemCode == TAG_ITEM_TAXIOUT)
	{
		tag = this->GetTaxiOutTag(FlightPlan, RadarTarget);
	}
	else if (ItemCode == TAG_ITEM_NEWQNH)
	{
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		this->flightStripAnnotation[callSign] = fpcad.GetFlightStripAnnotation(8);
		if (!this->flightStripAnnotation[callSign].empty() && this->flightStripAnnotation[callSign][0] == 'Q')
		{
			strcpy_s(sItemString, 16, "X");
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_ORANGE;
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_SAMESID)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string dep = fpd.GetOrigin();
		to_upper(dep);
		std::string rwy = fpd.GetDepartureRwy();
		std::string sid = fpd.GetSidName();

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
			return;

		if (!sid.empty() && sid.length() > 2) {
			auto sidKey = sid.substr(0, sid.length() - 2);
			auto sidDesignator = sid.substr(sid.length() - 2);

			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_WHITE;

			// Extend night SIDs
			auto nightIt = airport->second.nightTimeSids.find(sidKey);
			if (nightIt != airport->second.nightTimeSids.end())
			{
				sid = nightIt->second + sidDesignator;
			}

			strcpy_s(sItemString, 16, sid.c_str());

			auto rwyIt = airport->second.runways.find(rwy);
			if (rwyIt != airport->second.runways.end())
			{
				auto colorIt = rwyIt->second.sidColors.find(sidKey);
				if (colorIt != rwyIt->second.sidColors.end())
				{
					*pRGB = ColorFromString(colorIt->second);
				}
			}
		}
	}
	else if (ItemCode == TAG_ITEM_TAKEOFF_TIMER)
	{
		if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
		{
			ULONGLONG now = GetTickCount64();
			auto seconds = (now - this->twrSameSID_flightPlans.at(callSign)) / 1000;

			auto minutes = seconds / 60;
			seconds = seconds % 60;
			auto leadingSeconds = seconds <= 9 ? "0" : "";

			std::string printSeconds = std::to_string(minutes) + ":" + leadingSeconds + std::to_string(seconds);
			strcpy_s(sItemString, 16, printSeconds.c_str());
		}
	}
	else if (ItemCode == TAG_ITEM_TAKEOFF_DISTANCE)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string dep = fpd.GetOrigin();
		to_upper(dep);
		std::string rwy = fpd.GetDepartureRwy();
		auto position = FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPosition();
		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
			return;
		auto distance = DistanceFromRunwayThreshold(rwy, position, airport->second.runways);
		std::string num_text = std::to_string(distance);
		std::string rounded = num_text.substr(0, num_text.find('.') + 3);
		if (distance < 10.0)
		{
			rounded = "0" + rounded;
		}

		strcpy_s(sItemString, 16, rounded.c_str());
	}
	else if (ItemCode == TAG_ITEM_ASSIGNED_RUNWAY)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();

		strcpy_s(sItemString, 16, rwy.c_str());
	}
	else if (ItemCode == TAG_ITEM_TTT)
	{
		auto it = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
			[&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
		if (it != this->ttt_flightPlans.end())
		{
			auto position = RadarTarget.GetPosition().GetPosition();
			auto speed = RadarTarget.GetGS();

			EuroScopePlugIn::CPosition rwyThreshold;
			rwyThreshold.m_Latitude = it->second.thresholdLat;
			rwyThreshold.m_Longitude = it->second.thresholdLon;

			// Calculate TTT based on current position/speed and runway distance
			if (speed > 0)
			{
				double distanceNm = position.DistanceTo(rwyThreshold);
				int totalSeconds = static_cast<int>((distanceNm / speed) * 3600.0);
				int mm = totalSeconds / 60;
				int ss = totalSeconds % 60;
				char buf[16];
				sprintf_s(buf, "%s_%02d:%02d", it->second.designator.c_str(), mm, ss);
				strcpy_s(sItemString, 16, buf);
				*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
				if (totalSeconds > 120)
					*pRGB = TAG_COLOR_GREEN;
				else if (totalSeconds > 60)
					*pRGB = TAG_COLOR_YELLOW;
				else
					*pRGB = TAG_COLOR_RED;
			}
		}
	}
	else if (ItemCode == TAG_ITEM_INBOUND_NM)
	{
		auto myIt = std::find_if(this->ttt_distanceToRunway.begin(), this->ttt_distanceToRunway.end(),
			[&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
		auto myPlan = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
			[&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });

		if (myIt != this->ttt_distanceToRunway.end() && myPlan != this->ttt_flightPlans.end())
		{
			auto sortedIt = this->ttt_sortedByRunway.find(myPlan->second.designator);
			if (sortedIt != this->ttt_sortedByRunway.end())
			{
				const auto& keys = sortedIt->second;
				char buf[16];
				if (keys.front() == myIt->first)
				{
					sprintf_s(buf, "%.1f", myIt->second);
				}
				else
				{
					for (size_t i = 1; i < keys.size(); ++i)
					{
						if (keys[i] == myIt->first)
						{
							double gap = myIt->second - this->ttt_distanceToRunway.at(keys[i - 1]);
							sprintf_s(buf, "+%.1f", gap);
							*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
							if (gap > 3.0)
								*pRGB = TAG_COLOR_GREEN;
							else if (gap > 2.5)
								*pRGB = TAG_COLOR_YELLOW;
							else
								*pRGB = TAG_COLOR_RED;
							break;
						}
					}
				}
				strcpy_s(sItemString, 16, buf);
			}
		}
	}
	else if (ItemCode == TAG_ITEM_SUGGESTED_VACATE)
	{
		[&]()
			{
				auto standIt = this->standAssignment.find(callSign);
				if (standIt == this->standAssignment.end())
					return;

				std::string stand = standIt->second;

				// Find this aircraft in the sorted inbound list
				auto myPlan = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
					[&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
				if (myPlan == this->ttt_flightPlans.end())
					return;

				auto sortedIt = this->ttt_sortedByRunway.find(myPlan->second.designator);
				if (sortedIt == this->ttt_sortedByRunway.end())
					return;

				const auto& keys = sortedIt->second;
				auto myIdx = std::find(keys.begin(), keys.end(), myPlan->first);
				if (myIdx == keys.end())
					return;

				// Calculate gap to follower if one exists
				bool hasFollower = myIdx + 1 != keys.end();
				double gap = hasFollower
					? this->ttt_distanceToRunway.at(*(myIdx + 1)) - this->ttt_distanceToRunway.at(myPlan->first)
					: 0.0;

				// Look up runway vacate config
				std::string arr = FlightPlan.GetFlightPlanData().GetDestination();
				to_upper(arr);
				auto airportIt = this->airports.find(arr);
				if (airportIt == this->airports.end())
					return;

				auto rwyIt = airportIt->second.runways.find(myPlan->second.designator);
				if (rwyIt == airportIt->second.runways.end())
					return;

				for (auto& [vpName, vp] : rwyIt->second.vacatePoints)
				{
					if (hasFollower && gap < vp.minGap)
						continue;

					for (auto& pattern : vp.stands)
					{
						bool matched = false;
						if (pattern == "*")
						{
							matched = true;
						}
						else if (pattern.back() == '*')
						{
							std::string prefix = pattern.substr(0, pattern.size() - 1);
							to_upper(prefix);
							matched = stand.rfind(prefix, 0) == 0;
						}
						else
						{
							std::string p = pattern;
							to_upper(p);
							matched = stand == p;
						}

						if (matched)
						{
							strcpy_s(sItemString, 16, vpName.c_str());
							*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
							*pRGB = TAG_COLOR_WHITE;
							return;
						}
					}
				}
			}();
	}
	else if (ItemCode == TAG_ITEM_HP1)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string dep = fpd.GetOrigin();
		to_upper(dep);
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		this->flightStripAnnotation[callSign] = fpcad.GetFlightStripAnnotation(8);
		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
			return;
		if (this->flightStripAnnotation[callSign].length() > 2 && MatchesRunwayHoldingPoint(rwy, this->flightStripAnnotation[callSign].substr(2), 1, airport->second.runways))
		{
			strcpy_s(sItemString, 16, this->flightStripAnnotation[callSign].substr(2).c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_GREEN;

			if (this->flightStripAnnotation[callSign].substr(2).find('*') != std::string::npos)
				*pRGB = TAG_COLOR_ORANGE;

			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				*pRGB = TAG_COLOR_DARKGREY;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_HP2)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string dep = fpd.GetOrigin();
		to_upper(dep);
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		this->flightStripAnnotation[callSign] = fpcad.GetFlightStripAnnotation(8);
		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
			return;
		if (this->flightStripAnnotation[callSign].length() > 2 && MatchesRunwayHoldingPoint(rwy, this->flightStripAnnotation[callSign].substr(2), 2, airport->second.runways))
		{
			strcpy_s(sItemString, 16, this->flightStripAnnotation[callSign].substr(2).c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_GREEN;

			if (this->flightStripAnnotation[callSign].substr(2).find('*') != std::string::npos)
				*pRGB = TAG_COLOR_ORANGE;

			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				*pRGB = TAG_COLOR_DARKGREY;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_HP3)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string dep = fpd.GetOrigin();
		to_upper(dep);
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		this->flightStripAnnotation[callSign] = fpcad.GetFlightStripAnnotation(8);
		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
			return;
		if (this->flightStripAnnotation[callSign].length() > 2 && MatchesRunwayHoldingPoint(rwy, this->flightStripAnnotation[callSign].substr(2), 3, airport->second.runways))
		{
			strcpy_s(sItemString, 16, this->flightStripAnnotation[callSign].substr(2).c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_GREEN;

			if (this->flightStripAnnotation[callSign].substr(2).find('*') != std::string::npos)
				*pRGB = TAG_COLOR_ORANGE;

			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				*pRGB = TAG_COLOR_DARKGREY;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_HPO)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string dep = fpd.GetOrigin();
		to_upper(dep);
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		this->flightStripAnnotation[callSign] = fpcad.GetFlightStripAnnotation(8);
		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
			return;
		if (this->flightStripAnnotation[callSign].length() > 2 && MatchesRunwayHoldingPoint(rwy, this->flightStripAnnotation[callSign].substr(2), 4, airport->second.runways))
		{
			strcpy_s(sItemString, 16, this->flightStripAnnotation[callSign].substr(2).c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_GREEN;

			if (this->flightStripAnnotation[callSign].substr(2).find('*') != std::string::npos)
				*pRGB = TAG_COLOR_ORANGE;

			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				*pRGB = TAG_COLOR_DARKGREY;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_DEPARTURE_INFO)
	{
		*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
		try
		{
			std::string depAirport = FlightPlan.GetFlightPlanData().GetOrigin();
			to_upper(depAirport);
			auto airport = this->airports.find(depAirport);
			if (airport == this->airports.end())
				return;
			std::string groundState = FlightPlan.GetGroundState();
			if (groundState == "TAXI" || groundState == "DEPA")
			{
				std::string rwy = FlightPlan.GetFlightPlanData().GetDepartureRwy();
				if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) == 0)
				{
					std::string lastDeparted_callSign;
					if (this->twrSameSID_lastDeparted.find(rwy) != this->twrSameSID_lastDeparted.end())
					{
						lastDeparted_callSign = this->twrSameSID_lastDeparted[rwy];
					}

					if (!lastDeparted_callSign.empty())
					{
						bool lastDeparted_active = false;
						EuroScopePlugIn::CRadarTarget lastDeparted_radarTarget = this->RadarTargetSelect(lastDeparted_callSign.c_str());
						if (lastDeparted_radarTarget.IsValid())
						{
							lastDeparted_active = true;
						}

						if (lastDeparted_active)
						{
							char departedWtc = lastDeparted_radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetAircraftWtc();
							char wtc = FlightPlan.GetFlightPlanData().GetAircraftWtc();

							if (GetAircraftWeightCategoryRanking(departedWtc) > GetAircraftWeightCategoryRanking(wtc))
							{
								// Time based
								unsigned long secondsRequired = 120;

								this->flightStripAnnotation[lastDeparted_callSign] = lastDeparted_radarTarget.GetCorrelatedFlightPlan().GetControllerAssignedData().GetFlightStripAnnotation(8);
								this->flightStripAnnotation[callSign] = FlightPlan.GetControllerAssignedData().GetFlightStripAnnotation(8);
								if (this->flightStripAnnotation[lastDeparted_callSign].length() > 2 && this->flightStripAnnotation[callSign].length() > 2)
								{
									std::string departedHP = this->flightStripAnnotation[lastDeparted_callSign].substr(2);
									std::string hp = this->flightStripAnnotation[callSign].substr(2);
									if (!IsSameHoldingPoint(departedHP, hp, airport->second.runways))
									{
										secondsRequired += 60;
									}
								}

								ULONGLONG now = GetTickCount64();
								if (this->twrSameSID_flightPlans.find(lastDeparted_callSign) != this->twrSameSID_flightPlans.end()) {
									auto secondsSinceDeparted = (now - this->twrSameSID_flightPlans.at(lastDeparted_callSign)) / 1000;

									if (secondsSinceDeparted > secondsRequired)
									{
										*pRGB = TAG_COLOR_GREEN;
										strcpy_s(sItemString, 16, "OK");
										return;
									}

									if (secondsSinceDeparted + 30 > secondsRequired)
									{
										*pRGB = TAG_COLOR_GREEN;
										strcpy_s(sItemString, 16, (std::to_string(secondsRequired - secondsSinceDeparted) + "s").c_str());
										return;
									}

									if (secondsSinceDeparted + 45 > secondsRequired)
									{
										*pRGB = TAG_COLOR_YELLOW;
										strcpy_s(sItemString, 16, (std::to_string(secondsRequired - secondsSinceDeparted) + "s").c_str());
										return;
									}

									*pRGB = TAG_COLOR_RED;
									strcpy_s(sItemString, 16, (std::to_string(secondsRequired - secondsSinceDeparted) + "s").c_str());
									return;
								}

								// Flight plan removed, either disconnected or out of range
								*pRGB = TAG_COLOR_GREEN;
								strcpy_s(sItemString, 16, "OK");
								return;
							}

							// Distance based
							std::string departedSID = lastDeparted_radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetSidName();
							std::string sid = FlightPlan.GetFlightPlanData().GetSidName();

							double distanceRequired = 5;

							if (!departedSID.empty() && !sid.empty() && departedSID.length() > 2 && sid.length() > 2) {
								auto depSidKey = departedSID.substr(0, departedSID.length() - 2);
								auto sidKey = sid.substr(0, sid.length() - 2);

								auto rwyIt = airport->second.runways.find(rwy);
								if (rwyIt != airport->second.runways.end())
								{
									auto& sidGroupsMap = rwyIt->second.sidGroups;
									auto depGroupIt = sidGroupsMap.find(depSidKey);
									auto sidGroupIt = sidGroupsMap.find(sidKey);
									if (depGroupIt != sidGroupsMap.end() && sidGroupIt != sidGroupsMap.end())
									{
										if (depGroupIt->second != sidGroupIt->second)
										{
											distanceRequired = 3;
										}
									}
								}
							}

							auto distanceBetween = RadarTarget.GetPosition().GetPosition().DistanceTo(lastDeparted_radarTarget.GetPosition().GetPosition());
							if (distanceBetween > distanceRequired)
							{
								*pRGB = TAG_COLOR_GREEN;
								strcpy_s(sItemString, 16, "OK");
								return;
							}

							if (distanceRequired <= 3.1 && distanceBetween > 1.3)
							{
								*pRGB = TAG_COLOR_GREEN;
								std::string num_text = std::to_string(distanceRequired - distanceBetween);
								std::string rounded = num_text.substr(0, num_text.find('.') + 3) + "nm";
								strcpy_s(sItemString, 16, rounded.c_str());
								return;
							}

							if (distanceBetween > 3)
							{
								*pRGB = TAG_COLOR_GREEN;
								std::string num_text = std::to_string(distanceRequired - distanceBetween);
								std::string rounded = num_text.substr(0, num_text.find('.') + 3) + "nm";
								strcpy_s(sItemString, 16, rounded.c_str());
								return;
							}

							if (distanceBetween > 2.5)
							{
								*pRGB = TAG_COLOR_YELLOW;
								std::string num_text = std::to_string(distanceRequired - distanceBetween);
								std::string rounded = num_text.substr(0, num_text.find('.') + 3) + "nm";
								strcpy_s(sItemString, 16, rounded.c_str());
								return;
							}

							*pRGB = TAG_COLOR_RED;
							std::string num_text = std::to_string(distanceRequired - distanceBetween);
							std::string rounded = num_text.substr(0, num_text.find('.') + 3) + "nm";
							strcpy_s(sItemString, 16, rounded.c_str());
							return;
						}
					}

					*pRGB = TAG_COLOR_GREEN;
					strcpy_s(sItemString, 16, "OK?");
				}
				else if (this->twrSameSID_lastDeparted.find(rwy) != this->twrSameSID_lastDeparted.end() && this->twrSameSID_lastDeparted[rwy] == callSign)
				{
					// This is the last aircraft that departed this runway
					*pRGB = TAG_COLOR_ORANGE;
					strcpy_s(sItemString, 16, rwy.c_str());
				}
			}
		}
		catch ([[maybe_unused]] const std::exception& ex)
		{
			*pRGB = TAG_COLOR_RED;
			strcpy_s(sItemString, 16, "ERR");
		}
	}

	*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
	strcpy_s(sItemString, 16, tag.tag.c_str());
	*pRGB = tag.color;
}

void CDelHelX::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
	EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelectASEL();
	if (!fp.IsValid()) {
		return;
	}

	std::string callSign = fp.GetCallsign();

	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string rwy = fpd.GetDepartureRwy();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);

	EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();

	auto airport = this->airports.find(dep);
	if (airport == this->airports.end() && FunctionId != TAG_FUNC_CLRD_TO_LAND && FunctionId != TAG_FUNC_MISSED_APP)
	{
		// Airport not in config
		return;
	}

	EuroScopePlugIn::CRadarTarget rt = fp.GetCorrelatedRadarTarget();

	if (FunctionId == TAG_FUNC_ON_FREQ) {
		auto tag = this->GetPushStartHelperTag(fp, rt);
		if (tag.color != TAG_COLOR_RED)
		{
			// Are we ground or higher?
			if (this->ControllerMyself().GetFacility() >= 3)
			{
				EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();

				bool isTaxiOut = false;
				for (auto& taxiOut : airport->second.taxiOutStands)
				{
					u_int corners = taxiOut.second.lat.size();
					double lat[10], lon[10];
					std::copy(taxiOut.second.lat.begin(), taxiOut.second.lat.end(), lat);
					std::copy(taxiOut.second.lon.begin(), taxiOut.second.lon.end(), lon);

					if (CDelHelX::PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
					{
						isTaxiOut = true;
						continue;
					}
				}

				if (isTaxiOut)
				{
					std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
					fp.GetControllerAssignedData().SetScratchPadString("ST-UP");
					fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
				}
				else
				{
					// We could give PUSH here, but it's better to visually check, so let's just "pop" it up using ONFREQ
					std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
					fp.GetControllerAssignedData().SetScratchPadString("ONFREQ");
					fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
				}
			}
			else
			{
				// We are delivery set ONFREQ
				std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
				fp.GetControllerAssignedData().SetScratchPadString("ONFREQ");
				fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
			}
		}
	}
	else if (FunctionId == TAG_FUNC_CLEAR_NEWQNH)
	{
		if (!this->flightStripAnnotation[callSign].empty())
		{
			this->flightStripAnnotation[callSign][0] = ' ';
			fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
			this->PushToOtherControllers(fp);
		}
	}
	else if (FunctionId == TAG_FUNC_ASSIGN_HP1)
	{
		this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], GetRunwayHoldingPoint(rwy, 1, airport->second.runways));
		fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
	else if (FunctionId == TAG_FUNC_ASSIGN_HP2)
	{
		this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], GetRunwayHoldingPoint(rwy, 2, airport->second.runways));
		fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
	else if (FunctionId == TAG_FUNC_ASSIGN_HP3)
	{
		this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], GetRunwayHoldingPoint(rwy, 3, airport->second.runways));
		fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
	else if (FunctionId == TAG_FUNC_REQUEST_HP1)
	{
		this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], GetRunwayHoldingPoint(rwy, 1, airport->second.runways) + "*");
		fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
	else if (FunctionId == TAG_FUNC_REQUEST_HP2)
	{
		this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], GetRunwayHoldingPoint(rwy, 2, airport->second.runways) + "*");
		fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
	else if (FunctionId == TAG_FUNC_REQUEST_HP3)
	{
		this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], GetRunwayHoldingPoint(rwy, 3, airport->second.runways) + "*");
		fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
	else if (FunctionId == TAG_FUNC_ASSIGN_HPO)
	{
		RECT area;
		area.left = Pt.x;
		area.right = Pt.x + 100;
		area.top = Pt.y;
		area.bottom = Pt.y + 100;
		this->OpenPopupList(area, "Assign HP", 1);

		auto rwyIt = airport->second.runways.find(rwy);
		if (rwyIt != airport->second.runways.end())
		{
			for (auto& [hpName, hpData] : rwyIt->second.holdingPoints)
			{
				if (hpData.assignable)
				{
					this->AddPopupListElement(hpName.c_str(), "", TAG_FUNC_HPO_LISTSELECT);
				}
			}
		}
	}
	else if (FunctionId == TAG_FUNC_REQUEST_HPO)
	{
		RECT area;
		area.left = Pt.x;
		area.right = Pt.x + 100;
		area.top = Pt.y;
		area.bottom = Pt.y + 100;
		this->OpenPopupList(area, "Request HP", 1);

		auto rwyIt = airport->second.runways.find(rwy);
		if (rwyIt != airport->second.runways.end())
		{
			for (auto& [hpName, hpData] : rwyIt->second.holdingPoints)
			{
				if (hpData.assignable)
				{
					this->AddPopupListElement((hpName + "*").c_str(), "", TAG_FUNC_HPO_LISTSELECT);
				}
			}
		}
	}
	else if (FunctionId == TAG_FUNC_HPO_LISTSELECT)
	{
		this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], sItemString);
		fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
	else if (FunctionId == TAG_FUNC_LINE_UP)
	{
		std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
		fp.GetControllerAssignedData().SetScratchPadString("LINEUP");
		fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
	}
	else if (FunctionId == TAG_FUNC_TAKE_OFF)
	{
		std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
		fp.GetControllerAssignedData().SetScratchPadString("DEPA");
		fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());

		fp.StartTracking();
	}
	else if (FunctionId == TAG_FUNC_TRANSFER_NEXT)
	{
		std::string targetController = fp.GetCoordinatedNextController();
		if (!targetController.empty() && this->ControllerMyself().GetFacility() >= 4)
		{
			fp.InitiateHandoff(targetController.c_str());
		}
		else
		{
			fp.EndTracking();
		}
		if (this->flightStripAnnotation[callSign].length() > 1)
		{
			this->flightStripAnnotation[callSign][1] = 'T';
		}
		else
		{
			this->flightStripAnnotation[callSign] = " T";
		}
		fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
	else if (FunctionId == TAG_FUNC_CLRD_TO_LAND)
	{
		fp.EndTracking();
		this->radarScreen->StartTagFunction(callSign.c_str(), nullptr, 0, "S-Highlight", TOPSKY_PLUGIN_NAME, 4, POINT(), RECT());
	}
	else if (FunctionId == TAG_FUNC_MISSED_APP)
	{
		fp.StartTracking();
		fpcad.SetClearedAltitude(5000);

		this->radarScreen->StartTagFunction(callSign.c_str(), nullptr, 0, "S-Highlight", TOPSKY_PLUGIN_NAME, 4, POINT(), RECT());
		std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
		fp.GetControllerAssignedData().SetScratchPadString((scratchBackup + "MISAP_").c_str());
	}
}

void CDelHelX::OnTimer(int Counter)
{
	if (this->updateCheck && this->latestVersion.valid() && this->latestVersion.wait_for(0ms) == std::future_status::ready) {
		this->CheckForUpdate();
	}

	if (Counter > 0 && Counter % 10 == 0)
	{
		this->CheckAirportNAPReminder();
	}

	if (Counter > 0 && Counter % 2 == 0)
	{
		this->UpdateTowerSameSID();
		this->AutoUpdateDepartureHoldingPoints();
		this->UpdateRadarTargetDepartureInfo();
		this->UpdateTTTInbounds();
	}
}

void CDelHelX::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
	std::string callSign = FlightPlan.GetCallsign();
	if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
	{
		this->twrSameSID.RemoveFpFromTheList(FlightPlan);
		this->twrSameSID_flightPlans.erase(callSign);
	}
}

void CDelHelX::OnNewMetarReceived(const char* sStation, const char* sFullMetar)
{
	std::string station = sStation;
	to_upper(station);

	this->LogDebugMessage("New METAR for station " + station + ": " + sFullMetar, "Metar");

	auto airport = this->airports.find(station);
	if (airport == this->airports.end())
	{
		// Station not in airport config, so ignore it
		return;
	}

	std::vector<std::string> metarElements = split(sFullMetar);
	for (std::string metarElement : metarElements)
	{
		static const std::regex qnh(R"(Q[0-9]{4})");
		static const std::regex alt(R"(A[0-9]{4})");

		if (std::regex_match(metarElement, qnh) || std::regex_match(metarElement, alt))
		{
			// Check if existing QNH and if that is now different
			auto existingQNH = this->airportQNH.find(station);
			if (existingQNH == this->airportQNH.end())
			{
				this->LogDebugMessage("First QNH value for airport " + station + " is " + metarElement, "Metar");

				// No existing QNH, add it
				this->airportQNH.emplace(station, metarElement);
			}
			else
			{
				if (existingQNH->second != metarElement)
				{
					this->LogDebugMessage("New QNH value for airport " + station + " is " + metarElement, "Metar");

					// Save new QNH
					this->airportQNH[station] = metarElement;

					// Set flight strip annotation on aircraft on the ground at that airport
					for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
						EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();

						// Skip aircraft is not on the ground
						auto stationAp = this->airports.find(station);
						int stationElevation = stationAp != this->airports.end() ? stationAp->second.fieldElevation : 0;
						if (!pos.IsValid() || pos.GetPressureAltitude() >= stationElevation + 50) {
							continue;
						}

						EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
						// Skip aircraft is tracked (except aircraft tracked by current controller)
						if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe())) {
							continue;
						}

						std::string callSign = fp.GetCallsign();
						std::string dep = fp.GetFlightPlanData().GetOrigin();
						to_upper(dep);

						if (dep == station && fp.GetClearenceFlag())
						{
							EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
							if (this->flightStripAnnotation[callSign].empty())
							{
								this->flightStripAnnotation[callSign].append("Q");
							}
							else
							{
								this->flightStripAnnotation[callSign][0] = 'Q';
							}
							fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
							this->PushToOtherControllers(fp);
						}
					}
				}
			}
		}
	}
}

void CDelHelX::OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan fp, int dataType)
{
	if (dataType == EuroScopePlugIn::CTR_DATA_TYPE_SCRATCH_PAD_STRING)
	{
		std::string callSign = fp.GetCallsign();

		std::string scratch = fp.GetControllerAssignedData().GetScratchPadString();
		size_t pos = scratch.find("GRP/S/");
		if (pos != std::string::npos) {
			std::string stand = scratch.substr(pos + 6);
			
			this->standAssignment[callSign] = stand;
		}
	}
}

static CDelHelX* pPlugin;

void __declspec (dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
	*ppPlugInInstance = pPlugin = new CDelHelX();
}

void __declspec (dllexport) EuroScopePlugInExit(void)
{
	delete pPlugin;
}