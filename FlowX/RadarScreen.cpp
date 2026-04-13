/**
 * @file RadarScreen.cpp
 * @brief Radar screen implementation; GDI rendering, drag interaction, and controller station tracking.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "RadarScreen.h"
#include "CFlowX_Base.h"
#include "CFlowX_CustomTags.h"
#include "CFlowX_Functions.h"
#include "CFlowX_Settings.h"
#include "CFlowX_Timers.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <mmsystem.h>
#include <string>
#include "constants.h"
#include "helpers.h"
#include "osm_taxiways.h"

RadarScreen::RadarScreen()
{
    this->debug = false;
}

RadarScreen::~RadarScreen() = default;

/// @brief Notifies the plugin that the screen is closing, then deletes this RadarScreen.
void RadarScreen::OnAsrContentToBeClosed()
{
    static_cast<CFlowX_Base*>(GetPlugIn())->ClearRadarScreen();
    delete this;
}

/// @brief Adds the controller to the appropriate station set when they come online or update their position.
/// @param Controller The updated controller.
void RadarScreen::OnControllerPositionUpdate(EuroScopePlugIn::CController Controller)
{
    try
    {
        std::string cs = Controller.GetCallsign();
        std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);

        std::string myCS = this->GetPlugIn()->ControllerMyself().GetCallsign();
        std::transform(myCS.begin(), myCS.end(), myCS.begin(), ::toupper);

        // Not interested in observers, non-controllers and my own call-sign
        if (Controller.IsController() && Controller.GetRating() > 1 && cs != myCS)
        {
            double      freq          = Controller.GetPrimaryFrequency();
            std::string freqFormatted = std::format("{:.3f}", freq);

            auto updateStation = [&](std::map<std::string, std::string>& stations, const char* label)
            {
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
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnControllerPositionUpdate", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnControllerPositionUpdate", "unknown exception");
    }
}

/// @brief Removes the controller from the appropriate station set when they go offline.
/// @param Controller The disconnected controller.
void RadarScreen::OnControllerDisconnect(EuroScopePlugIn::CController Controller)
{
    try
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
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnControllerDisconnect", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnControllerDisconnect", "unknown exception");
    }
}

/// @brief Dispatches all custom drawing to the four extract draw helpers; no calculations here.
/// @param hDC GDI device context for drawing.
/// @param Phase EuroScope refresh phase.
void RadarScreen::OnRefresh(HDC hDC, int Phase)
{
    try
    {
        if (Phase == EuroScopePlugIn::REFRESH_PHASE_BEFORE_TAGS)
        {
            this->UpdateSwingoverState();
            if (this->showTaxiGraph)
                this->DrawTaxiGraph(hDC);
            this->DrawTaxiOverlay(hDC);
            this->DrawTaxiRoutes(hDC);
            this->DrawPlanningRoutes(hDC);
            this->UpdateTaxiSafety();
        }

        if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS)
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

            this->DrawDepartureInfoTag(hDC);
            this->DrawGndTransferSquares(hDC);
            this->DrawTaxiWarningLabels(hDC);
            this->DrawTaxiConflicts(hDC);

            // Register invisible hit areas on ground aircraft for taxi planning (right-click to plan).
            // Skip during active planning so the full-screen TAXI_PLANNING overlay catches all clicks.
            if (settings->osmGraph.IsBuilt() && this->taxiPlanActive.empty())
            {
                for (auto rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
                     rt      = GetPlugIn()->RadarTargetSelectNext(rt))
                {
                    auto fp = rt.GetCorrelatedFlightPlan();
                    if (!fp.IsValid())
                        continue;
                    if (rt.GetPosition().GetReportedGS() > 50)
                        continue;

                    POINT pt   = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());
                    RECT  area = {pt.x - 10, pt.y - 10, pt.x + 10, pt.y + 10};
                    AddScreenObject(SCREEN_OBJECT_TAXI_TARGET, fp.GetCallsign(), area, false, "");
                }
            }
        }

        if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_LISTS)
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

            // Cache the EuroScope HWND on first frame so Create*Popout can do ClientToScreen
            // when called from OnClickScreenObject (which has no HDC).
            if (!this->esHwnd_)
                this->esHwnd_ = WindowFromDC(hDC);

            // ── Approach Estimate ──────────────────────────────────────────────
            if (this->approachEstPopout)
            {
                if (this->approachEstPopout->IsCloseRequested())
                {
                    settings->SetApproachEstVisible(false);
                    settings->SetApproachEstPoppedOut(false);
                    this->approachEstPopout.reset();
                }
                else if (this->approachEstPopout->IsPopInRequested())
                {
                    settings->SetApproachEstPoppedOut(false);
                    this->approachEstPopout.reset();
                }
            }
            if (settings->GetApproachEstVisible() && settings->GetApproachEstPoppedOut() &&
                !this->approachEstPopout)
            {
                this->CreateApproachEstPopout(settings);
            }
            if (settings->GetApproachEstVisible())
            {
                if (this->approachEstPopout)
                {
                    if (!this->approachEstPopout->IsDirectDragging())
                    {
                        // Sync from the popout's live size — onDirectDrag_ may have already resized
                        // the HWND directly; reading back prevents ResizeIfNeeded from snapping it back.
                        this->approachEstWindowW = this->approachEstPopout->GetContentW();
                        this->approachEstWindowH = this->approachEstPopout->GetContentH();
                        this->RenderToPopout(hDC, this->approachEstPopout.get(),
                                             this->approachEstWindowPos,
                                             this->approachEstWindowW, this->approachEstWindowH,
                                             [this](HDC dc)
                                             { this->DrawApproachEstimateWindow(dc); });
                    }
                }
                else
                {
                    this->DrawApproachEstimateWindow(hDC);
                }
            }

            // ── Departure Rate ─────────────────────────────────────────────────
            if (this->depRatePopout)
            {
                if (this->depRatePopout->IsCloseRequested())
                {
                    settings->SetDepRateVisible(false);
                    settings->SetDepRatePoppedOut(false);
                    this->depRatePopout.reset();
                }
                else if (this->depRatePopout->IsPopInRequested())
                {
                    settings->SetDepRatePoppedOut(false);
                    this->depRatePopout.reset();
                }
            }
            if (settings->GetDepRateVisible() && settings->GetDepRatePoppedOut() &&
                !this->depRatePopout)
            {
                this->CreateDepRatePopout(settings);
            }
            if (settings->GetDepRateVisible())
            {
                if (this->depRatePopout)
                {
                    this->RenderToPopout(hDC, this->depRatePopout.get(),
                                         this->depRateWindowPos,
                                         this->depRateWindowW, this->depRateWindowH,
                                         [this](HDC dc)
                                         { this->DrawDepRateWindow(dc); });
                }
                else
                {
                    this->DrawDepRateWindow(hDC);
                }
            }

            // ── TWR Outbound ───────────────────────────────────────────────────
            if (settings->GetTwrOutboundVisible())
            {
                this->DrawTwrOutbound(hDC);
            }

            // ── TWR Inbound ────────────────────────────────────────────────────
            if (settings->GetTwrInboundVisible())
            {
                this->DrawTwrInbound(hDC);
            }

            this->DrawNapReminder(hDC);

            // ── Weather ────────────────────────────────────────────────────────
            if (this->weatherPopout)
            {
                if (this->weatherPopout->IsCloseRequested())
                {
                    settings->SetWeatherVisible(false);
                    settings->SetWeatherPoppedOut(false);
                    this->weatherPopout.reset();
                }
                else if (this->weatherPopout->IsPopInRequested())
                {
                    settings->SetWeatherPoppedOut(false);
                    this->weatherPopout.reset();
                }
            }
            if (settings->GetWeatherVisible() && settings->GetWeatherPoppedOut() &&
                !this->weatherPopout)
            {
                this->CreateWeatherPopout(settings);
            }
            if (settings->GetWeatherVisible())
            {
                if (this->weatherPopout)
                {
                    this->RenderToPopout(hDC, this->weatherPopout.get(),
                                         this->weatherWindowPos,
                                         this->weatherWindowW, this->weatherWindowH,
                                         [this](HDC dc)
                                         { this->DrawWeatherWindow(dc); });
                }
                else
                {
                    this->DrawWeatherWindow(hDC);
                }
            }

            // ── DIFLIS ─────────────────────────────────────────────────────────
            if (this->difliPopout)
            {
                if (this->difliPopout->IsCloseRequested())
                {
                    settings->SetDifliVisible(false);
                    settings->SetDifliPoppedOut(false);
                    this->difliPopout.reset();
                }
                else if (this->difliPopout->IsPopInRequested())
                {
                    settings->SetDifliPoppedOut(false);
                    this->difliPopout.reset();
                }
            }
            if (settings->GetDifliVisible() && settings->GetDifliPoppedOut() &&
                !this->difliPopout)
            {
                this->CreateDifliPopout(settings);
            }
            if (settings->GetDifliVisible())
            {
                if (this->difliPopout)
                {
                    if (!this->difliPopout->IsDirectDragging())
                    {
                        this->difliWindowW = this->difliPopout->GetContentW();
                        this->difliWindowH = this->difliPopout->GetContentH();
                        this->RenderToPopout(hDC, this->difliPopout.get(),
                                             this->difliWindowPos,
                                             this->difliWindowW, this->difliWindowH,
                                             [this](HDC dc)
                                             { this->DrawDifliWindow(dc); });
                    }
                }
                else
                {
                    this->DrawDifliWindow(hDC);
                }
            }

            this->DrawStartButton(hDC);
            this->DrawStartMenu(hDC);

            // Register the full-screen planning overlay last (after all other objects) so it
            // catches clicks regardless of what DEP_TAG, GND_TRANSFER, or window objects sit on top.
            if (!this->taxiPlanActive.empty())
            {
                RECT clip{};
                GetClipBox(hDC, &clip);
                AddScreenObject(SCREEN_OBJECT_TAXI_PLANNING, this->taxiPlanActive.c_str(), clip, false, "");
            }
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnRefresh", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnRefresh", "unknown exception");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private draw helpers — pure GDI output, no state calculations
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Updates the screen-pixel anchor for a departure overlay when the radar target moves.
/// @param RadarTarget The target whose position has changed.
void RadarScreen::OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget)
{
    try
    {
        auto depInfoIt = this->radarTargetDepartureInfos.find(RadarTarget.GetCallsign());
        if (RadarTarget.IsValid() && depInfoIt != this->radarTargetDepartureInfos.end())
        {
            const auto esPos             = RadarTarget.GetPosition().GetPosition();
            depInfoIt->second.anchor.lat = esPos.m_Latitude;
            depInfoIt->second.anchor.lon = esPos.m_Longitude;
        }
        if (RadarTarget.IsValid())
        {
            static_cast<CFlowX_CustomTags*>(this->GetPlugIn())->UpdatePositionDerivedTags(RadarTarget);
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnRadarTargetPositionUpdate", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnRadarTargetPositionUpdate", "unknown exception");
    }
}

/// @brief Removes the departure overlay entry for a disconnecting flight plan.
/// @param FlightPlan The disconnecting flight plan.
void RadarScreen::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
    try
    {
        std::string cs = FlightPlan.GetCallsign();

        auto findCallSign = this->radarTargetDepartureInfos.find(cs);
        if (findCallSign != this->radarTargetDepartureInfos.end())
            this->radarTargetDepartureInfos.erase(findCallSign);

        // Clear taxi/push planning state for disconnecting aircraft.
        this->pushTracked.erase(cs);
        this->taxiAssigned.erase(cs);
        this->taxiAssignedTimes.erase(cs);
        this->taxiAssignedPos.erase(cs);
        this->taxiTracked.erase(cs);
        this->taxiSuggested.erase(cs);
        if (this->taxiPlanActive == cs)
        {
            this->taxiPlanActive.clear();
            this->taxiPlanIsPush        = false;
            this->taxiPlanForwardOnly   = false;
            this->taxiSwingoverActive   = false;
            this->taxiSwingoverFixedSeg = {};
            this->taxiSwingoverOrigin   = {};
            this->taxiSwingoverBearing  = -1.0;
            this->taxiAltPrevDown       = false;
            this->taxiWaypoints.clear();
            this->taxiGreenPreview = {};
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("RadarScreen::OnFlightPlanDisconnect", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("RadarScreen::OnFlightPlanDisconnect", "unknown exception");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Popout helpers
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Renders a window to a memory DC and pushes the resulting bitmap to a popout window.
///
/// Temporarily overrides @p windowPos to {0,0} so the draw function renders at the bitmap origin,
/// then restores it after the bitmap is transferred. Cursor position, hit areas, and pending events
/// are exchanged with the popout window so hover highlights and clicks work natively.
void RadarScreen::RenderToPopout(HDC screenDC, PopoutWindow* popout, POINT& windowPos, int w, int h,
                                 std::function<void(HDC)> drawFn)
{
    if (w <= 0 || h <= 0)
        return;

    popout->ResizeIfNeeded(w, h);
    this->popoutHoverPoint_ = popout->GetCursorPosition();

    HDC     memDC  = CreateCompatibleDC(screenDC);
    HBITMAP bmp    = CreateCompatibleBitmap(screenDC, w, h);
    HGDIOBJ oldBmp = SelectObject(memDC, bmp);

    RECT r = {0, 0, w, h};
    FillRect(memDC, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    POINT savedPos        = windowPos;
    windowPos             = {0, 0};
    this->isPopoutRender_ = true;
    this->currentPopout_  = popout;
    popout->ClearScreenObjects();
    drawFn(memDC);
    this->isPopoutRender_ = false;
    this->currentPopout_  = nullptr;
    windowPos             = savedPos;

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    popout->UpdateContent(bmp, w, h);

    // Dispatch events queued by the popout thread — executed here on the EuroScope main thread.
    bool hadEvents = false;
    for (auto& ev : popout->ConsumeEvents())
    {
        hadEvents = true;
        switch (ev.type)
        {
        case PendingEvent::Type::Hover:
            this->OnOverScreenObject(ev.objectType, ev.objectId.c_str(), ev.pt, ev.area);
            break;
        case PendingEvent::Type::LClick:
            this->OnClickScreenObject(ev.objectType, ev.objectId.c_str(), ev.pt, ev.area,
                                      EuroScopePlugIn::BUTTON_LEFT);
            break;
        case PendingEvent::Type::RClick:
            this->OnClickScreenObject(ev.objectType, ev.objectId.c_str(), ev.pt, ev.area,
                                      EuroScopePlugIn::BUTTON_RIGHT);
            break;
        case PendingEvent::Type::DragMove:
            this->OnMoveScreenObject(ev.objectType, ev.objectId.c_str(), ev.pt, ev.area, false);
            break;
        case PendingEvent::Type::DragRelease:
            this->OnMoveScreenObject(ev.objectType, ev.objectId.c_str(), ev.pt, ev.area, true);
            break;
        }
    }
    // Mirror RequestRefresh() — repaint the popout immediately after any event dispatch.
    if (hadEvents)
        popout->RequestRepaint();
}

/// @brief Routes an AddScreenObject call to the active popout (during isPopoutRender_) or to EuroScope.
void RadarScreen::AddScreenObjectAuto(int objectType, const char* objectId, RECT rect, bool dragable,
                                      const char* tooltip)
{
    if (this->isPopoutRender_ && this->currentPopout_)
        this->currentPopout_->AddScreenObject(objectType, objectId, rect, dragable, tooltip);
    else if (!this->isPopoutRender_)
        AddScreenObject(objectType, objectId, rect, dragable, tooltip);
}

