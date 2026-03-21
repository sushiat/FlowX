#include "pch.h"
#include "CDelHelX_Functions.h"

#include "helpers.h"

/// @brief Sets ONFREQ, ST-UP, or PUSH ground state depending on the aircraft's current stand position.
/// @param fp Currently selected flight plan.
/// @param rt Correlated radar target.
void CDelHelX_Functions::Func_OnFreq(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
	auto tag = this->GetPushStartHelperTag(fp, rt);
	if (tag.color == TAG_COLOR_RED)
	{
		return;
	}

	std::string dep = fp.GetFlightPlanData().GetOrigin();
	to_upper(dep);
	auto airport = this->airports.find(dep);

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

			if (CDelHelX_Functions::PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
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

/// @brief Clears the 'Q' flag from flight-strip annotation slot 8 and syncs to other controllers.
/// @param fp Currently selected flight plan.
void CDelHelX_Functions::Func_ClearNewQnh(EuroScopePlugIn::CFlightPlan& fp)
{
	std::string callSign = fp.GetCallsign();
	if (!this->flightStripAnnotation[callSign].empty())
	{
		this->flightStripAnnotation[callSign][0] = ' ';
		fp.GetControllerAssignedData().SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
		this->PushToOtherControllers(fp);
	}
}

/// @brief Assigns the standard holding point for slot @p index to flight-strip annotation slot 8.
/// @param fp Currently selected flight plan.
/// @param index Slot index (1-3 for HP1-HP3).
void CDelHelX_Functions::Func_AssignHp(EuroScopePlugIn::CFlightPlan& fp, int index)
{
	std::string callSign = fp.GetCallsign();
	std::string dep = fp.GetFlightPlanData().GetOrigin();
	to_upper(dep);
	std::string rwy = fp.GetFlightPlanData().GetDepartureRwy();
	auto airport = this->airports.find(dep);
	this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], GetRunwayHoldingPoint(rwy, index, airport->second.runways));
	fp.GetControllerAssignedData().SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
	this->PushToOtherControllers(fp);
}

/// @brief Requests the holding point for slot @p index by appending '*' to indicate the pilot must confirm.
/// @param fp Currently selected flight plan.
/// @param index Slot index (1-3 for HP1-HP3).
void CDelHelX_Functions::Func_RequestHp(EuroScopePlugIn::CFlightPlan& fp, int index)
{
	std::string callSign = fp.GetCallsign();
	std::string dep = fp.GetFlightPlanData().GetOrigin();
	to_upper(dep);
	std::string rwy = fp.GetFlightPlanData().GetDepartureRwy();
	auto airport = this->airports.find(dep);
	this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], GetRunwayHoldingPoint(rwy, index, airport->second.runways) + "*");
	fp.GetControllerAssignedData().SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
	this->PushToOtherControllers(fp);
}

/// @brief Opens a popup list of assignable holding points so the controller can pick a non-standard HP.
/// @param fp Currently selected flight plan.
/// @param Pt Screen position at which to anchor the popup.
void CDelHelX_Functions::Func_AssignHpo(EuroScopePlugIn::CFlightPlan& fp, POINT Pt)
{
	RECT area;
	area.left = Pt.x;
	area.right = Pt.x + 100;
	area.top = Pt.y;
	area.bottom = Pt.y + 100;
	this->OpenPopupList(area, "Assign HP", 1);

	std::string dep = fp.GetFlightPlanData().GetOrigin();
	to_upper(dep);
	std::string rwy = fp.GetFlightPlanData().GetDepartureRwy();
	auto airport = this->airports.find(dep);
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

/// @brief Opens a popup list of assignable holding points with each entry starred to denote a request.
/// @param fp Currently selected flight plan.
/// @param Pt Screen position at which to anchor the popup.
void CDelHelX_Functions::Func_RequestHpo(EuroScopePlugIn::CFlightPlan& fp, POINT Pt)
{
	RECT area;
	area.left = Pt.x;
	area.right = Pt.x + 100;
	area.top = Pt.y;
	area.bottom = Pt.y + 100;
	this->OpenPopupList(area, "Request HP", 1);

	std::string dep = fp.GetFlightPlanData().GetOrigin();
	to_upper(dep);
	std::string rwy = fp.GetFlightPlanData().GetDepartureRwy();
	auto airport = this->airports.find(dep);
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

/// @brief Writes the user-selected holding-point name from the popup into flight-strip annotation slot 8.
/// @param fp Currently selected flight plan.
/// @param sItemString The holding-point name (possibly starred) chosen from the popup.
void CDelHelX_Functions::Func_HpoListselect(EuroScopePlugIn::CFlightPlan& fp, const char* sItemString)
{
	std::string callSign = fp.GetCallsign();
	this->flightStripAnnotation[callSign] = AppendHoldingPointToFlightStripAnnotation(this->flightStripAnnotation[callSign], sItemString);
	fp.GetControllerAssignedData().SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
	this->PushToOtherControllers(fp);
}

/// @brief Sets the LINEUP ground state via a momentary scratch-pad toggle.
/// @param fp Currently selected flight plan.
void CDelHelX_Functions::Func_LineUp(EuroScopePlugIn::CFlightPlan& fp)
{
	std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
	fp.GetControllerAssignedData().SetScratchPadString("LINEUP");
	fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
}

/// @brief Sets the DEPA ground state and starts tracking the flight plan.
/// @param fp Currently selected flight plan.
void CDelHelX_Functions::Func_TakeOff(EuroScopePlugIn::CFlightPlan& fp)
{
	std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
	fp.GetControllerAssignedData().SetScratchPadString("DEPA");
	fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());

	fp.StartTracking();
}

/// @brief Initiates a handoff to the next controller and marks the flight strip as transferred.
/// @param fp Currently selected flight plan.
/// @note Tower controllers search for a SID-specific approach frequency first; falls back to first APP, then drops tracking.
void CDelHelX_Functions::Func_TransferNext(EuroScopePlugIn::CFlightPlan& fp)
{
	std::string callSign = fp.GetCallsign();
	std::string targetController = fp.GetCoordinatedNextController();
	std::string dep = fp.GetFlightPlanData().GetOrigin();
	to_upper(dep);
	if (!targetController.empty() && this->ControllerMyself().GetFacility() >= 4)
	{
		fp.InitiateHandoff(targetController.c_str());
	}
	else
	{
		// If we are TWR, check if any APP station is online, and transfer there
		if (this->ControllerMyself().GetFacility() == 4)
		{
			if (this->radarScreen != nullptr)
			{
				// Check if we can find SID specific freq
				auto airport = this->airports.find(dep);
				bool wasAssigned = false;
				if (airport != this->airports.end()) {
					std::string sid = fp.GetFlightPlanData().GetSidName();
					std::string freq = airport->second.defaultAppFreq;
					for (auto& [f, sids] : airport->second.sidAppFreqs)
					{
						if (std::find(sids.begin(), sids.end(), sid) != sids.end())
						{
							freq = f;
							break;
						}
					}

					for (auto station : this->radarScreen->approachStations)
					{
						if (station.first.find(dep) != std::string::npos && station.second == freq)
						{
							fp.InitiateHandoff(station.first.c_str());
							wasAssigned = true;
							break;
						}
					}
				}

				// No SID specific freq, just look for any APP station and pick the first one
				for (auto station : this->radarScreen->approachStations)
				{
					if (station.first.find(dep) != std::string::npos)
					{
						fp.InitiateHandoff(station.first.c_str());
						wasAssigned = true;
						break;
					}
				}

				if (!wasAssigned)
				{
					fp.EndTracking();
				}
			} 
			else
			{
				fp.EndTracking();
			}
		}
		else {
			fp.EndTracking();
		}
	}

	if (this->flightStripAnnotation[callSign].length() > 1)
	{
		this->flightStripAnnotation[callSign][1] = 'T';
	}
	else
	{
		this->flightStripAnnotation[callSign] = " T";
	}
	fp.GetControllerAssignedData().SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
	this->PushToOtherControllers(fp);
}

/// @brief Ends tracking of the inbound and triggers TopSky's highlight function.
/// @param fp Currently selected flight plan.
/// @param radarScreenInstance Active radar screen used to call the TopSky tag function.
void CDelHelX_Functions::Func_ClrdToLand(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance)
{
	std::string callSign = fp.GetCallsign();
	if (fp.GetTrackingControllerIsMe())
	{
		fp.EndTracking();
	}
	radarScreenInstance->StartTagFunction(callSign.c_str(), nullptr, 0, "S-Highlight", TOPSKY_PLUGIN_NAME, 4, POINT(), RECT());
}

/// @brief Handles a missed approach: takes tracking, assigns 5000 ft, highlights in TopSky, and sets MISAP scratch.
/// @param fp Currently selected flight plan.
/// @param radarScreenInstance Active radar screen used to call the TopSky tag function.
void CDelHelX_Functions::Func_MissedApp(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance)
{
	if (!fp.GetTrackingControllerIsMe())
	{
		fp.StartTracking();
	}
	fp.GetControllerAssignedData().SetClearedAltitude(5000);

	std::string callSign = fp.GetCallsign();
	radarScreenInstance->StartTagFunction(callSign.c_str(), nullptr, 0, "S-Highlight", TOPSKY_PLUGIN_NAME, 4, POINT(), RECT());
	std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
	fp.GetControllerAssignedData().SetScratchPadString((scratchBackup + "MISAP_").c_str());
}

/// @brief Triggers the Ground Radar plugin's automatic stand assignment function.
/// @param fp Currently selected flight plan.
/// @param radarScreenInstance Active radar screen used to call the Ground Radar tag function.
void CDelHelX_Functions::Func_StandAuto(EuroScopePlugIn::CFlightPlan& fp, RadarScreen* radarScreenInstance)
{
	std::string callSign = fp.GetCallsign();
	radarScreenInstance->StartTagFunction(callSign.c_str(), "GRplugin", 0, "   Auto   ", GROUNDRADAR_PLUGIN_NAME, 2, POINT(), RECT());
}
