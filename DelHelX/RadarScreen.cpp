#include "pch.h"
#include "RadarScreen.h"

#include <algorithm>
#include <string>
#include "constants.h"

RadarScreen::RadarScreen() : EuroScopePlugIn::CRadarScreen()
{
	this->debug = false;
}

RadarScreen::~RadarScreen()
{
}

void RadarScreen::OnControllerPositionUpdate(EuroScopePlugIn::CController Controller)
{
	std::string cs = Controller.GetCallsign();
	std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);

	std::string myCS = this->GetPlugIn()->ControllerMyself().GetCallsign();
	std::transform(myCS.begin(), myCS.end(), myCS.begin(), ::toupper);

	// Not interested in observers, non-controllers and my own call-sign
	if (Controller.IsController() && Controller.GetRating() > 1 && cs != myCS)
	{
		if (Controller.GetFacility() == 3)
		{
			if (this->groundStations.find(cs) == this->groundStations.end())
			{
				this->groundStations.insert(cs);
				if (this->debug) 
				{
					this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Ground", (cs + " online").c_str(), true, true, true, false, false);
				}
			}
		}

		if (Controller.GetFacility() == 4 && cs.find("ATIS") == std::string::npos)
		{
			if (this->towerStations.find(cs) == this->towerStations.end())
			{
				this->towerStations.insert(cs);
				if (this->debug) 
				{
					this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Tower", (cs + " online").c_str(), true, true, true, false, false);
				}
			}
		}

		if (Controller.GetFacility() == 5)
		{
			if (this->approachStations.find(cs) == this->approachStations.end())
			{
				this->approachStations.insert(cs);
				if (this->debug) 
				{
					this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Approach", (cs + " online").c_str(), true, true, true, false, false);
				}
			}
		}

		if (Controller.GetFacility() == 6)
		{
			if (this->centerStations.find(cs) == this->centerStations.end())
			{
				double freq = Controller.GetPrimaryFrequency();
				auto freqString = std::to_string(freq);
				std::string rounded = freqString.substr(0, freqString.find('.') + 4);

				this->centerStations.emplace(cs, rounded);
				if (this->debug) 
				{
					this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Center", (cs + " online").c_str(), true, true, true, false, false);
				}
			}
		}
	}
}

void RadarScreen::OnControllerDisconnect(EuroScopePlugIn::CController Controller)
{
	std::string cs = Controller.GetCallsign();
	std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);

	// Not interested in observers and non-controllers
	if (Controller.IsController() && Controller.GetRating() > 1)
	{
		if (Controller.GetFacility() == 3)
		{
			if (this->debug)
			{
				this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Ground", (cs + " disconnected").c_str(), true, true, true, false, false);
			}

			if (this->groundStations.find(cs) != this->groundStations.end())
			{
				this->groundStations.erase(cs);
			}
		}

		if (Controller.GetFacility() == 4 && cs.find("ATIS") == std::string::npos)
		{
			if (this->debug)
			{
				this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Tower", (cs + " disconnected").c_str(), true, true, true, false, false);
			}

			if (this->towerStations.find(cs) != this->towerStations.end())
			{
				this->towerStations.erase(cs);
			}
		}

		if (Controller.GetFacility() == 5)
		{
			if (this->debug)
			{
				this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Approach", (cs + " disconnected").c_str(), true, true, true, false, false);
			}

			if (this->approachStations.find(cs) != this->approachStations.end())
			{
				this->approachStations.erase(cs);
			}
		}

		if (Controller.GetFacility() == 6)
		{
			if (this->debug)
			{
				this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Center", (cs + " disconnected").c_str(), true, true, true, false, false);
			}
			
			if (this->centerStations.find(cs) != this->centerStations.end())
			{
				this->centerStations.erase(cs);
			}
		}
	}
}

void RadarScreen::OnRefresh(HDC hDC, int Phase)
{
	if (Phase == EuroScopePlugIn::REFRESH_PHASE_BEFORE_TAGS)
	{
		for (auto it = this->radarTargetDepartureInfos.begin(); it != this->radarTargetDepartureInfos.end(); ++it)
		{
			auto color = this->radarTargetDepartureInfoColors.find(it->first);
			auto position = this->radarTargetScreenPositions.find(it->first);

			if (color != this->radarTargetDepartureInfoColors.end() && position != this->radarTargetScreenPositions.end())
			{
				SetTextColor(hDC, color->second);
				TextOutA(hDC, position->second.x, position->second.y, it->second.c_str(), it->second.length());
			}
		}

		
		//SelectFont(hDC, )
		
	}
}

void RadarScreen::OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget)
{
	if (RadarTarget.IsValid() && this->radarTargetDepartureInfos.find(RadarTarget.GetCallsign()) != this->radarTargetDepartureInfos.end())
	{
		POINT screenPos = this->ConvertCoordFromPositionToPixel(RadarTarget.GetPosition().GetPosition());
		screenPos.x += 25;
		screenPos.y += 25;
		this->radarTargetScreenPositions.insert_or_assign(RadarTarget.GetCallsign(), screenPos);
	}
}



