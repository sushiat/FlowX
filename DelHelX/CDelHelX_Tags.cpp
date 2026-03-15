#include "pch.h"
#include "CDelHelX_Tags.h"

#include "helpers.h"

tagInfo CDelHelX_Tags::GetPushStartHelperTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
	tagInfo tag;
	std::string callSign = fp.GetCallsign();
	std::string groundState = fp.GetGroundState();
	EuroScopePlugIn::CController me = this->ControllerMyself();
	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);
	std::string rwy = fpd.GetDepartureRwy();
	std::string sid = fpd.GetSidName();

	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
	{
		// Airport not in config, so ignore it
		return tag;
	}

	if (!groundState.empty())
	{
		// Check if we passed the aircraft to the next frequency
		this->flightStripAnnotation[callSign] = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
		if (this->flightStripAnnotation[callSign].length() > 1 && this->flightStripAnnotation[callSign][1] == 'T')
		{
			tag.color = TAG_COLOR_DARKGREY;
		}

		if (groundState == "TAXI" || groundState == "DEPA") {
			// Find next frequency
			if (me.IsController() && me.GetRating() > 1 && me.GetFacility() >= 3 && me.GetFacility() <= 4)
			{
				// Only show tower to ground, but not to tower
				if (me.GetFacility() == 3) {
					bool towerOnline = false;
					if (this->radarScreen == nullptr)
					{
						return tag;
					}

					for (auto station : this->radarScreen->towerStations)
					{
						if (station.find(dep) != std::string::npos)
						{
							towerOnline = true;
							continue;
						}

						if (this->radarScreen == nullptr)
						{
							return tag;
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
				}

				if (this->radarScreen == nullptr)
				{
					return tag;
				}

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

					if (this->radarScreen == nullptr)
					{
						return tag;
					}
				}

				for (auto center : airport->second.ctrStations)
				{
					if (this->radarScreen == nullptr)
					{
						return tag;
					}

					for (auto station : this->radarScreen->centerStations)
					{
						if (station.first.find(center) != std::string::npos)
						{
							tag.tag += "->" + station.second;
							return tag;
						}

						if (this->radarScreen == nullptr)
						{
							return tag;
						}
					}
				}

				// Nothing online, UNICOM
				tag.tag += "->122.8";
			}
		}

		return tag;
	}

	if (!this->noChecks && rwy.empty())
	{
		tag.tag = "!RWY";
		tag.color = TAG_COLOR_RED;

		return tag;
	}

	EuroScopePlugIn::CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
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

	if (me.IsController() && me.GetRating() > 1 && me.GetFacility() >= 3)
	{
		tag.tag.empty() ? tag.tag = "OK" : tag.tag += "->OK";
		return tag;
	}

	if (this->radarScreen == nullptr)
		return tag;

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

			if (CDelHelX_Tags::PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
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
	if (this->radarScreen != nullptr) {
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
