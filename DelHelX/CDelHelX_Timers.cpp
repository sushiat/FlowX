#include "pch.h"
#include "CDelHelX_Timers.h"

#include "helpers.h"
#include "date/tz.h"

void CDelHelX_Timers::CheckAirportNAPReminder()
{
	for (auto& airport : this->airports)
	{
		if (airport.second.nap_reminder.enabled && !airport.second.nap_reminder.triggered)
		{
			try
			{
				std::ostringstream timeStream;
				timeStream << date::make_zoned(airport.second.nap_reminder.tzone, std::chrono::system_clock::now());
				std::string timeString = timeStream.str();

				std::vector<std::string> timeSplit = split(timeString, ' ');
				if (timeSplit.size() == 3)
				{
					auto tod = timeSplit[1];
					std::vector<std::string> todSplit = split(tod, ':');
					if (todSplit.size() == 3)
					{
						int hours = atoi(todSplit[0].c_str());
						int minutes = atoi(todSplit[1].c_str());

						if ((hours == airport.second.nap_reminder.hour && minutes >= airport.second.nap_reminder.minute) || hours > airport.second.nap_reminder.hour)
						{
							airport.second.nap_reminder.triggered = true;

							Beep(1568, 300);
							MessageBox(nullptr, ("What's the NAP procedure for " + airport.first + " tonight?").c_str(), "DelHelX Plugin", MB_OK | MB_ICONQUESTION | MB_TOPMOST);
						}
					}
				}
			}
			catch (std::exception& e)
			{
				this->LogMessage("Error processing NAP-reminder for airport " + airport.first + ". Error: " + std::string(e.what()), "Config");
			}
		}
	}
}

void CDelHelX_Timers::UpdateTTTInbounds()
{
	if (this->GetConnectionType() == EuroScopePlugIn::CONNECTION_TYPE_NO)
	{
		if (!this->ttt_flightPlans.empty())
		{
			for (auto ttt_fp : this->ttt_flightPlans)
			{
				auto fp = this->FlightPlanSelect(ttt_fp.first.c_str());
				this->tttInbound.RemoveFpFromTheList(fp);
			}

			this->ttt_flightPlans.clear();
		}

		return;
	}

	for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
	{
		EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
		EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
		std::string callSign = fp.GetCallsign();

		if (!pos.IsValid())
		{
			this->tttInbound.RemoveFpFromTheList(fp);
			continue;
		}

		for (auto airport = this->airports.begin(); airport != this->airports.end(); ++airport)
		{
			for (auto rwy = airport->second.runways.begin(); rwy != airport->second.runways.end(); ++rwy)
			{
				std::string rwyCallsign = callSign + rwy->second.designator;
				std::string arrRwyDigits = rwy->second.designator;
				arrRwyDigits.erase(std::remove_if(arrRwyDigits.begin(), arrRwyDigits.end(),
					[](char c) { return !std::isdigit(c); }), arrRwyDigits.end());
				int arrRwyHdg = arrRwyDigits.empty() ? -1 : std::stoi(arrRwyDigits);

				// If we can't determine the arrival runway heading, we can't determine if it's an inbound or not, so skip it
				if (arrRwyHdg == -1)
				{
					if (this->ttt_flightPlans.find(rwyCallsign) != this->ttt_flightPlans.end())
					{
						this->tttInbound.RemoveFpFromTheList(fp);
						this->ttt_flightPlans.erase(rwyCallsign);
					}
					continue;
				}

				// Check if the flight plan needs to be added to the list
				auto position = pos.GetPosition();
				auto distance = DistanceFromRunwayThreshold(rwy->second.designator, position, airport->second.runways);
				auto direction = DirectionFromRunwayThreshold(rwy->second.designator, position, airport->second.runways);
				auto pressAlt = pos.GetPressureAltitude();
				auto heading = pos.GetReportedHeading();
				int hdgDiff = std::abs(heading - arrRwyHdg * 10);
				if (hdgDiff > 180) hdgDiff = 360 - hdgDiff;
				double approachDir = std::fmod(arrRwyHdg * 10 + 180.0, 360.0);
				double dirDiff = std::abs(direction - approachDir);
				if (dirDiff > 180.0) dirDiff = 360.0 - dirDiff;

				int depElevation = airport->second.fieldElevation;
				if (pressAlt > depElevation + 50 && pressAlt < depElevation + 50 + 7000 && hdgDiff <= 30 && distance < 20 && dirDiff <= 5.0)
				{
					this->ttt_distanceToRunway[rwyCallsign] = distance;
					std::string trackingControllerId = fp.GetTrackingControllerId();

					if ((fp.GetTrackingControllerIsMe() || trackingControllerId.empty()) && this->standAssignment.find(callSign) == this->standAssignment.end())
					{
						// Trigger auto stand assignment in GroundRadar plugin
						this->radarScreen->StartTagFunction(callSign.c_str(), "GRplugin", 0, "   Auto   ", GROUNDRADAR_PLUGIN_NAME, 2, POINT(), RECT());
					}

					if (this->ttt_flightPlans.find(rwyCallsign) == this->ttt_flightPlans.end())
					{
						this->tttInbound.AddFpToTheList(fp);
						this->ttt_flightPlans.emplace(rwyCallsign, rwy->second);
					}
				}
				else
				{
					if (this->ttt_flightPlans.find(rwyCallsign) != this->ttt_flightPlans.end())
					{
						this->tttInbound.RemoveFpFromTheList(fp);
						this->ttt_flightPlans.erase(rwyCallsign);
					}
					this->ttt_distanceToRunway.erase(rwyCallsign);
				}
			}
		}
	}

	// Rebuild sorted-by-runway index
	this->ttt_sortedByRunway.clear();
	for (auto& [key, dist] : this->ttt_distanceToRunway)
	{
		auto planIt = this->ttt_flightPlans.find(key);
		if (planIt != this->ttt_flightPlans.end())
			this->ttt_sortedByRunway[planIt->second.designator].push_back(key);
	}
	for (auto& [designator, keys] : this->ttt_sortedByRunway)
	{
		std::sort(keys.begin(), keys.end(), [this](const std::string& a, const std::string& b) {
			return this->ttt_distanceToRunway.at(a) < this->ttt_distanceToRunway.at(b);
			});
	}
}

void CDelHelX_Timers::UpdateTowerSameSID()
{
	if (this->GetConnectionType() == EuroScopePlugIn::CONNECTION_TYPE_NO)
	{
		if (!this->twrSameSID_flightPlans.empty())
		{
			for (auto twr_fp : this->twrSameSID_flightPlans)
			{
				auto fp = this->FlightPlanSelect(twr_fp.first.c_str());
				this->twrSameSID.RemoveFpFromTheList(fp);
			}

			this->twrSameSID_flightPlans.clear();
		}

		return;
	}

	for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
	{
		EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
		EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
		std::string callSign = fp.GetCallsign();

		if (!pos.IsValid() || !fp.IsValid())
		{
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
			{
				this->twrSameSID.RemoveFpFromTheList(fp);
				this->twrSameSID_flightPlans.erase(callSign);
			}

			continue;
		}

		std::string dep = fp.GetFlightPlanData().GetOrigin();
		to_upper(dep);

		std::string arr = fp.GetFlightPlanData().GetDestination();
		to_upper(arr);

		// Skip aircraft without a valid flight plan (no departure/destination airport)
		if (dep.empty() || arr.empty()) {
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
			{
				this->twrSameSID.RemoveFpFromTheList(fp);
				this->twrSameSID_flightPlans.erase(callSign);
				this->dep_previousAircraft.erase(callSign);
				this->dep_sid.erase(callSign);
				this->dep_wtc.erase(callSign);
				this->dep_prevTakeoffOffset.erase(callSign);
				this->dep_timeRequired.erase(callSign);
				this->dep_sequenceNumber.erase(callSign);
			}

			continue;
		}

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
		{
			// Airport not in config
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
			{
				this->twrSameSID.RemoveFpFromTheList(fp);
				this->twrSameSID_flightPlans.erase(callSign);
				this->dep_previousAircraft.erase(callSign);
				this->dep_sid.erase(callSign);
				this->dep_wtc.erase(callSign);
				this->dep_prevTakeoffOffset.erase(callSign);
				this->dep_timeRequired.erase(callSign);
				this->dep_sequenceNumber.erase(callSign);
			}

			continue;
		}

		// Check if the flight plan needs to be added to the list
		std::string groundState = fp.GetGroundState();
		auto pressAlt = pos.GetPressureAltitude();
		int depElevation = airport->second.fieldElevation;
		if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < depElevation + 50 && this->twrSameSID_flightPlans.find(callSign) == this->twrSameSID_flightPlans.end())
		{
			this->twrSameSID.AddFpToTheList(fp);
			this->twrSameSID_flightPlans.emplace(callSign, 0);
		}

		// Check if we need to remove the flight plan because of ground state
		if (!(groundState == "TAXI" || groundState == "DEPA") && this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
		{
			this->twrSameSID.RemoveFpFromTheList(fp);
			this->twrSameSID_flightPlans.erase(callSign);
			this->dep_previousAircraft.erase(callSign);
			this->dep_sid.erase(callSign);
			this->dep_wtc.erase(callSign);
			this->dep_prevTakeoffOffset.erase(callSign);
			this->dep_timeRequired.erase(callSign);
			this->dep_sequenceNumber.erase(callSign);
		}

		// Check if aircraft started takeoff roll, press Alt > field elevation + 50 feet
		if (groundState == "DEPA" && pressAlt >= depElevation + 50 && this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
		{
			if (this->twrSameSID_flightPlans.at(callSign) == 0)
			{
				std::string depRwy = fp.GetFlightPlanData().GetDepartureRwy();
				this->twrSameSID_flightPlans[callSign] = GetTickCount64();
				this->dep_sid[callSign] = fp.GetFlightPlanData().GetSidName();
				this->dep_wtc[callSign] = fp.GetFlightPlanData().GetAircraftWtc();
				this->dep_sequenceNumber[callSign] = ++this->dep_sequenceCounter;
				auto prevDepIt = this->twrSameSID_lastDeparted.find(depRwy);
				if (prevDepIt != this->twrSameSID_lastDeparted.end())
				{
					const std::string& prevCallSign = prevDepIt->second;
					this->dep_previousAircraft[callSign] = prevCallSign;

					// Compute time offset between consecutive takeoffs
					auto prevTakeoffIt = this->twrSameSID_flightPlans.find(prevCallSign);
					if (prevTakeoffIt != this->twrSameSID_flightPlans.end() && prevTakeoffIt->second > 0)
						this->dep_prevTakeoffOffset[callSign] = (this->twrSameSID_flightPlans.at(callSign) - prevTakeoffIt->second) / 1000;

					// Compute required time separation from holding points
					int timeRequired = 120;
					this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
					auto prevFp = this->FlightPlanSelect(prevCallSign.c_str());
					if (prevFp.IsValid())
						this->flightStripAnnotation[prevCallSign] = prevFp.GetControllerAssignedData().GetFlightStripAnnotation(8);
					if (this->flightStripAnnotation.find(prevCallSign) != this->flightStripAnnotation.end() &&
						this->flightStripAnnotation[callSign].length() > 2 && this->flightStripAnnotation[prevCallSign].length() > 2)
					{
						std::string prevHP = this->flightStripAnnotation[prevCallSign].substr(2);
						std::string hp = this->flightStripAnnotation[callSign].substr(2);
						if (!IsSameHoldingPoint(prevHP, hp, airport->second.runways))
							timeRequired += 60;
					}
					this->dep_timeRequired[callSign] = timeRequired;
				}
				this->twrSameSID_lastDeparted[depRwy] = callSign;
			}
		}

		// Check if the aircraft has departed and is further than 15nm away or more than 4 minutes have passed since takeoff
		if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
		{
			if (this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				ULONGLONG now = GetTickCount64();
				auto seconds = (now - this->twrSameSID_flightPlans.at(callSign)) / 1000;
				if (seconds > 4 * 60)
				{
					this->flightStripAnnotation.erase(callSign);
					fp.GetControllerAssignedData().SetFlightStripAnnotation(8, "");
					this->PushToOtherControllers(fp);
					this->twrSameSID.RemoveFpFromTheList(fp);
					this->twrSameSID_flightPlans.erase(callSign);
					this->dep_previousAircraft.erase(callSign);
					this->dep_sid.erase(callSign);
					this->dep_wtc.erase(callSign);
					continue;
				}
			}

			EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
			std::string rwy = fpd.GetDepartureRwy();
			auto position = pos.GetPosition();
			auto distance = DistanceFromRunwayThreshold(rwy, position, airport->second.runways);

			if (distance >= 15)
			{
				this->flightStripAnnotation.erase(callSign);
				fp.GetControllerAssignedData().SetFlightStripAnnotation(8, "");
				this->PushToOtherControllers(fp);
				this->twrSameSID.RemoveFpFromTheList(fp);
				this->twrSameSID_flightPlans.erase(callSign);
				this->dep_previousAircraft.erase(callSign);
				this->dep_sid.erase(callSign);
				this->dep_wtc.erase(callSign);
				this->dep_prevTakeoffOffset.erase(callSign);
				this->dep_timeRequired.erase(callSign);
				this->dep_sequenceNumber.erase(callSign);
			}
		}
	}
}

void CDelHelX_Timers::UpdateRadarTargetDepartureInfo()
{
	if (this->radarScreen == nullptr)
		return;

	if (this->GetConnectionType() == EuroScopePlugIn::CONNECTION_TYPE_NO)
	{
		if (!this->radarScreen->radarTargetDepartureInfos.empty())
		{
			this->radarScreen->radarTargetDepartureInfos.clear();
		}

		return;
	}

	if (this->ControllerMyself().GetFacility() >= 3)
	{
		for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
		{
			EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
			EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
			EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();

			if (!pos.IsValid() || !fp.IsValid())
			{
				continue;
			}

			std::string dep = fp.GetFlightPlanData().GetOrigin();
			to_upper(dep);

			std::string arr = fp.GetFlightPlanData().GetDestination();
			to_upper(arr);

			// Skip aircraft without a valid flight plan (no departure/destination airport)
			if (dep.empty() || arr.empty())
			{
				continue;
			}

			auto airport = this->airports.find(dep);
			if (airport == this->airports.end())
			{
				continue;
			}

			std::string cs = rt.GetCallsign();
			std::string groundState = fp.GetGroundState();
			auto pressAlt = pos.GetPressureAltitude();
			auto groundSpeed = pos.GetReportedGS();
			int depElevation = airport->second.fieldElevation;
			if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < depElevation + 50 && groundSpeed < 40)
			{
				// Add/update departure info
				char itemString[16];
				int colorCode;
				COLORREF colorRef;
				double fontSize;
				OnGetTagItem(fp, rt, TAG_ITEM_DEPARTURE_INFO, 0, itemString, &colorCode, &colorRef, &fontSize);
				std::string dep_info = std::string(itemString);

				COLORREF sideColorRef;
				OnGetTagItem(fp, rt, TAG_ITEM_SAMESID, 0, itemString, &colorCode, &sideColorRef, &fontSize);


				auto hp_color = TAG_COLOR_GREEN;
				std::string hp;
				this->flightStripAnnotation[cs] = fpcad.GetFlightStripAnnotation(8);
				if (this->flightStripAnnotation[cs].length() > 2)
				{
					hp = this->flightStripAnnotation[cs].substr(2);
					if (this->flightStripAnnotation[cs].substr(2).find('*') != std::string::npos)
					{
						hp_color = TAG_COLOR_ORANGE;
					}
				}

				auto me = this->ControllerMyself();
				if (me.IsController() && me.GetRating() > 1 && me.GetFacility() <= 3) {
					if (this->flightStripAnnotation[cs].length() < 2 || this->flightStripAnnotation[cs][1] != 'T')
					{
						dep_info += ",T";
					}
				}

				auto findDepInfo = this->radarScreen->radarTargetDepartureInfos.find(cs);
				if (findDepInfo == this->radarScreen->radarTargetDepartureInfos.end())
				{
					depInfo departureInfo;
					departureInfo.dep_info = dep_info;
					departureInfo.dep_color = colorRef;
					departureInfo.pos.x = -1;
					departureInfo.pos.y = -1;
					departureInfo.dragX = 0;
					departureInfo.dragY = 0;
					departureInfo.lastDrag.x = -1;
					departureInfo.lastDrag.y = -1;
					departureInfo.hp_info = hp;
					departureInfo.hp_color = hp_color;
					departureInfo.sid_color = sideColorRef;
					this->radarScreen->radarTargetDepartureInfos.insert_or_assign(cs, departureInfo);
				}
				else
				{
					findDepInfo->second.dep_info = dep_info;
					findDepInfo->second.dep_color = colorRef;
					findDepInfo->second.hp_info = hp;
					findDepInfo->second.hp_color = hp_color;
					findDepInfo->second.sid_color = sideColorRef;
				}
			}
			else
			{
				// Remove departure info
				auto findCallSign = this->radarScreen->radarTargetDepartureInfos.find(cs);
				if (findCallSign != this->radarScreen->radarTargetDepartureInfos.end())
				{
					this->radarScreen->radarTargetDepartureInfos.erase(findCallSign);
				}
			}
		}
	}
}

void CDelHelX_Timers::AutoUpdateDepartureHoldingPoints()
{
	for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt))
	{
		EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
		EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		std::string callSign = fp.GetCallsign();

		if (!pos.IsValid() || !fp.IsValid())
		{
			continue;
		}

		std::string dep = fp.GetFlightPlanData().GetOrigin();
		to_upper(dep);

		std::string arr = fp.GetFlightPlanData().GetDestination();
		to_upper(arr);

		// Skip aircraft without a valid flight plan (no departure/destination airport)
		if (dep.empty() || arr.empty())
		{
			continue;
		}

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
		{
			continue;
		}

		EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();
		std::string groundState = fp.GetGroundState();
		auto pressAlt = pos.GetPressureAltitude();
		auto groundSpeed = pos.GetReportedGS();

		std::string before = this->flightStripAnnotation[callSign];
		int depElevation = airport->second.fieldElevation;
		if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < depElevation + 50 && groundSpeed < 30)
		{
			auto rwyIt = airport->second.runways.find(rwy);
			if (rwyIt != airport->second.runways.end())
			{
				for (auto& [hpName, hpData] : rwyIt->second.holdingPoints)
				{
					u_int corners = static_cast<u_int>(hpData.lat.size());
					double polyX[10], polyY[10];
					std::copy(hpData.lon.begin(), hpData.lon.end(), polyX);
					std::copy(hpData.lat.begin(), hpData.lat.end(), polyY);

					if (CDelHelX_Timers::PointInsidePolygon(static_cast<int>(corners), polyX, polyY, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
					{
						this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], hpName);
					}
				}
			}

			if (before != this->flightStripAnnotation[callSign])
			{
				fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
				this->PushToOtherControllers(fp);
			}
		}
	}
}
