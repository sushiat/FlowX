#include "pch.h"
#include "RadarScreen.h"
#include "CDelHelX_Base.h"

#include <algorithm>
#include <string>
#include "constants.h"

RadarScreen::RadarScreen()
{
    this->debug = false;
}

RadarScreen::~RadarScreen() = default;

/// @brief Notifies the plugin that the screen is closing, then deletes this RadarScreen.
void RadarScreen::OnAsrContentToBeClosed()
{
    static_cast<CDelHelX_Base*>(GetPlugIn())->ClearRadarScreen();
    delete this;
}

/// @brief Adds the controller to the appropriate station set when they come online or update their position.
/// @param Controller The updated controller.
void RadarScreen::OnControllerPositionUpdate(EuroScopePlugIn::CController Controller)
{
    std::string cs = Controller.GetCallsign();
    std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);

    std::string myCS = this->GetPlugIn()->ControllerMyself().GetCallsign();
    std::transform(myCS.begin(), myCS.end(), myCS.begin(), ::toupper);

    // Not interested in observers, non-controllers and my own call-sign
    if (Controller.IsController() && Controller.GetRating() > 1 && cs != myCS)
    {
        double freq = Controller.GetPrimaryFrequency();
        auto freqString = std::to_string(freq);
        std::string freqFormatted = freqString.substr(0, freqString.find('.') + 4);

        auto updateStation = [&](std::map<std::string, std::string>& stations, const char* label) {
            auto it = stations.find(cs);
            if (it == stations.end())
            {
                stations.emplace(cs, freqFormatted);
                if (this->debug)
                {
                    this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, label, (cs + " online (" + freqFormatted + ")").c_str(), true, true, true, false, false);
                }
            }
            else if (it->second != freqFormatted)
            {
                if (this->debug)
                {
                    this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, label, (cs + " freq changed: " + it->second + " -> " + freqFormatted).c_str(), true, true, true, false, false);
                }
                it->second = freqFormatted;
            }
        };

        if (Controller.GetFacility() == 3)
        {
            updateStation(this->groundStations, "Ground");
        }
        if (Controller.GetFacility() == 4 && cs.find("ATIS") == std::string::npos)
        {
            updateStation(this->towerStations, "Tower");
        }
        if (Controller.GetFacility() == 5)
        {
            updateStation(this->approachStations, "Approach");
        }
        if (Controller.GetFacility() == 6)
        {
            updateStation(this->centerStations, "Center");
        }
    }
}

/// @brief Removes the controller from the appropriate station set when they go offline.
/// @param Controller The disconnected controller.
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

/// @brief Draws departure info overlays (text, SID dot, HP label, connector line) after tags are rendered.
/// @param hDC GDI device context for drawing.
/// @param Phase EuroScope refresh phase; overlays are only drawn during REFRESH_PHASE_AFTER_TAGS.
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

                AddScreenObject(SCREEN_OBJECT_DEP_TAG, it->first.c_str(), area, true, "");
            }
        }

        // --- Departure rate window ---
        if (!this->depRateLog.empty())
        {
            const int ROW_H   = 15;
            const int WIN_PAD = 6;
            const int COL_RWY = 30;
            const int COL_CNT = 28;
            const int WIN_W   = WIN_PAD + COL_RWY + COL_CNT + WIN_PAD;
            int numRows       = (int)this->depRateLog.size();
            const int WIN_H   = ROW_H + numRows * ROW_H + WIN_PAD / 2;

            // Auto-position to lower-right on first draw
            if (this->depRateWindowPos.x == -1)
            {
                RECT clip;
                GetClipBox(hDC, &clip);
                this->depRateWindowPos.x = clip.right  - WIN_W - 20;
                this->depRateWindowPos.y = clip.bottom - WIN_H - 20;
            }

            int wx = this->depRateWindowPos.x;
            int wy = this->depRateWindowPos.y;

            // Window and header backgrounds
            RECT winRect = { wx, wy, wx + WIN_W, wy + WIN_H };
            RECT hdrRect = { wx, wy, wx + WIN_W, wy + ROW_H };
            auto bgBrush  = CreateSolidBrush(RGB(25, 25, 25));
            auto hdrBrush = CreateSolidBrush(RGB(55, 55, 55));
            FillRect(hDC, &winRect, bgBrush);
            FillRect(hDC, &hdrRect, hdrBrush);
            DeleteObject(bgBrush);
            DeleteObject(hdrBrush);

            // Border
            auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
            FrameRect(hDC, &winRect, borderBrush);
            DeleteObject(borderBrush);

            // Header text
            SetBkMode(hDC, TRANSPARENT);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            DrawTextA(hDC, "DEP/H", -1, &hdrRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddScreenObject(SCREEN_OBJECT_DEPRATE_WIN, "DEPRATE", hdrRect, true, "");

            // Data rows
            int row = 0;
            for (auto& kv : this->depRateLog)
            {
                int rowY  = wy + ROW_H + row * ROW_H;
                int count = (int)kv.second.size();

                RECT rwyRect = { wx + WIN_PAD,           rowY, wx + WIN_PAD + COL_RWY, rowY + ROW_H };
                RECT cntRect = { wx + WIN_PAD + COL_RWY, rowY, wx + WIN_W - WIN_PAD,   rowY + ROW_H };

                SetTextColor(hDC, TAG_COLOR_DEFAULT_GRAY);
                DrawTextA(hDC, kv.first.c_str(), -1, &rwyRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                SetTextColor(hDC, count > 0 ? TAG_COLOR_GREEN : TAG_COLOR_DEFAULT_GRAY);
                std::string countStr = std::to_string(count);
                DrawTextA(hDC, countStr.c_str(), -1, &cntRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                row++;
            }
        }
    }
}

/// @brief Updates the screen-pixel anchor for a departure overlay when the radar target moves.
/// @param RadarTarget The target whose position has changed.
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

/// @brief Removes the departure overlay entry for a disconnecting flight plan.
/// @param FlightPlan The disconnecting flight plan.
void RadarScreen::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
    auto findCallSign = this->radarTargetDepartureInfos.find(FlightPlan.GetCallsign());
    if (findCallSign != this->radarTargetDepartureInfos.end())
    {
        this->radarTargetDepartureInfos.erase(findCallSign);
    }
}

/// @brief Accumulates drag offsets for departure overlays and resets the drag origin on mouse release.
/// @param ObjectType EuroScope type identifier of the dragged object.
/// @param sObjectId Callsign string identifying which departure overlay is being dragged.
/// @param Pt Current cursor position.
/// @param Area Bounding rectangle of the object.
/// @param Released True when the mouse button has been released.
void RadarScreen::OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released)
{
    if (std::string(sObjectId) == "DEPRATE")
    {
        if (this->depRateLastDrag.x == -1 || this->depRateLastDrag.y == -1)
        {
            this->depRateLastDrag = Pt;
        }
        this->depRateWindowPos.x += Pt.x - this->depRateLastDrag.x;
        this->depRateWindowPos.y += Pt.y - this->depRateLastDrag.y;
        this->depRateLastDrag = Pt;
        if (Released)
        {
            this->depRateLastDrag = { -1, -1 };
        }
        return;
    }

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
