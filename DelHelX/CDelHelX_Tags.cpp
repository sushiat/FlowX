#include "pch.h"
#include "CDelHelX_Tags.h"

#include "helpers.h"

tagInfo CDelHelX_Tags::GetTwrNextFreqTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
	tagInfo tag;
	tag.color = TAG_COLOR_DEFAULT_GRAY;

	auto fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);
	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
	{
		// Airport not in config, so ignore it
		return tag;
	}

	std::string groundState = fp.GetGroundState();
	bool transferred = false;
	if (groundState == "TAXI" || groundState == "DEPA")
	{
		// Check if we passed the aircraft to the next frequency, clear the tag if we did
		std::string callSign = fp.GetCallsign();
		this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
		if (this->flightStripAnnotation[callSign].length() > 1 && this->flightStripAnnotation[callSign][1] == 'T')
		{
			transferred = true;
		}

		if (this->radarScreen == nullptr)
		{
			return tag;
		}

		auto me = this->ControllerMyself();
		if (me.IsController() && me.GetRating() > 1 && me.GetFacility() >= 3 && me.GetFacility() <= 4)
		{
			// Only show tower to ground, but not to tower
			if (me.GetFacility() == 3) {
				bool towerOnline = false;
				for (auto station : this->radarScreen->towerStations)
				{
					if (station.find(dep) != std::string::npos)
					{
						towerOnline = true;
						continue;
					}
				}

				if (towerOnline || this->towerOverride)
				{
					std::string rwy = fpd.GetDepartureRwy();
					double distToThreshold = DistanceFromRunwayThreshold(rwy, rt.GetPosition().GetPosition(), airport->second.runways);
					bool nearThreshold = distToThreshold < 0.2;

					bool atHoldingPoint = false;
					auto rwyIt = airport->second.runways.find(rwy);
					if (rwyIt != airport->second.runways.end())
					{
						for (auto& hp : rwyIt->second.holdingPoints)
						{
							u_int corners = static_cast<u_int>(hp.second.lat.size());
							double polyX[10], polyY[10];
							std::copy(hp.second.lon.begin(), hp.second.lon.end(), polyX);
							std::copy(hp.second.lat.begin(), hp.second.lat.end(), polyY);
							if (PointInsidePolygon(static_cast<int>(corners), polyX, polyY, rt.GetPosition().GetPosition().m_Longitude, rt.GetPosition().GetPosition().m_Latitude))
							{
								atHoldingPoint = true;
								break;
							}
						}
					}

					for (auto rwyFreq : airport->second.rwyTwrFreq)
					{
						if (rwy == rwyFreq.first)
						{
							if (!transferred) 
							{
								tag.color = nearThreshold || atHoldingPoint ? (this->blinking ? TAG_COLOR_YELLOW : TAG_COLOR_WHITE) : TAG_COLOR_WHITE;
							}
							tag.tag = "->" + rwyFreq.second;
							return tag;
						}
					}

					// Didn't find a runway specific tower, so return default
					if (!transferred)
					{
						tag.color = nearThreshold || atHoldingPoint ? (this->blinking ? TAG_COLOR_YELLOW : TAG_COLOR_WHITE) : TAG_COLOR_WHITE;
					}
					tag.tag = "->" + airport->second.twrFreq;
					return tag;
				}
			}

			// Not squawking mode-C, don't show next freq
			if (!rt.GetPosition().GetTransponderC() && rt.GetPosition().GetPressureAltitude() > (airport->second.fieldElevation + 50))
			{
				tag.tag = "!MODE-C";
				tag.color = this->blinking ? TAG_COLOR_RED : TAG_COLOR_ORANGE;
				return tag;
			}

			// We are TWR, find APP or CTR freq, plus more color coding
			if (!transferred)
			{
				tag.color = TAG_COLOR_WHITE;
				if (rt.GetPosition().GetPressureAltitude() >= airport->second.airborneTransfer && rt.GetPosition().GetPressureAltitude() < airport->second.airborneTransferWarning)
				{
					tag.color = TAG_COLOR_TURQ;
				}
				else if (rt.GetPosition().GetPressureAltitude() >= airport->second.airborneTransferWarning)
				{
					tag.color = this->blinking ? TAG_COLOR_ORANGE : TAG_COLOR_TURQ;
				}
			}

			for (auto station : this->radarScreen->approachStations)
			{
				if (station.find(dep) != std::string::npos)
				{
					// Search for SID-specific freq
					std::string sid = fpd.GetSidName();
					{
						std::string freq = airport->second.defaultAppFreq;
						for (auto& [f, sids] : airport->second.sidAppFreqs)
						{
							if (std::find(sids.begin(), sids.end(), sid) != sids.end())
							{
								freq = f;
								break;
							}
						}
						tag.tag = "->" + freq;
						return tag;
					}
				}
			}

			for (auto center : airport->second.ctrStations)
			{
				for (auto station : this->radarScreen->centerStations)
				{
					if (station.first.find(center) != std::string::npos)
					{
						tag.tag = "->" + station.second;
						return tag;
					}


				}
			}

			// Nothing online, UNICOM
			tag.tag = "->122.8";
			return tag;
		}
	}

	// Not taxiing or taking off, ignore it
	return tag;
}

tagInfo CDelHelX_Tags::GetPushStartHelperTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
	tagInfo tag;
	tag.color = TAG_COLOR_GREEN;

	auto fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);
	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
	{
		// Airport not in config, so ignore it
		return tag;
	}

	std::string groundState = fp.GetGroundState();
	if (!groundState.empty())
	{
		// Aircraft is now moving, so we can remove the tag
		return tag;
	}

	std::string rwy = fpd.GetDepartureRwy();
	if (!this->noChecks && rwy.empty())
	{
		tag.tag = "!RWY";
		tag.color = TAG_COLOR_RED;

		return tag;
	}

	auto cad = fp.GetControllerAssignedData();
	std::string assignedSquawk = cad.GetSquawk();
	std::string currentSquawk = rt.GetPosition().GetSquawk();

	if (this->noChecks && assignedSquawk.empty())
	{
		assignedSquawk = "2000";
	}

	if (assignedSquawk.empty())
	{
		tag.tag = "!ASSR";
		tag.color = TAG_COLOR_RED;

		return tag;
	}

	bool clearanceFlag = fp.GetClearenceFlag();
	if (!this->noChecks && !clearanceFlag)
	{
		tag.tag = "!CLR";
		tag.color = TAG_COLOR_RED;

		return tag;
	}

	if (assignedSquawk != currentSquawk)
	{
		tag.tag = assignedSquawk;
		tag.color = TAG_COLOR_ORANGE;
	}

	// If I'm a controller, not Observer(1) and facility at least GND(3), I can push and start aircraft myself
	auto me = this->ControllerMyself();
	if (me.IsController() && me.GetRating() > 1 && me.GetFacility() >= 3)
	{
		tag.tag.empty() ? tag.tag = "OK" : tag.tag += "->OK";
		return tag;
	}

	if (this->radarScreen == nullptr)
	{
		return tag;
	}

	bool groundOnline = false;
	for (auto station : this->radarScreen->groundStations)
	{
		if (station.find(dep) != std::string::npos)
		{
			groundOnline = true;
			continue;
		}
	}

	if (groundOnline || this->groundOverride)
	{
		EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();
		for (auto& geoGnd : airport->second.geoGndFreq)
		{
			u_int corners = geoGnd.second.lat.size();
			double lat[10], lon[10];
			std::copy(geoGnd.second.lat.begin(), geoGnd.second.lat.end(), lat);
			std::copy(geoGnd.second.lon.begin(), geoGnd.second.lon.end(), lon);

			if (PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
			{
				tag.tag += "->" + geoGnd.second.freq;
				return tag;
			}
		}

		// Didn't find any geo-based GND, so return default
		tag.tag += "->" + airport->second.gndFreq;
		return tag;
	}

	bool towerOnline = false;
	for (auto station : this->radarScreen->towerStations)
	{
		if (station.find(dep) != std::string::npos)
		{
			towerOnline = true;
			continue;
		}
	}

	if (towerOnline || this->towerOverride)
	{
		for (auto rwyFreq : airport->second.rwyTwrFreq)
		{
			if (rwy == rwyFreq.first)
			{
				tag.tag += "->" + rwyFreq.second;
				return tag;
			}
		}

		// Didn't find a runway specific tower, so return default
		tag.tag += "->" + airport->second.twrFreq;
		return tag;
	}

	std::string sid = fpd.GetSidName();
	for (auto station : this->radarScreen->approachStations)
	{
		if (station.find(dep) != std::string::npos)
		{
			// Search for SID-specific freq
			{
				std::string freq = airport->second.defaultAppFreq;
				for (auto& [f, sids] : airport->second.sidAppFreqs)
				{
					if (std::find(sids.begin(), sids.end(), sid) != sids.end())
					{
						freq = f;
						break;
					}
				}
				tag.tag += "->" + freq;
				return tag;
			}
		}
	}

	for (auto center : airport->second.ctrStations)
	{
		for (auto station : this->radarScreen->centerStations)
		{
			if (station.first.find(center) != std::string::npos)
			{
				tag.tag += "->" + station.second;
				return tag;
			}
		}
	}

	// Nothing online, UNICOM
	tag.tag += "->122.8";
	return tag;
}

tagInfo CDelHelX_Tags::GetTaxiOutTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
	tagInfo tag;

	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);

	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
		return tag;

	EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();
	std::string groundState = fp.GetGroundState();

	if (groundState.empty() || groundState == "STUP")
	{
		bool isTaxiOut = false;
		for (auto& taxiOut : airport->second.taxiOutStands)
		{
			u_int corners = taxiOut.second.lat.size();
			double lat[10], lon[10];
			std::copy(taxiOut.second.lat.begin(), taxiOut.second.lat.end(), lat);
			std::copy(taxiOut.second.lon.begin(), taxiOut.second.lon.end(), lon);

			if (CDelHelX_Tags::PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
			{
				isTaxiOut = true;
				continue;
			}
		}

		if (isTaxiOut)
		{
			tag.tag = "T";
			tag.color = TAG_COLOR_GREEN;
		}
		else
		{
			tag.tag = groundState.empty() ? "P" : "";
		}
	}

	return tag;
}

tagInfo CDelHelX_Tags::GetNewQnhTag(EuroScopePlugIn::CFlightPlan& fp)
{
	tagInfo tag;
	std::string callSign = fp.GetCallsign();

	EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
	this->flightStripAnnotation[callSign] = fpcad.GetFlightStripAnnotation(8);
	if (!this->flightStripAnnotation[callSign].empty() && this->flightStripAnnotation[callSign][0] == 'Q')
	{
		tag.tag = "X";
		tag.color = TAG_COLOR_ORANGE;
	}
	else
	{
		tag.tag = "";
	}

	return tag;
}

tagInfo CDelHelX_Tags::GetSameSidTag(EuroScopePlugIn::CFlightPlan& fp)
{
	tagInfo tag;

	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);
	std::string rwy = fpd.GetDepartureRwy();
	std::string sid = fpd.GetSidName();

	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
		return tag;

	if (!sid.empty() && sid.length() > 2) {
		auto sidKey = sid.substr(0, sid.length() - 2);
		auto sidDesignator = sid.substr(sid.length() - 2);

		tag.color = TAG_COLOR_WHITE;

		// Extend night SIDs
		auto nightIt = airport->second.nightTimeSids.find(sidKey);
		if (nightIt != airport->second.nightTimeSids.end())
		{
			sid = nightIt->second + sidDesignator;
		}

		tag.tag = sid;

		auto rwyIt = airport->second.runways.find(rwy);
		if (rwyIt != airport->second.runways.end())
		{
			auto colorIt = rwyIt->second.sidColors.find(sidKey);
			if (colorIt != rwyIt->second.sidColors.end())
			{
				tag.color = ColorFromString(colorIt->second);
			}
		}
	}

	return tag;
}

tagInfo CDelHelX_Tags::GetTakeoffTimerTag(EuroScopePlugIn::CFlightPlan& fp)
{
	tagInfo tag;
	std::string callSign = fp.GetCallsign();

	if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
	{
		ULONGLONG now = GetTickCount64();
		auto seconds = (now - this->twrSameSID_flightPlans.at(callSign)) / 1000;

		auto minutes = seconds / 60;
		seconds = seconds % 60;
		auto leadingSeconds = seconds <= 9 ? "0" : "";

		tag.tag = std::to_string(minutes) + ":" + leadingSeconds + std::to_string(seconds);
	}

	return tag;
}

tagInfo CDelHelX_Tags::GetTakeoffDistanceTag(EuroScopePlugIn::CFlightPlan& fp)
{
	tagInfo tag;

	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);
	std::string rwy = fpd.GetDepartureRwy();
	auto position = fp.GetCorrelatedRadarTarget().GetPosition().GetPosition();
	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
		return tag;
	auto distance = DistanceFromRunwayThreshold(rwy, position, airport->second.runways);
	std::string num_text = std::to_string(distance);
	std::string rounded = num_text.substr(0, num_text.find('.') + 3);
	if (distance < 10.0)
	{
		rounded = "0" + rounded;
	}

	tag.tag = rounded;
	return tag;
}

tagInfo CDelHelX_Tags::GetAssignedRunwayTag(EuroScopePlugIn::CFlightPlan& fp)
{
	tagInfo tag;

	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string rwy = fpd.GetDepartureRwy();

	tag.tag = rwy;
	return tag;
}

tagInfo CDelHelX_Tags::GetTttTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
	tagInfo tag;
	std::string callSign = fp.GetCallsign();

	auto it = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
		[&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
	if (it != this->ttt_flightPlans.end())
	{
		auto position = rt.GetPosition().GetPosition();
		auto speed = rt.GetGS();

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
			tag.tag = std::string(buf);
			if (totalSeconds > 120)
				tag.color = TAG_COLOR_GREEN;
			else if (totalSeconds > 60)
				tag.color = TAG_COLOR_YELLOW;
			else
				tag.color = TAG_COLOR_RED;
		}
	}

	return tag;
}

tagInfo CDelHelX_Tags::GetInboundNmTag(EuroScopePlugIn::CFlightPlan& fp)
{
	tagInfo tag;
	std::string callSign = fp.GetCallsign();

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
			char buf[16] = {};
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
						if (gap > 3.0)
							tag.color = TAG_COLOR_GREEN;
						else if (gap > 2.5)
							tag.color = TAG_COLOR_YELLOW;
						else
							tag.color = TAG_COLOR_RED;
						break;
					}
				}
			}
			tag.tag = std::string(buf);
		}
	}

	return tag;
}

tagInfo CDelHelX_Tags::GetSuggestedVacateTag(EuroScopePlugIn::CFlightPlan& fp)
{
	tagInfo tag;
	std::string callSign = fp.GetCallsign();

	auto standIt = this->standAssignment.find(callSign);
	if (standIt == this->standAssignment.end())
		return tag;

	std::string stand = standIt->second;

	// Find this aircraft in the sorted inbound list
	auto myPlan = std::find_if(this->ttt_flightPlans.begin(), this->ttt_flightPlans.end(),
		[&callSign](const auto& entry) { return entry.first.rfind(callSign, 0) == 0; });
	if (myPlan == this->ttt_flightPlans.end())
		return tag;

	auto sortedIt = this->ttt_sortedByRunway.find(myPlan->second.designator);
	if (sortedIt == this->ttt_sortedByRunway.end())
		return tag;

	const auto& keys = sortedIt->second;
	auto myIdx = std::find(keys.begin(), keys.end(), myPlan->first);
	if (myIdx == keys.end())
		return tag;

	// Calculate gap to follower if one exists
	bool hasFollower = myIdx + 1 != keys.end();
	double gap = hasFollower
		? this->ttt_distanceToRunway.at(*(myIdx + 1)) - this->ttt_distanceToRunway.at(myPlan->first)
		: 0.0;

	// Look up runway vacate config
	std::string arr = fp.GetFlightPlanData().GetDestination();
	to_upper(arr);
	auto airportIt = this->airports.find(arr);
	if (airportIt == this->airports.end())
		return tag;

	auto rwyIt = airportIt->second.runways.find(myPlan->second.designator);
	if (rwyIt == airportIt->second.runways.end())
		return tag;

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
				tag.tag = vpName;
				tag.color = TAG_COLOR_WHITE;
				return tag;
			}
		}
	}

	return tag;
}

tagInfo CDelHelX_Tags::GetHoldingPointTag(EuroScopePlugIn::CFlightPlan& fp, int index)
{
	tagInfo tag;
	std::string callSign = fp.GetCallsign();

	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);
	std::string rwy = fpd.GetDepartureRwy();
	EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
	this->flightStripAnnotation[callSign] = fpcad.GetFlightStripAnnotation(8);
	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
		return tag;
	if (this->flightStripAnnotation[callSign].length() > 2 && MatchesRunwayHoldingPoint(rwy, this->flightStripAnnotation[callSign].substr(2), index, airport->second.runways))
	{
		tag.tag = this->flightStripAnnotation[callSign].substr(2);
		tag.color = TAG_COLOR_GREEN;

		if (this->flightStripAnnotation[callSign].substr(2).find('*') != std::string::npos)
			tag.color = TAG_COLOR_ORANGE;

		if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
		{
			tag.color = TAG_COLOR_DARKGREY;
		}
	}
	else
	{
		tag.tag = "";
	}

	return tag;
}

tagInfo CDelHelX_Tags::GetDepartureInfoTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
	tagInfo tag;
	std::string callSign = fp.GetCallsign();

	try
	{
		std::string depAirport = fp.GetFlightPlanData().GetOrigin();
		to_upper(depAirport);
		auto airport = this->airports.find(depAirport);
		if (airport == this->airports.end())
			return tag;
		std::string groundState = fp.GetGroundState();
		if (groundState == "TAXI" || groundState == "DEPA")
		{
			std::string rwy = fp.GetFlightPlanData().GetDepartureRwy();
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
						char wtc = fp.GetFlightPlanData().GetAircraftWtc();

						if (GetAircraftWeightCategoryRanking(departedWtc) > GetAircraftWeightCategoryRanking(wtc))
						{
							// Time based
							unsigned long secondsRequired = 120;

							this->flightStripAnnotation[lastDeparted_callSign] = lastDeparted_radarTarget.GetCorrelatedFlightPlan().GetControllerAssignedData().GetFlightStripAnnotation(8);
							this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
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
									tag.color = TAG_COLOR_GREEN;
									tag.tag = "OK";
									return tag;
								}

								if (secondsSinceDeparted + 30 > secondsRequired)
								{
									tag.color = TAG_COLOR_GREEN;
									tag.tag = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
									return tag;
								}

								if (secondsSinceDeparted + 45 > secondsRequired)
								{
									tag.color = TAG_COLOR_YELLOW;
									tag.tag = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
									return tag;
								}

								tag.color = TAG_COLOR_RED;
								tag.tag = std::to_string(secondsRequired - secondsSinceDeparted) + "s";
								return tag;
							}

							// Flight plan removed, either disconnected or out of range
							tag.color = TAG_COLOR_GREEN;
							tag.tag = "OK";
							return tag;
						}

						// Distance based
						std::string departedSID = lastDeparted_radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetSidName();
						std::string sid = fp.GetFlightPlanData().GetSidName();

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

						auto distanceBetween = rt.GetPosition().GetPosition().DistanceTo(lastDeparted_radarTarget.GetPosition().GetPosition());
						if (distanceBetween > distanceRequired)
						{
							tag.color = TAG_COLOR_GREEN;
							tag.tag = "OK";
							return tag;
						}

						if (distanceRequired <= 3.1 && distanceBetween > 1.3)
						{
							tag.color = TAG_COLOR_GREEN;
							std::string num_text = std::to_string(distanceRequired - distanceBetween);
							tag.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
							return tag;
						}

						if (distanceBetween > 3)
						{
							tag.color = TAG_COLOR_GREEN;
							std::string num_text = std::to_string(distanceRequired - distanceBetween);
							tag.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
							return tag;
						}

						if (distanceBetween > 2.5)
						{
							tag.color = TAG_COLOR_YELLOW;
							std::string num_text = std::to_string(distanceRequired - distanceBetween);
							tag.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
							return tag;
						}

						tag.color = TAG_COLOR_RED;
						std::string num_text = std::to_string(distanceRequired - distanceBetween);
						tag.tag = num_text.substr(0, num_text.find('.') + 3) + "nm";
						return tag;
					}
				}

				tag.color = TAG_COLOR_GREEN;
				tag.tag = "OK?";
			}
			else if (this->twrSameSID_lastDeparted.find(rwy) != this->twrSameSID_lastDeparted.end() && this->twrSameSID_lastDeparted[rwy] == callSign)
			{
				// This is the last aircraft that departed this runway
				tag.color = TAG_COLOR_ORANGE;
				tag.tag = rwy;
			}
		}
	}
	catch ([[maybe_unused]] const std::exception& ex)
	{
		tag.color = TAG_COLOR_RED;
		tag.tag = "ERR";
	}

	return tag;
}
