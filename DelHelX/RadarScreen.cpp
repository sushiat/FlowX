#include "pch.h"
#include "RadarScreen.h"

#include <algorithm>
#include <string>
#include "constants.h"

RadarScreen::RadarScreen(RadarScreen** ownerPtr) : m_ownerPtr(ownerPtr)
{
	this->debug = false;
}

RadarScreen::~RadarScreen() = default;

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
	if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS)
	{
		for (auto it = this->radarTargetDepartureInfos.begin(); it != this->radarTargetDepartureInfos.end(); ++it)
		{
			if (it->second.pos.x > -1 && it->second.pos.y > -1)
			{
				SetTextColor(hDC, it->second.dep_color);
				SIZE textSize;
				GetTextExtentPoint32A(hDC, it->second.dep_info.c_str(), it->second.dep_info.length(), &textSize);
				TextOutA(hDC, it->second.pos.x - textSize.cx + it->second.dragX, it->second.pos.y + it->second.dragY, it->second.dep_info.c_str(), it->second.dep_info.length());
				RECT area;
				area.left = it->second.pos.x - textSize.cx - 2 + it->second.dragX;
				area.top = it->second.pos.y - 2 + it->second.dragY;
				area.right = it->second.pos.x + 2 + it->second.dragX;
				area.bottom = it->second.pos.y + textSize.cy + 2 + it->second.dragY;

				auto sidBrush = CreateSolidBrush(it->second.sid_color);
				auto sidPen = CreatePen(PS_SOLID, 1, it->second.sid_color);
				SelectObject(hDC, sidBrush);
				SelectObject(hDC, sidPen);
				RECT rect = {
					it->second.pos.x - textSize.cx + it->second.dragX + 2,
					it->second.pos.y + it->second.dragY + (area.bottom - area.top) - 5 + 2,
					it->second.pos.x - textSize.cx + it->second.dragX + 14,
					it->second.pos.y + it->second.dragY + (area.bottom - area.top) - 5 + 14 };
				Ellipse(hDC, rect.left, rect.top, rect.right, rect.bottom);
				DeleteObject(sidBrush);
				DeleteObject(sidPen);

				if (!it->second.hp_info.empty())
				{
					SetTextColor(hDC, it->second.hp_color);
					TextOutA(hDC, it->second.pos.x - textSize.cx + 18 + it->second.dragX, it->second.pos.y + it->second.dragY + (area.bottom - area.top) - 5, it->second.hp_info.c_str(), it->second.hp_info.length());
					area.bottom += (area.bottom - area.top) - 5;
				}

				auto pen = CreatePen(PS_SOLID, 1, it->second.dep_color);
				SelectObject(hDC, pen);
				MoveToEx(hDC, it->second.pos.x + 16, it->second.pos.y - 3, nullptr);
				if (area.right <= it->second.pos.x + 16)
				{
					LineTo(hDC, area.right, area.top + (area.bottom - area.top) / 2);
				}
				else
				{
					LineTo(hDC, area.left, area.top + (area.bottom - area.top) / 2);
				}

				DeleteObject(pen);

				AddScreenObject(6681, it->first.c_str(), area, true, "");
			}
		}
	}
}

void RadarScreen::OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget)
{
	auto depInfo = this->radarTargetDepartureInfos.find(RadarTarget.GetCallsign());
	if (RadarTarget.IsValid() && depInfo != this->radarTargetDepartureInfos.end())
	{
		POINT screenPos = this->ConvertCoordFromPositionToPixel(RadarTarget.GetPosition().GetPosition());
		screenPos.x -= 16;
		screenPos.y += 3;
		depInfo->second.pos = screenPos;
	}
}

void RadarScreen::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
	auto findCallSign = this->radarTargetDepartureInfos.find(FlightPlan.GetCallsign());
	if (findCallSign != this->radarTargetDepartureInfos.end())
	{
		this->radarTargetDepartureInfos.erase(findCallSign);
	}
}

void RadarScreen::OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released)
{
	auto depInfo = this->radarTargetDepartureInfos.find(std::string(sObjectId));
	if (depInfo != this->radarTargetDepartureInfos.end())
	{
		if (depInfo->second.lastDrag.x == -1 || depInfo->second.lastDrag.y == -1)
		{
			depInfo->second.lastDrag = Pt;
		}

		depInfo->second.dragX += Pt.x - depInfo->second.lastDrag.x;
		depInfo->second.dragY += Pt.y - depInfo->second.lastDrag.y;

		depInfo->second.lastDrag = Pt;

		if (Released)
		{
			depInfo->second.lastDrag.x = -1;
			depInfo->second.lastDrag.y = -1;
		}
	}
}
