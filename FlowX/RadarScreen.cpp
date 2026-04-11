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

/// @brief Recomputes taxiGreenPreview from the current taxiCursorSnap and planning state.
/// Called from OnOverScreenObject on every mouse move and from UpdateSwingoverState on ALT toggle
/// so the new route is visible immediately without waiting for the next mouse event.
void RadarScreen::RecalculateTaxiPreview()
{
    if (this->taxiPlanActive.empty() || this->taxiPlanIsPush || this->taxiMidDrawing)
        return;

    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    auto  rt       = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
    if (!rt.IsValid())
        return;

    const EuroScopePlugIn::CPosition rpos    = rt.GetPosition().GetPosition();
    const GeoPoint                   origin  = {rpos.m_Latitude, rpos.m_Longitude};
    const double                     heading = rt.GetPosition().GetReportedHeadingTrueNorth();

    std::set<int> blocked;
    for (const auto& [_, pushRoute] : this->pushTracked)
    {
        auto b = settings->osmGraph.NodesToBlock(pushRoute.polyline, 3.0);
        blocked.insert(b.begin(), b.end());
    }

    const double taxiWs = settings->GetAircraftWingspan(
        GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str()).GetFlightPlanData().GetAircraftFPType());

    if (this->taxiSwingoverActive && this->taxiSwingoverFixedSeg.valid)
    {
        TaxiRoute tail = settings->osmGraph.FindWaypointRoute(
            this->taxiSwingoverOrigin, this->taxiWaypoints, this->taxiCursorSnap,
            taxiWs, settings->GetActiveDepRunways(), settings->GetActiveArrRunways(),
            this->taxiSwingoverBearing, blocked);
        if (tail.valid)
        {
            TaxiRoute combined = this->taxiSwingoverFixedSeg;
            combined.totalDistM += tail.totalDistM;
            combined.totalCost += tail.totalCost;
            combined.exitBearing = tail.exitBearing;
            for (const auto& pt : tail.polyline)
                combined.polyline.push_back(pt);
            for (const auto& ref : tail.wayRefs)
                combined.wayRefs.push_back(ref);
            this->taxiGreenPreview = combined;
        }
        else
        {
            this->taxiGreenPreview = this->taxiSwingoverFixedSeg;
        }
    }
    else
    {
        const bool hasDrawnNodes = !this->taxiDrawnNodeSet.empty() && !this->taxiWaypoints.empty();
        // If the cursor is still on top of the last waypoint (e.g. right after
        // middle-button release), route to the waypoint only — avoids a degenerate
        // zero-length final segment that loops back on itself.
        const bool cursorAtLastWp =
            !this->taxiWaypoints.empty() &&
            HaversineM(this->taxiCursorSnap, this->taxiWaypoints.back()) < 5.0;
        const GeoPoint        dest = cursorAtLastWp ? this->taxiWaypoints.back() : this->taxiCursorSnap;
        std::vector<GeoPoint> wps =
            cursorAtLastWp
                ? std::vector<GeoPoint>(this->taxiWaypoints.begin(), this->taxiWaypoints.end() - 1)
                : this->taxiWaypoints;
        this->taxiGreenPreview = settings->osmGraph.FindWaypointRoute(
            origin, wps, dest,
            taxiWs, settings->GetActiveDepRunways(), settings->GetActiveArrRunways(),
            heading, blocked, hasDrawnNodes, this->taxiDrawnNodeSet, settings->GetDebug(),
            this->taxiPlanForwardOnly);
    }
}

/// @brief Detects ALT keypress each BEFORE_TAGS frame and toggles taxiSwingoverActive when it fires.
void RadarScreen::UpdateSwingoverState()
{
    if (this->taxiPlanActive.empty() || this->taxiPlanIsPush)
        return;

    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

    const bool vkMenu  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool vkLMenu = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
    const bool vkRMenu = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
    const bool altNow  = vkMenu || vkLMenu || vkRMenu;
    if (altNow && !this->taxiAltPrevDown)
    {
        this->taxiSwingoverActive = !this->taxiSwingoverActive;

        if (this->taxiSwingoverActive)
        {
            this->taxiSwingoverFixedSeg = {};
            const auto& airports        = settings->GetAirports();
            auto        rt              = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
            if (!airports.empty() && rt.IsValid())
            {
                const auto&  ap     = airports.begin()->second;
                const auto   rpos   = rt.GetPosition().GetPosition();
                const double hdg    = rt.GetPosition().GetReportedHeadingTrueNorth();
                const double taxiWs = settings->GetAircraftWingspan(
                    GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str()).GetFlightPlanData().GetAircraftFPType());
                const GeoPoint acPos{rpos.m_Latitude, rpos.m_Longitude};

                const std::string curRef = settings->osmGraph.WayRefAt(acPos, 40.0);
                if (!curRef.empty())
                {
                    auto sr = settings->osmGraph.SwingoverSnap(
                        acPos, curRef, ap.taxiLaneSwingoverPairs, taxiWs, hdg);
                    if (sr.valid)
                    {
                        TaxiRoute fixedSeg;
                        fixedSeg.polyline.push_back(acPos);
                        fixedSeg.polyline.push_back(sr.crossPt);
                        for (const auto& pt : sr.sbendPts)
                            fixedSeg.polyline.push_back(pt);
                        fixedSeg.polyline.push_back(sr.partnerPt);
                        fixedSeg.wayRefs            = {curRef};
                        fixedSeg.valid              = true;
                        fixedSeg.totalDistM         = HaversineM(acPos, sr.partnerPt);
                        this->taxiSwingoverFixedSeg = fixedSeg;
                        this->taxiSwingoverOrigin   = sr.partnerPt;
                        this->taxiSwingoverBearing  = sr.brngAtPartner;
                    }
                    else
                        this->taxiSwingoverActive = false;
                }
                else
                    this->taxiSwingoverActive = false;
            }
            else
                this->taxiSwingoverActive = false;
        }
        else
        {
            this->taxiSwingoverFixedSeg = {};
        }

        if (settings->GetDebug())
            settings->LogDebugMessage(
                std::format("Swingover {} (VK_MENU={} VK_LMENU={} VK_RMENU={})",
                            this->taxiSwingoverActive ? "ON" : "OFF",
                            vkMenu, vkLMenu, vkRMenu),
                "TAXI");

        // Recompute and redraw immediately so the controller sees the new route
        // without having to nudge the mouse first.
        this->RecalculateTaxiPreview();
        this->RequestRefresh();
    }
    this->taxiAltPrevDown = altNow;
}

/// @brief Draws the OSM taxiway/taxilane polylines, derived runway centrelines, holding-position circles, and way labels.
void RadarScreen::DrawTaxiOverlay(HDC hDC)
{
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

    if (!(this->showTaxiOverlay || this->showTaxiLabels))
        return;
    if (settings->osmData.ways.empty() && settings->osmData.holdingPositions.empty())
        return;

    // Pass 1 — draw polylines, one pen per way.
    if (this->showTaxiOverlay)
        for (const auto& way : settings->osmData.ways)
        {
            const COLORREF col  = (way.type == AerowayType::Taxiway_HoldingPoint)   ? RGB(255, 80, 80)
                                  : (way.type == AerowayType::Taxiway_Intersection) ? RGB(80, 150, 255)
                                  : (way.type == AerowayType::Taxilane)             ? RGB(0, 200, 255)
                                  : (way.type == AerowayType::Runway)               ? RGB(255, 140, 0)
                                                                                    : RGB(255, 220, 0);
            HPEN           pen  = CreatePen(PS_SOLID, 2, col);
            HPEN           prev = static_cast<HPEN>(SelectObject(hDC, pen));

            bool first = true;
            for (const auto& gp : way.geometry)
            {
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = gp.lat;
                pos.m_Longitude = gp.lon;
                const POINT pt  = ConvertCoordFromPositionToPixel(pos);
                if (first)
                {
                    MoveToEx(hDC, pt.x, pt.y, nullptr);
                    first = false;
                }
                else
                    LineTo(hDC, pt.x, pt.y);
            }

            SelectObject(hDC, prev);
            DeleteObject(pen);
        }

    // Pass 1b — derived runway centrelines (from TaxiGraph, not OSM ways).
    if (this->showTaxiOverlay)
    {
        HPEN rwPen  = CreatePen(PS_SOLID, 3, RGB(255, 140, 0));
        HPEN rwPrev = static_cast<HPEN>(SelectObject(hDC, rwPen));
        for (const auto& cl : settings->osmGraph.runwayCentrelines)
        {
            bool first = true;
            for (const auto& gp : cl)
            {
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = gp.lat;
                pos.m_Longitude = gp.lon;
                const POINT pt  = ConvertCoordFromPositionToPixel(pos);
                if (first)
                {
                    MoveToEx(hDC, pt.x, pt.y, nullptr);
                    first = false;
                }
                else
                    LineTo(hDC, pt.x, pt.y);
            }
        }
        SelectObject(hDC, rwPrev);
        DeleteObject(rwPen);
    }

    HFONT labelFont = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = static_cast<HFONT>(SelectObject(hDC, labelFont));
    SetBkMode(hDC, TRANSPARENT);

    auto drawLabel = [&](POINT pt, const std::string& text, COLORREF col)
    {
        RECT m = {0, 0, 200, 30};
        DrawTextA(hDC, text.c_str(), -1, &m, DT_CALCRECT | DT_SINGLELINE);
        const int hw = (m.right - m.left) / 2 + 2;
        const int hh = (m.bottom - m.top) / 2 + 1;
        RECT      bg = {pt.x - hw, pt.y - hh, pt.x + hw, pt.y + hh};
        auto      br = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hDC, &bg, br);
        DeleteObject(br);
        SetTextColor(hDC, col);
        DrawTextA(hDC, text.c_str(), -1, &bg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    // Pass 3 — holding position nodes: filled orange circles with designator label.
    if (this->showTaxiOverlay && !settings->osmData.holdingPositions.empty())
    {
        HPEN   hpPen       = CreatePen(PS_SOLID, 1, RGB(255, 140, 0));
        HBRUSH hpBrush     = CreateSolidBrush(RGB(255, 140, 0));
        HPEN   prevHpPen   = static_cast<HPEN>(SelectObject(hDC, hpPen));
        HBRUSH prevHpBrush = static_cast<HBRUSH>(SelectObject(hDC, hpBrush));

        for (const auto& hp : settings->osmData.holdingPositions)
        {
            EuroScopePlugIn::CPosition pos;
            pos.m_Latitude  = hp.pos.lat;
            pos.m_Longitude = hp.pos.lon;
            const POINT pt  = ConvertCoordFromPositionToPixel(pos);
            Ellipse(hDC, pt.x - 7, pt.y - 7, pt.x + 7, pt.y + 7);

            const std::string& lbl = hp.ref.empty() ? hp.name : hp.ref;
            if (!lbl.empty() && this->showTaxiLabels)
                drawLabel({pt.x + 13, pt.y}, lbl, RGB(160, 80, 0));
        }

        SelectObject(hDC, prevHpBrush);
        SelectObject(hDC, prevHpPen);
        DeleteObject(hpBrush);
        DeleteObject(hpPen);
    }

    // Pass 2 — way labels; gated on showTaxiLabels.
    if (this->showTaxiLabels)
    {
        std::set<std::string> drawnIsxLabels;
        std::set<std::string> drawnHpLabels;

        for (const auto& way : settings->osmData.ways)
        {
            if (way.geometry.size() < 2)
                continue;

            const std::string& lbl = way.ref.empty() ? way.name : way.ref;
            if (lbl.empty())
                continue;

            if (way.type == AerowayType::Taxiway_Intersection)
            {
                if (drawnIsxLabels.contains(lbl))
                    continue;
                drawnIsxLabels.insert(lbl);
            }

            if (way.type == AerowayType::Taxiway_HoldingPoint)
            {
                if (drawnHpLabels.contains(lbl))
                    continue;
                drawnHpLabels.insert(lbl);
            }

            const COLORREF textCol = (way.type == AerowayType::Taxiway_HoldingPoint)   ? RGB(200, 60, 60)
                                     : (way.type == AerowayType::Taxiway_Intersection) ? RGB(60, 120, 220)
                                     : (way.type == AerowayType::Taxilane)             ? RGB(0, 160, 210)
                                     : (way.type == AerowayType::Runway)               ? RGB(200, 90, 0)
                                                                                       : RGB(180, 140, 0);

            double accum   = 0.0;
            double nextLbl = 250.0;
            bool   placed  = false;

            for (size_t k = 1; k < way.geometry.size(); ++k)
            {
                const auto&  a    = way.geometry[k - 1];
                const auto&  b    = way.geometry[k];
                const double dLat = (b.lat - a.lat) * std::numbers::pi / 180.0;
                const double dLon = (b.lon - a.lon) * std::numbers::pi / 180.0;
                const double cosA = std::cos(a.lat * std::numbers::pi / 180.0);
                const double cosB = std::cos(b.lat * std::numbers::pi / 180.0);
                const double h    = std::sin(dLat / 2) * std::sin(dLat / 2) +
                                    cosA * cosB * std::sin(dLon / 2) * std::sin(dLon / 2);
                const double seg  = 6'371'000.0 * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));

                if (accum + seg >= nextLbl)
                {
                    EuroScopePlugIn::CPosition pos;
                    pos.m_Latitude  = b.lat;
                    pos.m_Longitude = b.lon;
                    drawLabel(ConvertCoordFromPositionToPixel(pos), lbl, textCol);
                    nextLbl += 500.0;
                    placed = true;
                }
                accum += seg;
            }

            if (!placed)
            {
                const size_t               mid = way.geometry.size() / 2;
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = way.geometry[mid].lat;
                pos.m_Longitude = way.geometry[mid].lon;
                drawLabel(ConvertCoordFromPositionToPixel(pos), lbl, textCol);
            }
        }
    }

    SelectObject(hDC, prevFont);
    DeleteObject(labelFont);
}

/// @brief Draws a single TaxiRoute polyline with the given GDI pen colour and width.
void RadarScreen::DrawRoutePolyline(HDC hDC, const TaxiRoute& route, COLORREF col, int width)
{
    if (!route.valid || route.polyline.empty())
        return;
    HPEN pen   = CreatePen(PS_SOLID, width, col);
    HPEN prev  = static_cast<HPEN>(SelectObject(hDC, pen));
    bool first = true;
    for (const auto& gp : route.polyline)
    {
        EuroScopePlugIn::CPosition pos;
        pos.m_Latitude  = gp.lat;
        pos.m_Longitude = gp.lon;
        POINT pt        = ConvertCoordFromPositionToPixel(pos);
        if (first)
        {
            MoveToEx(hDC, pt.x, pt.y, nullptr);
            first = false;
        }
        else
            LineTo(hDC, pt.x, pt.y);
    }
    SelectObject(hDC, prev);
    DeleteObject(pen);
}

/// @brief Draws confirmed (2-second flash) and persistently tracked taxi routes.
void RadarScreen::DrawTaxiRoutes(HDC hDC)
{
    // Draw confirmed taxi routes (2-second flash); prune entries older than 2 s.
    if (!this->taxiAssigned.empty())
    {
        const ULONGLONG          now = GetTickCount64();
        std::vector<std::string> toErase;
        for (const auto& [cs, _] : this->taxiAssigned)
        {
            auto tIt = this->taxiAssignedTimes.find(cs);
            if (tIt == this->taxiAssignedTimes.end() || now - tIt->second >= 2000)
                toErase.push_back(cs);
        }
        for (const auto& cs : toErase)
        {
            this->taxiAssigned.erase(cs);
            this->taxiAssignedTimes.erase(cs);
        }

        HPEN pen  = CreatePen(PS_SOLID, 3, RGB(80, 220, 80));
        HPEN prev = static_cast<HPEN>(SelectObject(hDC, pen));
        for (const auto& [cs, route] : this->taxiAssigned)
        {
            if (!route.valid || route.polyline.empty())
                continue;
            bool first = true;
            for (const auto& gp : route.polyline)
            {
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = gp.lat;
                pos.m_Longitude = gp.lon;
                POINT pt        = ConvertCoordFromPositionToPixel(pos);
                if (first)
                {
                    MoveToEx(hDC, pt.x, pt.y, nullptr);
                    first = false;
                }
                else
                    LineTo(hDC, pt.x, pt.y);
            }
        }
        SelectObject(hDC, prev);
        DeleteObject(pen);
    }

    // Draw persistent tracked routes (remaining portion from current aircraft position).
    // Helper: draws one tracked route clipped to the remaining leg; returns true if done.
    auto drawTrackedRoute = [&](HDC dc, const std::string& cs, const TaxiRoute& route,
                                std::vector<std::string>& done) -> void
    {
        if (!route.valid || route.polyline.size() < 2)
            return;
        auto rt = GetPlugIn()->RadarTargetSelect(cs.c_str());
        if (!rt.IsValid() || !rt.GetPosition().IsValid())
            return;

        const EuroScopePlugIn::CPosition rawPos = rt.GetPosition().GetPosition();
        const GeoPoint                   acGeo{rawPos.m_Latitude, rawPos.m_Longitude};
        const POINT                      acPt        = ConvertCoordFromPositionToPixel(rawPos);
        size_t                           closestIdx  = 0;
        long                             closestDist = LONG_MAX;
        for (size_t i = 0; i < route.polyline.size(); ++i)
        {
            EuroScopePlugIn::CPosition pos;
            pos.m_Latitude  = route.polyline[i].lat;
            pos.m_Longitude = route.polyline[i].lon;
            const POINT pt  = ConvertCoordFromPositionToPixel(pos);
            const long  dx  = pt.x - acPt.x;
            const long  dy  = pt.y - acPt.y;
            const long  dSq = dx * dx + dy * dy;
            if (dSq < closestDist)
            {
                closestDist = dSq;
                closestIdx  = i;
            }
        }

        // (c) Airborne fallback: aircraft is above field elevation + margin.
        // Clears stale routes after takeoff (fast-forwarded replay, missed TAKE OFF press, etc.).
        auto*         settings           = static_cast<CFlowX_Settings*>(this->GetPlugIn());
        const int     fieldElevFt        = settings->GetAirports().empty()
                                               ? 0
                                               : settings->GetAirports().begin()->second.fieldElevation;
        constexpr int AIRBORNE_MARGIN_FT = 50;
        if (rt.GetPosition().GetPressureAltitude() > fieldElevFt + AIRBORNE_MARGIN_FT)
        {
            done.push_back(cs);
            return;
        }

        if (closestIdx == route.polyline.size() - 1)
        {
            const GeoPoint& finalPt = route.polyline[closestIdx];

            // (a) Aircraft is geographically close to the final node.
            constexpr double ARRIVAL_THRESH_M = 80.0;
            const bool       nearEnd          = HaversineM(acGeo, finalPt) <= ARRIVAL_THRESH_M;

            // (b) Aircraft has passed the final node (skipped over due to infrequent
            //     position updates): dot(A→B, B→P) > 0.
            bool pastEnd = false;
            if (route.polyline.size() >= 2)
            {
                const GeoPoint& prevPt = route.polyline[closestIdx - 1];
                const double    abLat  = finalPt.lat - prevPt.lat;
                const double    abLon  = finalPt.lon - prevPt.lon;
                const double    bpLat  = acGeo.lat - finalPt.lat;
                const double    bpLon  = acGeo.lon - finalPt.lon;
                pastEnd                = (abLat * bpLat + abLon * bpLon) > 0.0;
            }

            if (nearEnd || pastEnd)
            {
                done.push_back(cs);
                return;
            }
        }

        // Connector from aircraft to nearest route point: yellow to indicate
        // the aircraft is off-network or has deviated from the assigned route.
        EuroScopePlugIn::CPosition snapPos;
        snapPos.m_Latitude  = route.polyline[closestIdx].lat;
        snapPos.m_Longitude = route.polyline[closestIdx].lon;
        const POINT snapPt  = ConvertCoordFromPositionToPixel(snapPos);
        if (acPt.x != snapPt.x || acPt.y != snapPt.y)
        {
            HPEN yellowPen = CreatePen(PS_SOLID, 2, RGB(255, 220, 0));
            HPEN savedPen  = static_cast<HPEN>(SelectObject(dc, yellowPen));
            MoveToEx(dc, acPt.x, acPt.y, nullptr);
            LineTo(dc, snapPt.x, snapPt.y);
            SelectObject(dc, savedPen);
            DeleteObject(yellowPen);
        }

        // Remaining route in the caller's pen colour (green).
        MoveToEx(dc, snapPt.x, snapPt.y, nullptr);
        for (size_t i = closestIdx + 1; i < route.polyline.size(); ++i)
        {
            EuroScopePlugIn::CPosition pos;
            pos.m_Latitude  = route.polyline[i].lat;
            pos.m_Longitude = route.polyline[i].lon;
            const POINT pt  = ConvertCoordFromPositionToPixel(pos);
            LineTo(dc, pt.x, pt.y);
        }
    };

    // Expire the hover state after 500 ms so it clears when the mouse moves away.
    constexpr ULONGLONG HOVER_MS = 1000;
    if (!this->hoveredTaxiTarget.empty() &&
        GetTickCount64() - this->hoveredTaxiTargetTick >= HOVER_MS)
        this->hoveredTaxiTarget.clear();

    // Draw hovered aircraft route individually (visible even when showTaxiRoutes is off).
    std::vector<std::string> routesDone;
    if (!this->hoveredTaxiTarget.empty())
    {
        auto it = this->taxiTracked.find(this->hoveredTaxiTarget);
        if (it != this->taxiTracked.end())
        {
            HPEN pen  = CreatePen(PS_SOLID, 3, RGB(140, 255, 140));
            HPEN prev = static_cast<HPEN>(SelectObject(hDC, pen));
            drawTrackedRoute(hDC, it->first, it->second, routesDone);
            SelectObject(hDC, prev);
            DeleteObject(pen);
        }
        else if (!this->pushTracked.count(this->hoveredTaxiTarget))
            this->hoveredTaxiTarget.clear(); // push-only aircraft: keep for DrawPlanningRoutes
    }

    if (!this->showTaxiRoutes || this->taxiTracked.empty())
    {
        for (const auto& cs : routesDone)
        {
            this->taxiTracked.erase(cs);
            this->taxiAssigned.erase(cs);
            this->taxiAssignedTimes.erase(cs);
            this->taxiAssignedPos.erase(cs);
        }
        return;
    }

    HPEN pen  = CreatePen(PS_SOLID, 2, RGB(80, 220, 80));
    HPEN prev = static_cast<HPEN>(SelectObject(hDC, pen));

    for (const auto& [cs, route] : this->taxiTracked)
    {
        if (cs == this->hoveredTaxiTarget)
            continue; // already drawn above with highlight pen
        drawTrackedRoute(hDC, cs, route, routesDone);
    }

    SelectObject(hDC, prev);
    DeleteObject(pen);

    for (const auto& cs : routesDone)
    {
        this->taxiTracked.erase(cs);
        this->taxiAssigned.erase(cs);
        this->taxiAssignedTimes.erase(cs);
        this->taxiAssignedPos.erase(cs);
    }
}

/// @brief Draws active push routes, the yellow suggested route, and the magenta/light-blue planning preview.
void RadarScreen::DrawPlanningRoutes(HDC hDC)
{
    // Draw push routes: only when "Show routes" is on or the pushing aircraft is hovered.
    // Red during taxi-planning mode (active obstacle), orange otherwise.
    if (!this->pushTracked.empty())
    {
        const bool     inTaxiPlan = !this->taxiPlanActive.empty() && !this->taxiPlanIsPush;
        const COLORREF pushCol    = inTaxiPlan ? RGB(220, 50, 50) : RGB(255, 140, 0);
        const COLORREF pushHovCol = inTaxiPlan ? RGB(255, 80, 80) : RGB(255, 180, 60);
        const bool     hoverFresh =
            !this->hoveredTaxiTarget.empty() &&
            GetTickCount64() - this->hoveredTaxiTargetTick < 1000ULL;
        const bool hoverIsPush =
            hoverFresh && this->pushTracked.count(this->hoveredTaxiTarget);

        if (hoverIsPush)
            this->DrawRoutePolyline(hDC, this->pushTracked.at(this->hoveredTaxiTarget),
                                    pushHovCol, 3);

        if (this->showTaxiRoutes)
            for (const auto& [cs, pushRoute] : this->pushTracked)
            {
                if (cs == this->hoveredTaxiTarget)
                    continue; // already drawn above with highlight colour
                this->DrawRoutePolyline(hDC, pushRoute, pushCol, 3);
            }
    }

    if (this->taxiPlanActive.empty())
        return;

    if (!this->taxiPlanIsPush)
    {
        auto sugIt = this->taxiSuggested.find(this->taxiPlanActive);
        if (sugIt != this->taxiSuggested.end())
            this->DrawRoutePolyline(hDC, sugIt->second, RGB(255, 220, 0), 2); // yellow suggestion
    }

    const COLORREF previewCol = this->taxiPlanIsPush ? RGB(100, 200, 255) : RGB(255, 0, 220);
    this->DrawRoutePolyline(hDC, this->taxiGreenPreview, previewCol, 3); // light-blue (push) or magenta (taxi)

    // Draw the middle-drag intent path as a thin orange polyline while the gesture is active.
    if (this->taxiMidDrawing && this->taxiDrawPolyline.size() >= 2)
    {
        HPEN drawPen = CreatePen(PS_SOLID, 3, RGB(0, 240, 255));
        HPEN oldPen  = static_cast<HPEN>(SelectObject(hDC, drawPen));
        for (size_t pi = 1; pi < this->taxiDrawPolyline.size(); ++pi)
        {
            EuroScopePlugIn::CPosition pa, pb;
            pa.m_Latitude  = this->taxiDrawPolyline[pi - 1].lat;
            pa.m_Longitude = this->taxiDrawPolyline[pi - 1].lon;
            pb.m_Latitude  = this->taxiDrawPolyline[pi].lat;
            pb.m_Longitude = this->taxiDrawPolyline[pi].lon;
            POINT ptA      = ConvertCoordFromPositionToPixel(pa);
            POINT ptB      = ConvertCoordFromPositionToPixel(pb);
            MoveToEx(hDC, ptA.x, ptA.y, nullptr);
            LineTo(hDC, ptB.x, ptB.y);
        }
        SelectObject(hDC, oldPen);
        DeleteObject(drawPen);
    }

    // Draw via-point markers as small cyan squares.
    for (const auto& wp : this->taxiWaypoints)
    {
        EuroScopePlugIn::CPosition wpos;
        wpos.m_Latitude  = wp.lat;
        wpos.m_Longitude = wp.lon;
        POINT  pt        = ConvertCoordFromPositionToPixel(wpos);
        HPEN   wpen      = CreatePen(PS_SOLID, 1, RGB(0, 220, 220));
        HBRUSH wbrush    = CreateSolidBrush(RGB(0, 220, 220));
        SelectObject(hDC, wpen);
        SelectObject(hDC, wbrush);
        Rectangle(hDC, pt.x - 4, pt.y - 4, pt.x + 4, pt.y + 4);
        DeleteObject(wbrush);
        DeleteObject(wpen);
    }

    this->DrawPushDeadEnds(hDC);
}

/// @brief Draws a green filled square near each landed inbound awaiting GND handoff; registers each as a clickable screen object.
void RadarScreen::DrawGndTransferSquares(HDC hDC)
{
    if (this->gndTransferSquares.empty())
    {
        return;
    }

    SetBkMode(hDC, TRANSPARENT);
    ULONGLONG now = GetTickCount64();

    for (const auto& callSign : this->gndTransferSquares)
    {
        EuroScopePlugIn::CRadarTarget rt = GetPlugIn()->RadarTargetSelect(callSign.c_str());
        if (!rt.IsValid() || !rt.GetPosition().IsValid())
        {
            continue;
        }

        ULONGLONG elapsedSec = this->gndTransferSquareTimes.contains(callSign)
                                   ? (now - this->gndTransferSquareTimes.at(callSign)) / 1000
                                   : 0;
        COLORREF  color      = elapsedSec >= 35   ? TAG_COLOR_RED
                               : elapsedSec >= 25 ? TAG_COLOR_YELLOW
                                                  : TAG_COLOR_GREEN;

        auto brush = CreateSolidBrush(color);
        auto pen   = CreatePen(PS_SOLID, 1, color);
        SelectObject(hDC, brush);
        SelectObject(hDC, pen);

        POINT pt = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());
        RECT  sq = {pt.x - 22, pt.y + 10, pt.x - 10, pt.y + 22};
        Rectangle(hDC, sq.left, sq.top, sq.right, sq.bottom);
        AddScreenObject(SCREEN_OBJECT_GND_TRANSFER, callSign.c_str(), sq, true, "");

        DeleteObject(pen);
        DeleteObject(brush);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Taxi safety monitoring
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Returns a GeoPoint offset from @p origin by @p distM metres along @p bearingDeg.
static GeoPoint PushZonePoint(const GeoPoint& origin, double bearingDeg, double distM)
{
    constexpr double PI   = 3.14159265358979323846;
    constexpr double R    = 6371000.0;
    const double     d    = distM / R;
    const double     lat1 = origin.lat * PI / 180.0;
    const double     lon1 = origin.lon * PI / 180.0;
    const double     brng = bearingDeg * PI / 180.0;
    const double     lat2 = std::asin(std::sin(lat1) * std::cos(d) +
                                      std::cos(lat1) * std::sin(d) * std::cos(brng));
    const double     lon2 =
        lon1 + std::atan2(std::sin(brng) * std::sin(d) * std::cos(lat1),
                          std::cos(d) - std::sin(lat1) * std::sin(lat2));
    return {lat2 * 180.0 / PI, lon2 * 180.0 / PI};
}

/// @brief Builds a push-zone polyline entirely on the taxiway network.
///
/// Walks @p armADistM in @p armABrng and @p armBDistM in @p armBBrng from @p pivot,
/// then concatenates: armB-end → pivot → armA-end.
static TaxiRoute BuildPushZone(const TaxiGraph& graph, const GeoPoint& pivot,
                               double armABrng, double armADistM,
                               double armBBrng, double armBDistM,
                               double wingspanM = 0.0)
{
    TaxiRoute armA = graph.WalkGraph(pivot, armABrng, armADistM, wingspanM);
    TaxiRoute armB = graph.WalkGraph(pivot, armBBrng, armBDistM, wingspanM);

    TaxiRoute combined;
    combined.valid = armA.valid || armB.valid;

    // Lay out: reverse(armB) then armA, skipping the duplicate pivot point.
    if (armB.valid && !armB.polyline.empty())
        combined.polyline.insert(combined.polyline.end(), armB.polyline.rbegin(), armB.polyline.rend());

    if (armA.valid && !armA.polyline.empty())
    {
        auto start = armA.polyline.cbegin();
        if (!combined.polyline.empty())
            ++start; // first point of armA == pivot, already present
        combined.polyline.insert(combined.polyline.end(), start, armA.polyline.cend());
    }

    return combined;
}

void RadarScreen::UpdateTaxiSafety()
{
    // Throttle: recompute at most once every 250 ms.
    const ULONGLONG now = GetTickCount64();
    if (now - this->taxiSafetyLastTickMs < 250)
        return;
    this->taxiSafetyLastTickMs = now;

    this->taxiDeviations.clear();
    this->taxiConflicts.clear();

    // Auto-clear taxi route when aircraft enters any holding point on its departure runway.
    {
        auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
        if (!settings->GetAirports().empty())
        {
            const auto&              ap = settings->GetAirports().begin()->second;
            std::vector<std::string> toErase;
            for (const auto& [cs, route] : this->taxiTracked)
            {
                auto rt = GetPlugIn()->RadarTargetSelect(cs.c_str());
                if (!rt.IsValid() || !rt.GetPosition().IsValid())
                    continue;
                auto fp = GetPlugIn()->FlightPlanSelect(cs.c_str());
                if (!fp.IsValid())
                    continue;
                std::string depRwy = fp.GetFlightPlanData().GetDepartureRwy();
                auto        rwyIt  = ap.runways.find(depRwy);
                if (rwyIt == ap.runways.end())
                    continue;
                const auto pos = rt.GetPosition().GetPosition();
                for (const auto& [hpName, hp] : rwyIt->second.holdingPoints)
                {
                    if (hp.lat.empty())
                        continue;
                    if (CFlowX_LookupsTools::PointInsidePolygon(
                            static_cast<int>(hp.lat.size()),
                            const_cast<double*>(hp.lon.data()),
                            const_cast<double*>(hp.lat.data()),
                            pos.m_Longitude, pos.m_Latitude))
                    {
                        toErase.push_back(cs);
                        break;
                    }
                }
            }
            for (const auto& cs : toErase)
            {
                this->taxiTracked.erase(cs);
                this->taxiAssigned.erase(cs);
                this->taxiAssignedTimes.erase(cs);
                this->taxiAssignedPos.erase(cs);
            }
        }
    }

    if (this->taxiTracked.empty())
        return;

    static const TaxiNetworkConfig kDefaultNC{};
    auto*                          safetySettings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    const TaxiNetworkConfig&       nc             = safetySettings->GetAirports().empty()
                                                        ? kDefaultNC
                                                        : safetySettings->GetAirports().begin()->second.taxiNetworkConfig;

    const double     DEVIATION_THRESH_M = nc.safety.deviationThreshM;
    const double     MIN_GS_KT          = nc.safety.minSpeedKt;
    constexpr double KT_TO_MS           = 0.514444;
    const double     MAX_PREDICT_S      = nc.safety.maxPredictS;
    const double     CONFLICT_DELTA_S   = nc.safety.conflictDeltaS;
    const double     SAME_DIR_DEG       = nc.safety.sameDirDeg;

    struct TimedPt
    {
        GeoPoint pos;
        double   t; // seconds from now
    };
    std::map<std::string, std::vector<TimedPt>> timedPaths;

    for (const auto& [cs, route] : this->taxiTracked)
    {
        if (!route.valid || route.polyline.size() < 2)
            continue;
        auto rt = GetPlugIn()->RadarTargetSelect(cs.c_str());
        if (!rt.IsValid() || !rt.GetPosition().IsValid())
            continue;

        const double   gs_kt = rt.GetPosition().GetReportedGS();
        const GeoPoint acPos{rt.GetPosition().GetPosition().m_Latitude,
                             rt.GetPosition().GetPosition().m_Longitude};

        // ── Deviation check ──────────────────────────────────────────────────
        // Suppress until the aircraft's position has changed from where it was when
        // the route was assigned — ensures the first position update after confirmation
        // is evaluated rather than the same stale position that was used during planning.
        constexpr double ASSIGN_POS_THRESH_M = 5.0;
        auto             assignPosIt         = this->taxiAssignedPos.find(cs);
        bool             suppressDeviation   = false;
        if (assignPosIt != this->taxiAssignedPos.end())
        {
            if (HaversineM(acPos, assignPosIt->second) >= ASSIGN_POS_THRESH_M)
                this->taxiAssignedPos.erase(assignPosIt); // position has moved — start checking
            else
                suppressDeviation = true;
        }
        if (!suppressDeviation)
        {
            double minDist = std::numeric_limits<double>::max();
            for (size_t i = 1; i < route.polyline.size(); ++i)
                minDist = std::min(minDist,
                                   PointToSegmentDistM(acPos, route.polyline[i - 1], route.polyline[i]));
            if (gs_kt > MIN_GS_KT && minDist > DEVIATION_THRESH_M)
                this->taxiDeviations.insert(cs);
        }

        // ── Build timed predicted path ───────────────────────────────────────
        if (gs_kt <= MIN_GS_KT)
            continue;
        const double gs_ms = gs_kt * KT_TO_MS;

        // Find the closest segment by perpendicular distance.
        size_t bestSeg  = 0;
        double bestDist = std::numeric_limits<double>::max();
        for (size_t i = 1; i < route.polyline.size(); ++i)
        {
            double d = PointToSegmentDistM(acPos, route.polyline[i - 1], route.polyline[i]);
            if (d < bestDist)
            {
                bestDist = d;
                bestSeg  = i; // index of the B-end node of the closest segment
            }
        }

        std::vector<TimedPt> path;
        path.push_back({acPos, 0.0});
        double t = 0.0;

        // Distance from aircraft position to the next node (B-end of closest segment).
        t += HaversineM(acPos, route.polyline[bestSeg]) / gs_ms;
        if (t <= MAX_PREDICT_S)
            path.push_back({route.polyline[bestSeg], t});

        for (size_t i = bestSeg + 1; i < route.polyline.size(); ++i)
        {
            t += HaversineM(route.polyline[i - 1], route.polyline[i]) / gs_ms;
            if (t > MAX_PREDICT_S)
                break;
            path.push_back({route.polyline[i], t});
        }

        if (path.size() >= 2)
            timedPaths[cs] = std::move(path);
    }

    // ── Pairwise conflict check ───────────────────────────────────────────────
    std::vector<std::string> keys;
    keys.reserve(timedPaths.size());
    for (const auto& [cs, _] : timedPaths)
        keys.push_back(cs);

    // Pre-compute per-path axis-aligned bounding boxes for the path-level AABB guard.
    struct PathBBox
    {
        double minLat, maxLat, minLon, maxLon;
    };
    std::map<std::string, PathBBox> pathBBoxes;
    for (const auto& [cs, path] : timedPaths)
    {
        PathBBox bb{path[0].pos.lat, path[0].pos.lat, path[0].pos.lon, path[0].pos.lon};
        for (const auto& tp : path)
        {
            bb.minLat = std::min(bb.minLat, tp.pos.lat);
            bb.maxLat = std::max(bb.maxLat, tp.pos.lat);
            bb.minLon = std::min(bb.minLon, tp.pos.lon);
            bb.maxLon = std::max(bb.maxLon, tp.pos.lon);
        }
        pathBBoxes[cs] = bb;
    }

    for (size_t i = 0; i < keys.size(); ++i)
    {
        const auto&    pathA = timedPaths[keys[i]];
        const PathBBox bbA   = pathBBoxes[keys[i]];
        for (size_t j = i + 1; j < keys.size(); ++j)
        {
            const auto&    pathB = timedPaths[keys[j]];
            const PathBBox bbB   = pathBBoxes[keys[j]];

            // Path-level AABB: skip all segment work when predicted paths don't overlap spatially.
            if (bbA.maxLat < bbB.minLat || bbA.minLat > bbB.maxLat ||
                bbA.maxLon < bbB.minLon || bbA.minLon > bbB.maxLon)
                continue;

            for (size_t a = 1; a < pathA.size(); ++a)
            {
                for (size_t b = 1; b < pathB.size(); ++b)
                {
                    // Segment-level AABB: skip SegmentIntersectGeo for non-overlapping segments.
                    if (std::max(pathA[a - 1].pos.lat, pathA[a].pos.lat) <
                            std::min(pathB[b - 1].pos.lat, pathB[b].pos.lat) ||
                        std::min(pathA[a - 1].pos.lat, pathA[a].pos.lat) >
                            std::max(pathB[b - 1].pos.lat, pathB[b].pos.lat) ||
                        std::max(pathA[a - 1].pos.lon, pathA[a].pos.lon) <
                            std::min(pathB[b - 1].pos.lon, pathB[b].pos.lon) ||
                        std::min(pathA[a - 1].pos.lon, pathA[a].pos.lon) >
                            std::max(pathB[b - 1].pos.lon, pathB[b].pos.lon))
                        continue;

                    GeoPoint isxPt;
                    if (!SegmentIntersectGeo(pathA[a - 1].pos, pathA[a].pos,
                                             pathB[b - 1].pos, pathB[b].pos, isxPt))
                        continue;

                    // Same-direction exclusion.
                    const double bearA = BearingDeg(pathA[a - 1].pos, pathA[a].pos);
                    const double bearB = BearingDeg(pathB[b - 1].pos, pathB[b].pos);
                    if (BearingDiff(bearA, bearB) < SAME_DIR_DEG)
                        continue;

                    // Interpolate arrival time at the intersection.
                    const double distA = HaversineM(pathA[a - 1].pos, pathA[a].pos);
                    const double fracA = (distA > 0.1)
                                             ? std::clamp(HaversineM(pathA[a - 1].pos, isxPt) / distA, 0.0, 1.0)
                                             : 0.0;
                    const double tA    = pathA[a - 1].t + fracA * (pathA[a].t - pathA[a - 1].t);

                    const double distB = HaversineM(pathB[b - 1].pos, pathB[b].pos);
                    const double fracB = (distB > 0.1)
                                             ? std::clamp(HaversineM(pathB[b - 1].pos, isxPt) / distB, 0.0, 1.0)
                                             : 0.0;
                    const double tB    = pathB[b - 1].t + fracB * (pathB[b].t - pathB[b - 1].t);

                    if (std::abs(tA - tB) < CONFLICT_DELTA_S)
                        this->taxiConflicts.push_back({keys[i], keys[j], isxPt, tA, tB, a, b});
                }
            }
        }
    }

    // ── Sound trigger: play once per conflict pair after it persists in <15 s window for 2 s ──
    std::set<std::string> activeKeys;
    std::set<std::string> under15Keys;
    for (const auto& c : this->taxiConflicts)
    {
        const std::string key = std::min(c.csA, c.csB) + "|" + std::max(c.csA, c.csB);
        activeKeys.insert(key);
        if (std::min(c.tA, c.tB) < 15.0)
            under15Keys.insert(key);
    }

    // Track first-seen timestamp for each under-15s conflict; remove entries that left the window.
    for (const auto& key : under15Keys)
    {
        if (!this->taxiConflictFirstSeen.contains(key))
            this->taxiConflictFirstSeen[key] = now;
    }
    std::erase_if(this->taxiConflictFirstSeen,
                  [&](const auto& kv)
                  { return !under15Keys.contains(kv.first); });

    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    for (const auto& key : under15Keys)
    {
        if (!this->taxiConflictSoundPlayed.contains(key) &&
            now - this->taxiConflictFirstSeen[key] >= 2000)
        {
            if (settings->GetSoundTaxiConflict())
            {
                const std::filesystem::path wav =
                    std::filesystem::path(GetPluginDirectory()) / "taxiConflict.wav";
                PlaySoundA(wav.string().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            }
            this->taxiConflictSoundPlayed.insert(key);
            break; // at most one sound per cycle
        }
    }
    std::erase_if(this->taxiConflictSoundPlayed,
                  [&](const std::string& k)
                  { return !activeKeys.contains(k); });
}

void RadarScreen::DrawTaxiGraph(HDC hDC)
{
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settings->osmGraph.IsBuilt())
        return;

    const auto& nodes = settings->osmGraph.Nodes();
    const auto& adj   = settings->osmGraph.Adj();

    auto toPixel = [&](const GeoPoint& gp) -> POINT
    {
        EuroScopePlugIn::CPosition pos;
        pos.m_Latitude  = gp.lat;
        pos.m_Longitude = gp.lon;
        return ConvertCoordFromPositionToPixel(pos);
    };

    // Pass 1 — edges, coloured by cost multiplier (cost / distance in metres).
    //   < 1×  green  — preferred / with-flow discount
    //   ~1×   white  — neutral
    //   ~3×   yellow — against generic flow or taxilane
    //   ~9×   orange — taxilane against flow
    //   ≥15×  red    — runway
    struct CostBand
    {
        double   maxMult;
        COLORREF col;
    };
    constexpr CostBand BANDS[] = {
        {1.0, RGB(0, 200, 100)},   // green
        {2.0, RGB(255, 255, 255)}, // white
        {5.0, RGB(255, 220, 0)},   // yellow
        {14.0, RGB(255, 130, 0)},  // orange
        {1e9, RGB(220, 50, 50)},   // red
    };
    constexpr int NBAND = static_cast<int>(std::size(BANDS));
    HPEN          costPens[NBAND];
    for (int b = 0; b < NBAND; ++b)
        costPens[b] = CreatePen(PS_SOLID, 2, BANDS[b].col);

    HPEN prevPen = static_cast<HPEN>(SelectObject(hDC, costPens[1]));
    int  curBand = 1;

    for (int i = 0; i < static_cast<int>(adj.size()); ++i)
    {
        const POINT ptA = toPixel(nodes[i].pos);
        for (const auto& e : adj[i])
        {
            if (e.to <= i)
                continue; // draw each edge once

            const double distM = HaversineM(nodes[i].pos, nodes[e.to].pos);
            const double mult  = (distM > 0.01) ? e.cost / distM : 1.0;

            int band = NBAND - 1;
            for (int b = 0; b < NBAND; ++b)
            {
                if (mult < BANDS[b].maxMult)
                {
                    band = b;
                    break;
                }
            }
            if (band != curBand)
            {
                SelectObject(hDC, costPens[band]);
                curBand = band;
            }

            const POINT ptB = toPixel(nodes[e.to].pos);
            MoveToEx(hDC, ptA.x, ptA.y, nullptr);
            LineTo(hDC, ptB.x, ptB.y);
        }
    }
    SelectObject(hDC, prevPen);
    for (int b = 0; b < NBAND; ++b)
        DeleteObject(costPens[b]);

    // Pass 2 — nodes: 6 × 6 px square coloured by type.
    //   Waypoint       → cyan
    //   HoldingPosition→ red
    //   Stand          → green  (added lazily; only visible after a route to a stand is computed)
    //   HoldingPoint   → not drawn (config centroids; not useful in visual debug)
    for (const auto& n : nodes)
    {
        if (n.type == TaxiNodeType::HoldingPoint)
            continue;
        const COLORREF col = (n.type == TaxiNodeType::HoldingPosition) ? RGB(220, 50, 50)
                             : (n.type == TaxiNodeType::Stand)         ? RGB(50, 220, 50)
                                                                       : RGB(0, 200, 255);
        const POINT    pt  = toPixel(n.pos);
        HBRUSH         br  = CreateSolidBrush(col);
        const RECT     r   = {pt.x - 3, pt.y - 3, pt.x + 3, pt.y + 3};
        FillRect(hDC, &r, br);
        DeleteObject(br);
    }

    // Pass 3 — flow chevrons: one per directed edge that is "with flow" for the active config.
    // Each chevron is two short lines forming a ">" at the edge midpoint, pointing in the
    // direction of travel.  Only edges longer than MIN_PX pixels on screen are annotated to
    // avoid cluttering heavily-subdivided short segments.
    const auto activeFlowRules = settings->osmGraph.GetActiveFlowRules(
        settings->GetActiveDepRunways(), settings->GetActiveArrRunways());

    if (!activeFlowRules.empty())
    {
        // Cardinal direction → bearing (degrees).
        auto cardinalBearing = [](const std::string& dir) -> double
        {
            if (dir == "N")
                return 0.0;
            if (dir == "E")
                return 90.0;
            if (dir == "S")
                return 180.0;
            if (dir == "W")
                return 270.0;
            return -1.0;
        };

        constexpr double MIN_PX     = 8.0;  // skip chevron on very short screen segments
        constexpr double CHEVRON_S  = 8.0;  // half-arm length in pixels
        constexpr double SPACING_PX = 80.0; // minimum screen distance between chevrons on the same wayRef

        // Pre-build a map from wayRef → rule bearing for O(1) lookup inside the loop.
        std::map<std::string, double> refBearing;
        for (const auto& rule : activeFlowRules)
        {
            const double b = cardinalBearing(rule.direction);
            if (b >= 0.0)
                refBearing.emplace(rule.taxiway, b);
        }

        // Last midpoint (screen px) where a chevron was drawn, keyed by wayRef.
        std::map<std::string, POINT> lastChevron;

        HPEN chevronPen  = CreatePen(PS_SOLID, 2, RGB(255, 215, 50));
        HPEN prevChevPen = static_cast<HPEN>(SelectObject(hDC, chevronPen));

        for (int i = 0; i < static_cast<int>(adj.size()); ++i)
        {
            const POINT ptA = toPixel(nodes[i].pos);
            for (const auto& e : adj[i])
            {
                if (e.wayRef.empty())
                    continue;

                const auto rIt = refBearing.find(e.wayRef);
                if (rIt == refBearing.end())
                    continue;

                // Only annotate edges whose bearing is within 45° of the rule (i.e. with-flow).
                if (BearingDiff(e.bearingDeg, rIt->second) > 45.0)
                    continue;

                const POINT  ptB = toPixel(nodes[e.to].pos);
                const double dx  = static_cast<double>(ptB.x - ptA.x);
                const double dy  = static_cast<double>(ptB.y - ptA.y);
                const double len = std::sqrt(dx * dx + dy * dy);
                if (len < MIN_PX)
                    continue;

                // Enforce minimum spacing between chevrons on the same wayRef.
                const double mx = (ptA.x + ptB.x) / 2.0;
                const double my = (ptA.y + ptB.y) / 2.0;
                if (const auto lIt = lastChevron.find(e.wayRef); lIt != lastChevron.end())
                {
                    const double sdx = mx - lIt->second.x;
                    const double sdy = my - lIt->second.y;
                    if (std::sqrt(sdx * sdx + sdy * sdy) < SPACING_PX)
                        continue;
                }
                lastChevron[e.wayRef] = {static_cast<LONG>(mx), static_cast<LONG>(my)};

                // Unit direction and perpendicular.
                const double ux = dx / len;
                const double uy = dy / len;
                const double px = -uy;
                const double py = ux;

                const POINT apex  = {static_cast<LONG>(mx + ux * CHEVRON_S),
                                     static_cast<LONG>(my + uy * CHEVRON_S)};
                const POINT barbL = {static_cast<LONG>(mx - ux * CHEVRON_S + px * CHEVRON_S),
                                     static_cast<LONG>(my - uy * CHEVRON_S + py * CHEVRON_S)};
                const POINT barbR = {static_cast<LONG>(mx - ux * CHEVRON_S - px * CHEVRON_S),
                                     static_cast<LONG>(my - uy * CHEVRON_S - py * CHEVRON_S)};

                MoveToEx(hDC, barbL.x, barbL.y, nullptr);
                LineTo(hDC, apex.x, apex.y);
                LineTo(hDC, barbR.x, barbR.y);
            }
        }

        SelectObject(hDC, prevChevPen);
        DeleteObject(chevronPen);
    }
}

void RadarScreen::DrawTaxiConflicts(HDC hDC)
{
    if (this->taxiConflicts.empty())
        return;

    HPEN prevPen = static_cast<HPEN>(GetCurrentObject(hDC, OBJ_PEN));

    for (const auto& c : this->taxiConflicts)
    {
        const double tMin = std::min(c.tA, c.tB);
        COLORREF     col;
        if (tMin < 15.0)
            col = RGB(220, 50, 50);
        else if (tMin < 30.0)
            col = RGB(255, 220, 0);
        else
            continue; // too far ahead — no marker yet

        EuroScopePlugIn::CPosition isxCPos;
        isxCPos.m_Latitude  = c.pt.lat;
        isxCPos.m_Longitude = c.pt.lon;
        const POINT px      = ConvertCoordFromPositionToPixel(isxCPos);

        HPEN mPen = CreatePen(PS_SOLID, 2, col);
        SelectObject(hDC, mPen);
        MoveToEx(hDC, px.x - 5, px.y - 5, nullptr);
        LineTo(hDC, px.x + 5, px.y + 5);
        MoveToEx(hDC, px.x + 5, px.y - 5, nullptr);
        LineTo(hDC, px.x - 5, px.y + 5);
        SelectObject(hDC, prevPen);
        DeleteObject(mPen);
    }
}

void RadarScreen::DrawTaxiWarningLabels(HDC hDC)
{
    if (this->taxiDeviations.empty() && this->taxiConflicts.empty())
        return;

    HFONT warnFont = CreateFontA(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, "EuroScope");
    HFONT prevFont = static_cast<HFONT>(SelectObject(hDC, warnFont));
    SetBkMode(hDC, TRANSPARENT);

    for (auto rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt      = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        auto fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid())
            continue;
        const std::string cs = fp.GetCallsign();

        // Three tiers: CRITICAL (<15 s) > WARN (15–30 s) > deviation.
        enum class Tier
        {
            None,
            Deviation,
            Warn,
            Critical
        } tier = Tier::None;

        for (const auto& c : this->taxiConflicts)
        {
            if (c.csA != cs && c.csB != cs)
                continue;
            const double tMin = std::min(c.tA, c.tB);
            if (tMin < 15.0)
            {
                tier = Tier::Critical;
                break;
            }
            if (tMin < 30.0 && tier < Tier::Warn)
                tier = Tier::Warn;
        }
        if (tier == Tier::None && this->taxiDeviations.contains(cs))
            tier = Tier::Deviation;
        if (tier == Tier::None)
            continue;

        const char*    label   = (tier == Tier::Deviation) ? "!ROUTE" : "CONFLICT";
        const COLORREF bgCol   = (tier == Tier::Critical) ? TAG_COLOR_RED : TAG_COLOR_YELLOW;
        const COLORREF textCol = (tier == Tier::Critical) ? RGB(255, 255, 255) : RGB(0, 0, 0);

        const POINT acPt = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());

        RECT m = {0, 0, 200, 30};
        DrawTextA(hDC, label, -1, &m, DT_CALCRECT | DT_SINGLELINE);
        constexpr int PAD = 1;
        constexpr int GAP = 14;

        const RECT bgRect = {acPt.x - m.right / 2 - PAD,
                             acPt.y - GAP - m.bottom - PAD * 2,
                             acPt.x + m.right / 2 + PAD,
                             acPt.y - GAP};

        HBRUSH brush = CreateSolidBrush(bgCol);
        FillRect(hDC, &bgRect, brush);
        DeleteObject(brush);

        SetTextColor(hDC, textCol);
        RECT textRect = bgRect;
        DrawTextA(hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hDC, prevFont);
    DeleteObject(warnFont);
}

/// @brief Highlights dead-end taxilane branches that are isolated by active push routes.
/// Called from BEFORE_TAGS only when in taxi planning mode (not push planning).
void RadarScreen::DrawPushDeadEnds(HDC hDC)
{
    if (this->taxiPlanIsPush || this->pushTracked.empty())
        return;

    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settings->osmGraph.IsBuilt())
        return;

    // Collect all blocked nodes from active push routes.
    std::set<int> blocked;
    for (const auto& [_, pushRoute] : this->pushTracked)
    {
        auto b = settings->osmGraph.NodesToBlock(pushRoute.polyline, 3.0);
        blocked.insert(b.begin(), b.end());
    }
    if (blocked.empty())
        return;

    // Re-derive planning destination (same logic as the right-click trigger).
    std::string ourIcao;
    if (!settings->GetAirports().empty())
        ourIcao = settings->GetAirports().begin()->first;

    auto fp = GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str());
    auto rt = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
    if (!fp.IsValid() || !rt.IsValid())
        return;

    std::string arrAirport = fp.GetFlightPlanData().GetDestination();
    to_upper(arrAirport);
    const bool isInbound = (!ourIcao.empty() && arrAirport == ourIcao);

    GeoPoint dest{0.0, 0.0};
    if (isInbound)
    {
        auto* timers  = static_cast<CFlowX_Timers*>(this->GetPlugIn());
        auto  standIt = timers->GetStandAssignment().find(this->taxiPlanActive);
        if (standIt != timers->GetStandAssignment().end())
        {
            const std::string& standName = standIt->second;
            const auto&        ap        = settings->GetAirports().begin()->second;
            auto               ovIt      = ap.standRoutingTargets.find(standName);
            if (ovIt != ap.standRoutingTargets.end())
            {
                GeoPoint hp = settings->osmGraph.HoldingPositionByLabel(ovIt->second);
                dest        = (hp.lat != 0.0 || hp.lon != 0.0)
                                  ? hp
                                  : TaxiGraph::StandApproachPoint(ourIcao + ":" + standName, settings->GetGrStands());
            }
            else
            {
                dest = TaxiGraph::StandApproachPoint(ourIcao + ":" + standName, settings->GetGrStands());
            }
        }
    }
    else if (!ourIcao.empty() && !settings->GetAirports().empty())
    {
        dest = settings->osmGraph.BestDepartureHP(settings->GetActiveDepRunways(),
                                                  settings->GetAirports().begin()->second);
    }
    if (dest.lat == 0.0 && dest.lon == 0.0)
        return; // no destination to check

    auto deadEdges = settings->osmGraph.DeadEndEdges(dest, blocked);
    if (deadEdges.empty())
        return;

    HPEN redPen  = CreatePen(PS_SOLID, 3, RGB(220, 50, 50));
    HPEN prevPen = static_cast<HPEN>(SelectObject(hDC, redPen));
    for (const auto& [a, b] : deadEdges)
    {
        EuroScopePlugIn::CPosition pa, pb;
        pa.m_Latitude   = a.lat;
        pa.m_Longitude  = a.lon;
        pb.m_Latitude   = b.lat;
        pb.m_Longitude  = b.lon;
        const POINT pta = ConvertCoordFromPositionToPixel(pa);
        const POINT ptb = ConvertCoordFromPositionToPixel(pb);
        MoveToEx(hDC, pta.x, pta.y, nullptr);
        LineTo(hDC, ptb.x, ptb.y);
    }
    SelectObject(hDC, prevPen);
    DeleteObject(redPen);
}

static void FillRectAlpha(HDC hDC, const RECT& rect, COLORREF color, int opacityPct); ///< Forward declaration — defined below DrawGndTransferSquares.

/// @brief Draws the Approach Estimate window: two vertical bars with a 300→0 s scale between them.
/// Left-side runways place their aircraft to the left of the left bar; right-side to the right of the right bar.
/// Runway labels appear at the top corners of the header row. Ticks every 5 s (short) and 10 s (long); labels every 20 s.
void RadarScreen::DrawApproachEstimateWindow(HDC hDC)
{
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settings->GetApproachEstVisible())
    {
        return;
    }

    const int fo         = settings->GetFontOffset();
    const int op         = settings->GetBgOpacity();
    const int TITLE_H    = 13;
    const int HDR_H      = 14 + fo; ///< Header row carrying the runway corner labels
    const int RESIZE_SZ  = 8;       ///< Size of the draggable resize square
    const int X_BTN      = 11;      ///< Close button size
    const int NUM_COL_W  = 48;      ///< Fixed width of the numbers column between the two bars
    const int TICK_LONG  = 10;      ///< Tick length (px into number column) for every-10-s marks
    const int TICK_SHORT = 8;       ///< Tick length for every-5-s (non-10-s) marks
    const int AC_PAD     = 4;       ///< Gap between the bar line and aircraft label text

    int WIN_W = this->approachEstWindowW;
    int WIN_H = this->approachEstWindowH;

    if (this->approachEstWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->approachEstWindowPos.x = clip.right - WIN_W - 20;
        this->approachEstWindowPos.y = (clip.top + clip.bottom) / 2 - WIN_H / 2;
    }

    int wx = this->approachEstWindowPos.x;
    int wy = this->approachEstWindowPos.y;

    // ── Geometry ──
    // Two bars flank a fixed-width numbers column, centred in the window.
    int barLx      = wx + (WIN_W - NUM_COL_W) / 2;
    int barRx      = barLx + NUM_COL_W;
    int numCenterX = barLx + NUM_COL_W / 2;

    int barTop    = wy + TITLE_H + HDR_H + 4;
    int barBottom = wy + WIN_H - RESIZE_SZ - 4;
    if (barBottom <= barTop)
    {
        barBottom = barTop + 1;
    }

    // Scale padding: inset the usable 300→0 range so aircraft text at the extremes stays readable.
    // Top pad: small margin above the 300-s marker line.
    // Bottom pad: enough for callsign + type below the 0-s marker line.
    const int SCALE_PAD_TOP    = 34;
    const int SCALE_PAD_BOTTOM = 34;
    int       scaleTop         = barTop + SCALE_PAD_TOP;
    int       scaleBottom      = barBottom - SCALE_PAD_BOTTOM;
    if (scaleBottom <= scaleTop)
    {
        scaleBottom = scaleTop + 1;
    }
    int barHeight = scaleBottom - scaleTop;

    // ── Close and popout button hover ──
    RECT  xRect   = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    RECT  popRect = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};
    POINT cursor  = {-9999, -9999};
    if (this->isPopoutRender_)
        cursor = this->popoutHoverPoint_;
    else
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursor);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursor);
    }
    bool xHovered                = PtInRect(&xRect, cursor) != 0;
    bool popHovered              = PtInRect(&popRect, cursor) != 0;
    this->winPopoutLastHoverType = -1;

    // ── Background ──
    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT hdrRect   = {wx, wy + TITLE_H, wx + WIN_W, wy + TITLE_H + HDR_H};

    FillRectAlpha(hDC, winRect, RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect, RGB(40, 40, 40), op);

    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }
    if (popHovered)
    {
        auto popBrush = CreateSolidBrush(RGB(40, 100, 160));
        FillRect(hDC, &popRect, popBrush);
        DeleteObject(popBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // ── Title bar ──
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    RECT dragRect = {wx, wy, popRect.left - 1, wy + TITLE_H};
    DrawTextA(hDC, "APPROACH ESTIMATE", -1, &dragRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // "^" sits near the top of its cell at small font sizes; nudge rect down 1 px.
    RECT popDrawRect = popRect;
    if (!this->isPopoutRender_)
        popDrawRect.top += 1;
    DrawTextA(hDC, this->isPopoutRender_ ? "v" : "^", -1, &popDrawRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);

    this->AddScreenObjectAuto(SCREEN_OBJECT_APPROACH_EST_WIN, "APPROACH_EST", dragRect, true, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_CLOSE, "approachEst", xRect, false, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_POPOUT, "approachEst", popRect, false, "");

    // ── Runway corner labels — always from config, not from active inbounds ──
    auto [leftLabel, rightLabel] = settings->GetEstimateBarLabels();

    // ── Header row: runway labels at top corners ──
    HFONT hdrFont = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevFont      = (HFONT)SelectObject(hDC, hdrFont);
    SetTextColor(hDC, TAG_COLOR_DEFAULT_GRAY);
    if (!leftLabel.empty())
    {
        RECT lr = {wx + 3, wy + TITLE_H, wx + WIN_W / 2, wy + TITLE_H + HDR_H};
        DrawTextA(hDC, leftLabel.c_str(), -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!rightLabel.empty())
    {
        RECT rr = {wx + WIN_W / 2, wy + TITLE_H, wx + WIN_W - 3, wy + TITLE_H + HDR_H};
        DrawTextA(hDC, rightLabel.c_str(), -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevFont);
    DeleteObject(hdrFont);

    // ── Two vertical bars ──
    auto    barPen  = CreatePen(PS_SOLID, 1, TAG_COLOR_WHITE);
    HGDIOBJ prevPen = SelectObject(hDC, barPen);
    MoveToEx(hDC, barLx, barTop, nullptr);
    LineTo(hDC, barLx, barBottom);
    MoveToEx(hDC, barRx, barTop, nullptr);
    LineTo(hDC, barRx, barBottom);
    SelectObject(hDC, prevPen);
    DeleteObject(barPen);

    // ── Tick marks and labels ──
    // Every 5 s: short tick inward from each bar; every 10 s: longer; labels every 20 s.
    HFONT tickFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevFont       = (HFONT)SelectObject(hDC, tickFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    auto tickPen = CreatePen(PS_SOLID, 1, TAG_COLOR_WHITE);
    prevPen      = SelectObject(hDC, tickPen);

    for (int t = 300; t >= 0; t -= 5)
    {
        int ty      = scaleTop + (300 - t) * barHeight / 300;
        int tickLen = (t % 10 == 0) ? TICK_LONG : TICK_SHORT;

        // Left bar: tick extends rightward (into numbers column)
        MoveToEx(hDC, barLx, ty, nullptr);
        LineTo(hDC, barLx + tickLen, ty);
        // Right bar: tick extends leftward (into numbers column)
        MoveToEx(hDC, barRx, ty, nullptr);
        LineTo(hDC, barRx - tickLen, ty);

        // Label every 20 s, centred in the numbers column
        if (t % 20 == 0)
        {
            std::string lbl = std::to_string(t);
            SIZE        sz  = {};
            GetTextExtentPoint32A(hDC, lbl.c_str(), (int)lbl.size(), &sz);
            TextOutA(hDC, numCenterX - sz.cx / 2, ty - sz.cy / 2, lbl.c_str(), (int)lbl.size());
        }
    }

    SelectObject(hDC, prevPen);
    DeleteObject(tickPen);
    SelectObject(hDC, prevFont);
    DeleteObject(tickFont);

    // ── Aircraft labels ──
    HFONT csFont    = CreateFontA(-16 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT atFont    = CreateFontA(-13 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    bool  useColors = settings->GetApprEstColors();

    auto markerPen = CreatePen(PS_SOLID, 1, TAG_COLOR_GREEN);

    for (const auto& row : this->twrInboundRowsCache)
    {
        if (row.tttSeconds < 0 || row.tttSeconds > 300)
        {
            continue;
        }
        if (row.rwy.empty())
        {
            continue;
        }

        std::string side = settings->GetRunwayEstimateBarSide(row.rwy);
        if (side != "left" && side != "right")
        {
            continue;
        }

        int clampedTTT = std::clamp(row.tttSeconds, 0, 300);
        int y          = scaleTop + (300 - clampedTTT) * barHeight / 300;
        y              = std::clamp(y, scaleTop, scaleBottom);

        // Determine colors — either always-green or inbound-list colors
        COLORREF csColor  = TAG_COLOR_GREEN;
        COLORREF bgColor  = TAG_COLOR_DEFAULT_NONE;
        COLORREF textOnBg = TAG_COLOR_GREEN;
        if (useColors)
        {
            if (row.isGoAround)
            {
                csColor  = TAG_COLOR_WHITE;
                bgColor  = TAG_BG_COLOR_RED;
                textOnBg = TAG_COLOR_WHITE;
            }
            else if (row.isFrozen)
            {
                csColor  = TAG_COLOR_BLACK;
                bgColor  = TAG_BG_COLOR_YELLOW;
                textOnBg = TAG_COLOR_BLACK;
            }
            else
            {
                csColor  = row.callsignColor;
                bgColor  = TAG_COLOR_DEFAULT_NONE;
                textOnBg = row.callsignColor;
            }
        }

        // Horizontal marker line — always green, same length as the 10-s tick (TICK_LONG)
        prevPen = SelectObject(hDC, markerPen);
        if (side == "left")
        {
            MoveToEx(hDC, barLx, y, nullptr);
            LineTo(hDC, barLx - TICK_LONG, y);
        }
        else
        {
            MoveToEx(hDC, barRx, y, nullptr);
            LineTo(hDC, barRx + TICK_LONG, y);
        }
        SelectObject(hDC, prevPen);

        // Callsign text — sits just below the marker line, adjacent to the bar
        prevFont    = (HFONT)SelectObject(hDC, csFont);
        SIZE csSize = {};
        GetTextExtentPoint32A(hDC, row.callsign.c_str(), (int)row.callsign.size(), &csSize);
        int csX = (side == "left") ? barLx - AC_PAD - csSize.cx : barRx + AC_PAD;
        int csY = y + 1;

        if (bgColor != TAG_COLOR_DEFAULT_NONE)
        {
            RECT   fillRect = {(LONG)(csX - 2), (LONG)(csY - 1), (LONG)(csX + csSize.cx + 2), (LONG)(csY + csSize.cy + 1)};
            HBRUSH bgBrush  = CreateSolidBrush(bgColor);
            FillRect(hDC, &fillRect, bgBrush);
            DeleteObject(bgBrush);
            SetTextColor(hDC, textOnBg);
        }
        else
        {
            SetTextColor(hDC, csColor);
        }
        TextOutA(hDC, csX, csY, row.callsign.c_str(), (int)row.callsign.size());
        SelectObject(hDC, prevFont);

        // Aircraft type — below the callsign, no background box
        if (!row.aircraftType.empty())
        {
            prevFont = (HFONT)SelectObject(hDC, atFont);
            SetTextColor(hDC, csColor);
            SIZE atSize = {};
            GetTextExtentPoint32A(hDC, row.aircraftType.c_str(), (int)row.aircraftType.size(), &atSize);
            int atX = (side == "left") ? barLx - AC_PAD - atSize.cx : barRx + AC_PAD;
            TextOutA(hDC, atX, csY + csSize.cy - 3, row.aircraftType.c_str(), (int)row.aircraftType.size());
            SelectObject(hDC, prevFont);
        }
    }

    DeleteObject(markerPen);
    DeleteObject(csFont);
    DeleteObject(atFont);

    // ── Resize handle (draggable grey square, lower-right corner) ──
    RECT resizeRect  = {wx + WIN_W - RESIZE_SZ, wy + WIN_H - RESIZE_SZ, wx + WIN_W, wy + WIN_H};
    auto resizeBrush = CreateSolidBrush(RGB(100, 100, 100));
    FillRect(hDC, &resizeRect, resizeBrush);
    DeleteObject(resizeBrush);
    this->AddScreenObjectAuto(SCREEN_OBJECT_APPROACH_EST_RESIZE, "APPROACH_EST_RESIZE", resizeRect, true, "");
}

/// @brief Draws per-aircraft departure info overlays (text, SID dot, HP label, connector line).
void RadarScreen::DrawDepartureInfoTag(HDC hDC)
{
    SetBkMode(hDC, TRANSPARENT);

    for (auto& [cs, info] : this->radarTargetDepartureInfos)
    {
        if (info.anchor.lat == 0.0 && info.anchor.lon == 0.0)
            continue;

        // Recompute pixel position from geo anchor every frame so panning/zooming moves the tag.
        EuroScopePlugIn::CPosition esPos;
        esPos.m_Latitude  = info.anchor.lat;
        esPos.m_Longitude = info.anchor.lon;
        POINT pos         = ConvertCoordFromPositionToPixel(esPos);
        pos.x -= 16;
        pos.y += 3;

        SetTextColor(hDC, info.dep_color);
        SIZE textSize;
        GetTextExtentPoint32A(hDC, info.dep_info.c_str(), (int)info.dep_info.length(), &textSize);
        TextOutA(hDC,
                 pos.x - textSize.cx + info.dragX,
                 pos.y + info.dragY,
                 info.dep_info.c_str(), (int)info.dep_info.length());

        RECT area;
        area.left   = pos.x - textSize.cx - 2 + info.dragX;
        area.top    = pos.y - 2 + info.dragY;
        area.right  = pos.x + 2 + info.dragX;
        area.bottom = pos.y + textSize.cy + 2 + info.dragY;

        auto sidBrush = CreateSolidBrush(info.sid_color);
        auto sidPen   = CreatePen(PS_SOLID, 1, info.sid_color);
        SelectObject(hDC, sidBrush);
        SelectObject(hDC, sidPen);
        RECT dotRect = {
            pos.x - textSize.cx + info.dragX + 2,
            pos.y + info.dragY + (area.bottom - area.top) - 5 + 2,
            pos.x - textSize.cx + info.dragX + 14,
            pos.y + info.dragY + (area.bottom - area.top) - 5 + 14};
        Ellipse(hDC, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
        DeleteObject(sidBrush);
        DeleteObject(sidPen);

        if (!info.hp_info.empty())
        {
            SetTextColor(hDC, info.hp_color);
            TextOutA(hDC,
                     pos.x - textSize.cx + 18 + info.dragX,
                     pos.y + info.dragY + (area.bottom - area.top) - 5,
                     info.hp_info.c_str(), (int)info.hp_info.length());
            area.bottom += (area.bottom - area.top) - 5;
        }

        auto pen = CreatePen(PS_SOLID, 1, info.dep_color);
        SelectObject(hDC, pen);
        MoveToEx(hDC, pos.x + 16, pos.y - 3, nullptr);
        if (area.right <= pos.x + 16)
            LineTo(hDC, area.right, area.top + (area.bottom - area.top) / 2);
        else
            LineTo(hDC, area.left, area.top + (area.bottom - area.top) / 2);
        DeleteObject(pen);

        if (info.queue_pos > 0)
        {
            std::string seqStr      = std::to_string(info.queue_pos);
            HFONT       seqFont     = CreateFontA(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
            HFONT       prevSeqFont = (HFONT)SelectObject(hDC, seqFont);
            SIZE        seqSize     = {};
            GetTextExtentPoint32A(hDC, seqStr.c_str(), (int)seqStr.length(), &seqSize);
            constexpr int PAD       = 3;
            constexpr int GAP       = 14; // distance from radar target dot to bottom of background box
            int           targetX   = pos.x + 16;
            int           targetY   = pos.y - 3;
            int           boxLeft   = targetX - seqSize.cx / 2 - PAD;
            int           boxTop    = targetY - GAP - seqSize.cy - PAD * 2;
            int           boxRight  = targetX + seqSize.cx / 2 + PAD + (seqSize.cx % 2 == 0 ? 0 : 1);
            int           boxBottom = targetY - GAP;
            RECT          bgRect    = {boxLeft, boxTop, boxRight, boxBottom};
            auto          bgBrush   = CreateSolidBrush(RGB(50, 50, 50));
            FillRect(hDC, &bgRect, bgBrush);
            DeleteObject(bgBrush);
            SetTextColor(hDC, info.dep_color);
            SetBkMode(hDC, TRANSPARENT);
            TextOutA(hDC, boxLeft + PAD, boxTop + PAD, seqStr.c_str(), (int)seqStr.length());
            SelectObject(hDC, prevSeqFont);
            DeleteObject(seqFont);
        }

        AddScreenObject(SCREEN_OBJECT_DEP_TAG, cs.c_str(), area, true, "");
        AddScreenObject(SCREEN_OBJECT_DEP_TAG_SID_DOT, (cs + "|SID_DOT").c_str(), dotRect, false, "");
    }
}

/// @brief Fills a rect with the given color at the specified opacity percent (20–100).
/// At 100% falls back to a plain FillRect. Otherwise uses AlphaBlend (Msimg32).
static void FillRectAlpha(HDC hDC, const RECT& rect, COLORREF color, int opacityPct)
{
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }
    if (opacityPct >= 100)
    {
        auto brush = CreateSolidBrush(color);
        FillRect(hDC, &rect, brush);
        DeleteObject(brush);
        return;
    }
    HDC     memDC  = CreateCompatibleDC(hDC);
    HBITMAP bmp    = CreateCompatibleBitmap(hDC, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);
    RECT    local  = {0, 0, w, h};
    auto    brush  = CreateSolidBrush(color);
    FillRect(memDC, &local, brush);
    DeleteObject(brush);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, static_cast<BYTE>(opacityPct * 255 / 100), 0};
    AlphaBlend(hDC, rect.left, rect.top, w, h, memDC, 0, 0, w, h, bf);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
}

/// @brief Draws the DEP/H departure-rate window from the pre-calculated depRateRowsCache.
void RadarScreen::DrawDepRateWindow(HDC hDC)
{
    auto* settingsDep = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsDep->GetDepRateVisible())
    {
        return;
    }
    int fo = settingsDep->GetFontOffset();
    int op = settingsDep->GetBgOpacity();

    const int TITLE_H = 13;
    const int HDR_H   = 17 + fo;
    const int ROW_H   = 15 + fo;
    const int WIN_PAD = 6;
    // Base column widths at font size 12; scaled by (12+fo)/12:
    auto cw = [&](int base) -> int
    { return base * (12 + fo) / 12; };
    const int COL_RWY = cw(35);
    const int COL_CNT = cw(20);
    const int COL_GAP = 6;
    const int COL_SPC = cw(56);
    const int WIN_W   = WIN_PAD + COL_RWY + COL_CNT + COL_GAP + COL_SPC + WIN_PAD;
    const int X_BTN   = 11; ///< Width and height of the title-bar close button
    int       numRows = (int)this->depRateRowsCache.size();
    const int WIN_H   = TITLE_H + HDR_H + numRows * ROW_H + WIN_PAD / 2;

    this->depRateWindowW = WIN_W;
    this->depRateWindowH = WIN_H;

    if (this->depRateWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->depRateWindowPos.x = clip.right - WIN_W - 20;
        this->depRateWindowPos.y = clip.bottom - WIN_H - 20;
    }

    int wx = this->depRateWindowPos.x;
    int wy = this->depRateWindowPos.y;

    // Close and popout button rects (inside title bar, inset 1 px from border)
    RECT xRect                   = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    RECT popRect                 = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};
    this->winCloseLastHoverType  = -1;
    this->winPopoutLastHoverType = -1;
    POINT cursorDep              = {-9999, -9999};
    if (this->isPopoutRender_)
        cursorDep = this->popoutHoverPoint_;
    else
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursorDep);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursorDep);
    }
    bool xHovered   = PtInRect(&xRect, cursorDep) != 0;
    bool popHovered = PtInRect(&popRect, cursorDep) != 0;

    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT hdrRect   = {wx, wy + TITLE_H, wx + WIN_W, wy + TITLE_H + HDR_H};

    FillRectAlpha(hDC, winRect, RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect, RGB(40, 40, 40), op);
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }
    if (popHovered)
    {
        auto popBrush = CreateSolidBrush(RGB(40, 100, 160));
        FillRect(hDC, &popRect, popBrush);
        DeleteObject(popBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable — excludes close and popout button area)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    RECT dragRect = {wx, wy, popRect.left - 1, wy + TITLE_H};
    DrawTextA(hDC, "Departures", -1, &dragRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT popDrawRect = popRect;
    if (!this->isPopoutRender_)
        popDrawRect.top += 1;
    DrawTextA(hDC, this->isPopoutRender_ ? "v" : "^", -1, &popDrawRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    this->AddScreenObjectAuto(SCREEN_OBJECT_DEPRATE_WIN, "DEPRATE", dragRect, true, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_CLOSE, "depRate", xRect, false, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_POPOUT, "depRate", popRect, false, "");

    HFONT hdrFontDep  = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dataFontDep = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFontDep = (HFONT)SelectObject(hDC, hdrFontDep);

    // Column headers
    SetTextColor(hDC, TAG_COLOR_WHITE);
    {
        int  colY0  = wy + TITLE_H;
        int  splitX = wx + WIN_PAD + COL_RWY + COL_CNT + COL_GAP;
        RECT rwyHdr = {wx + WIN_PAD, colY0, wx + WIN_PAD + COL_RWY, colY0 + HDR_H};
        RECT cntHdr = {wx + WIN_PAD + COL_RWY, colY0, splitX, colY0 + HDR_H};
        RECT spcHdr = {splitX, colY0, wx + WIN_W - WIN_PAD, colY0 + HDR_H};
        DrawTextA(hDC, "RWY", -1, &rwyHdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextA(hDC, "#", -1, &cntHdr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DrawTextA(hDC, "AVG", -1, &spcHdr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // Data rows
    SelectObject(hDC, dataFontDep);
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r    = this->depRateRowsCache[row];
        int         rowY = wy + TITLE_H + HDR_H + row * ROW_H;
        if (row % 2 == 1)
        {
            RECT alt = {wx + 1, rowY, wx + WIN_W - 1, rowY + ROW_H};
            FillRectAlpha(hDC, alt, RGB(32, 32, 32), op);
        }

        RECT rwyRect = {wx + WIN_PAD, rowY, wx + WIN_PAD + COL_RWY, rowY + ROW_H};
        RECT cntRect = {wx + WIN_PAD + COL_RWY, rowY, wx + WIN_PAD + COL_RWY + COL_CNT, rowY + ROW_H};
        RECT spcRect = {wx + WIN_PAD + COL_RWY + COL_CNT + COL_GAP, rowY, wx + WIN_W - WIN_PAD, rowY + ROW_H};

        SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
        DrawTextA(hDC, r.runway.c_str(), -1, &rwyRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hDC, r.countColor);
        DrawTextA(hDC, r.countStr.c_str(), -1, &cntRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hDC, r.spacingColor);
        DrawTextA(hDC, r.spacingStr.c_str(), -1, &spcRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevFontDep);
    DeleteObject(hdrFontDep);
    DeleteObject(dataFontDep);
}

/// @brief Draws the TWR Outbound custom window from the pre-sorted twrOutboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrOutbound(HDC hDC)
{
    auto* settingsOut = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsOut->GetTwrOutboundVisible())
    {
        return;
    }
    int fo = settingsOut->GetFontOffset();
    int op = settingsOut->GetBgOpacity();

    const int TITLE_H = 13;
    const int HDR_H   = 17 + fo;
    const int ROW_H   = 19 + fo;
    const int PAD     = 6;
    // Original list column order and widths in chars (base widths at font size 17; scaled by (17+fo)/17):
    // C/S=12, STS=12, DEP?=10, RWY=4, SID=11, WTC=4, ATYP=8, Freq=15, HP=4, Spacing=17, dNM=8
    auto cw = [&](int base) -> int
    { return base * (17 + fo) / 17; };
    const int CS           = cw(84);    // 12 chars
    const int STS          = cw(84);    // 12 chars
    const int DEP          = cw(70);    // 10 chars
    const int RWY          = cw(35);    //  4 chars (widened: "RWY" label needs more than 4×7px)
    const int SID          = cw(77);    // 11 chars
    const int WTC          = cw(28);    //  4 chars
    const int ATYP         = cw(56);    //  8 chars
    const int FREQ         = cw(105);   // 15 chars
    const int HP           = cw(28);    //  4 chars
    const int QPOS         = cw(21);    //  3 chars  (queue position: "1"–"9", or "10")
    const int SPC          = cw(119);   // 17 chars
    const int TMR          = cw(56);    //  8 chars ("9:59"; extra left margin separates from Spacing)
    const int LDST         = cw(77);    // 11 chars (live distance to previous departure; extra left margin separates from T+)
    const int DIMMED_ROW_H = ROW_H - 3; ///< Reduced row height for dimmed rows (matches font size reduction 17→14)
    const int SEP_H        = 12;        ///< Height of blank separator row between sort groups
    const int WIN_W        = PAD + CS + STS + DEP + RWY + SID + WTC + ATYP + FREQ + HP + QPOS + SPC + TMR + LDST + PAD;
    int       numRows      = (int)this->twrOutboundRowsCache.size();

    int numSeps   = 0;
    int totalRowH = 0;
    for (const auto& r : this->twrOutboundRowsCache)
    {
        if (r.groupSeparatorAbove)
        {
            ++numSeps;
        }
        totalRowH += r.dimmed ? DIMMED_ROW_H : ROW_H;
    }

    const int WIN_H = TITLE_H + HDR_H + totalRowH + numSeps * SEP_H + PAD / 2;

    this->twrOutboundWindowW = WIN_W;
    this->twrOutboundWindowH = WIN_H;

    if (this->twrOutboundWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->twrOutboundWindowPos.x = 20;
        this->twrOutboundWindowPos.y = clip.bottom - WIN_H - 20;
    }

    int wx = this->twrOutboundWindowPos.x;
    int wy = this->twrOutboundWindowPos.y;

    const int X_BTN             = 11;
    RECT      xRect             = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    this->winCloseLastHoverType = -1;
    POINT cursorOut             = {-9999, -9999};
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursorOut);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursorOut);
    }
    bool xHovered = PtInRect(&xRect, cursorOut) != 0;

    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT hdrRect   = {wx, wy + TITLE_H, wx + WIN_W, wy + TITLE_H + HDR_H};

    FillRectAlpha(hDC, winRect, RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect, RGB(40, 40, 40), op);
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }
    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable — excludes close button area)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Outbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    RECT dragRectOut = {wx, wy, xRect.left - 1, wy + TITLE_H};
    AddScreenObject(SCREEN_OBJECT_TWR_OUT_WIN, "TWROUT", dragRectOut, true, "");
    AddScreenObject(SCREEN_OBJECT_WIN_CLOSE, "twrOut", xRect, false, "");

    // Column headers — smaller font
    HFONT hdrFont      = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevDataFont = (HFONT)SelectObject(hDC, hdrFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    {
        int  x      = wx + PAD;
        int  colY0  = wy + TITLE_H;
        auto colHdr = [&](int width, const char* label, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT r = {x, colY0, x + width, colY0 + HDR_H};
            DrawTextA(hDC, label, -1, &r, flags);
            x += width;
        };
        colHdr(CS, "C/S");
        colHdr(STS, "STS");
        colHdr(DEP, "DEP?");
        colHdr(RWY, "RWY", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(SID, "SID");
        colHdr(WTC, "W", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(ATYP, "ATYP");
        colHdr(FREQ, "Freq");
        colHdr(HP, "HP", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(QPOS, "#", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(SPC, "Spacing", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(TMR, "T+", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(LDST, "dNM", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevDataFont);
    DeleteObject(hdrFont);

    // Data rows — normal font for active aircraft, dimmed for departed+untracked
    HFONT dataFont = CreateFontA(-17 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dimFont  = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevDataFont   = (HFONT)SelectObject(hDC, dataFont);
    auto wtcColor  = [](char wtc) -> COLORREF
    {
        switch (wtc)
        {
        case 'L':
            return TAG_COLOR_TURQ;
        case 'H':
            return TAG_COLOR_ORANGE;
        case 'J':
            return TAG_COLOR_RED;
        default:
            return TAG_COLOR_LIST_GRAY;
        }
    };

    int rowTop = wy + TITLE_H + HDR_H;
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r    = this->twrOutboundRowsCache[row];
        int         rowH = r.dimmed ? DIMMED_ROW_H : ROW_H;

        if (r.groupSeparatorAbove)
        {
            RECT sepRect = {wx + 1, rowTop, wx + WIN_W - 1, rowTop + SEP_H};
            FillRect(hDC, &sepRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            rowTop += SEP_H;
        }

        int rowY = rowTop;
        if (row % 2 == 1)
        {
            RECT alt = {wx + 1, rowY, wx + WIN_W - 1, rowY + rowH};
            FillRectAlpha(hDC, alt, RGB(32, 32, 32), op);
        }
        int cx = wx + PAD;

        SelectObject(hDC, r.dimmed ? dimFont : dataFont);

        // Plain clickable cell for non-tagInfo data (callsign, wtc, aircraft type).
        auto cellClickable = [&](int width, const std::string& text, COLORREF color,
                                 const std::string& objId, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = {cx, rowY, cx + width, rowY + rowH};
            SetTextColor(hDC, color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            AddScreenObject(SCREEN_OBJECT_TWR_OUT_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        // tagInfo-aware variants: honour bgColor, bold, and fontDelta in addition to text and colour.
        // When bgColor is set the fill is measured to the actual text extent and drawn as an overlay
        // on top of the normal row background, then text is redrawn on top of the fill.
        auto cellTag = [&](int width, const tagInfo& t, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = {cx, rowY, cx + width, rowY + rowH};

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont     = CreateFontA(baseSize - delta, 0, 0, 0,
                                           t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int    textLeft = (flags & DT_RIGHT)    ? rect.right - textSize.cx
                                  : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                                                        : rect.left;
                int    textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT   fillRect = {(LONG)(textLeft - 3), std::max(rect.top, (LONG)(textTop - 1)),
                                   (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1))};
                HBRUSH brush    = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled)
            {
                SelectObject(hDC, r.dimmed ? dimFont : dataFont);
                DeleteObject(cellFont);
            }
            cx += width;
        };

        auto cellTagClickable = [&](int width, const tagInfo& t, const std::string& objId,
                                    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = {cx, rowY, cx + width, rowY + rowH};

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont     = CreateFontA(baseSize - delta, 0, 0, 0,
                                           t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int    textLeft = (flags & DT_RIGHT)    ? rect.right - textSize.cx
                                  : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                                                        : rect.left;
                int    textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT   fillRect = {(LONG)(textLeft - 3), std::max(rect.top, (LONG)(textTop - 1)),
                                   (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1))};
                HBRUSH brush    = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled)
            {
                SelectObject(hDC, r.dimmed ? dimFont : dataFont);
                DeleteObject(cellFont);
            }
            AddScreenObject(SCREEN_OBJECT_TWR_OUT_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        cellClickable(CS, r.callsign, r.callsignColor, r.callsign + "|CS");
        cellTagClickable(STS, r.status, r.callsign + "|STS");
        cellTagClickable(DEP, r.depInfo, r.callsign + "|DEP");
        cellTagClickable(RWY, r.rwy, r.callsign + "|RWY", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(SID, r.sameSid, r.callsign + "|SID");
        cellTagClickable(WTC, {.tag = std::string(1, r.wtc), .color = wtcColor(r.wtc), .bold = (r.wtc != 'M' && r.wtc != ' '), .fontDelta = (r.wtc != 'M' && r.wtc != ' ') ? 1 : 0}, r.callsign + "|WTC", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(ATYP, r.aircraftType, r.callsignColor, r.callsign + "|ATYP");
        cellTagClickable(FREQ, r.nextFreq, r.callsign + "|FREQ");
        cellTagClickable(HP, r.hp, r.callsign + "|HP", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(QPOS, r.queuePos, r.callsign + "|QPOS", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(SPC, r.spacing, r.callsign + "|SPC", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        cellTag(TMR, r.timeSinceTakeoff, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        cellTag(LDST, r.liveSpacing, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        rowTop += rowH;
    }

    SelectObject(hDC, prevDataFont);
    DeleteObject(dataFont);
    DeleteObject(dimFont);
}

/// @brief Draws the TWR Inbound custom window from the pre-sorted twrInboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrInbound(HDC hDC)
{
    auto* settingsIn = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsIn->GetTwrInboundVisible())
    {
        return;
    }
    int fo = settingsIn->GetFontOffset();
    int op = settingsIn->GetBgOpacity();

    const int TITLE_H      = 13;
    const int HDR_H        = 17 + fo;
    const int ROW_H        = 19 + fo;
    const int DIMMED_ROW_H = ROW_H - 3; ///< Reduced row height for dimmed rows (matches font size reduction 17→14)
    const int SEP_H        = 12;        ///< Height of blank separator row between runway groups
    const int PAD          = 6;
    // Column order: RWY, TTT, C/S, NM, SPD, WTC, ATYP, Gate, Vacate, ARW
    // Base widths at font size 17; scaled by (17+fo)/17:
    auto cw = [&](int base) -> int
    { return base * (17 + fo) / 17; };
    const int RWY     = cw(35); //  front column — runway group label
    const int TTT     = cw(90); //  "mm:ss" normally; "->nnn.nnn" (9 chars) on go-around
    const int CS      = cw(84); // 12 chars
    const int NM      = cw(56); //  8 chars
    const int GS      = cw(35); //  5 chars
    const int WTC     = cw(28); //  4 chars
    const int ATYP    = cw(56); //  8 chars
    const int GATE    = cw(35); //  5 chars
    const int VACATE  = cw(49); //  7 chars
    const int ARW     = cw(49); //  7 chars
    const int WIN_W   = PAD + RWY + TTT + CS + NM + GS + WTC + ATYP + GATE + VACATE + ARW + PAD;
    int       numRows = (int)this->twrInboundRowsCache.size();

    // Count separators and sum actual row heights
    int numSeps   = 0;
    int totalRowH = 0;
    {
        std::string lastGrp;
        for (const auto& r : this->twrInboundRowsCache)
        {
            if (!lastGrp.empty() && r.rwy != lastGrp)
            {
                ++numSeps;
            }
            lastGrp = r.rwy;
            totalRowH += r.dimmed ? DIMMED_ROW_H : ROW_H;
        }
    }

    const int WIN_H = TITLE_H + HDR_H + totalRowH + numSeps * SEP_H + PAD / 2;

    this->twrInboundWindowW = WIN_W;
    this->twrInboundWindowH = WIN_H;

    if (this->twrInboundWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->twrInboundWindowPos.x = 20;
        this->twrInboundWindowPos.y = clip.bottom - WIN_H - 20;
    }

    int wx = this->twrInboundWindowPos.x;
    int wy = this->twrInboundWindowPos.y;

    const int X_BTN             = 11;
    RECT      xRect             = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    this->winCloseLastHoverType = -1;
    POINT cursorIn              = {-9999, -9999};
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursorIn);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursorIn);
    }
    bool xHovered = PtInRect(&xRect, cursorIn) != 0;

    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT hdrRect   = {wx, wy + TITLE_H, wx + WIN_W, wy + TITLE_H + HDR_H};

    FillRectAlpha(hDC, winRect, RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect, RGB(40, 40, 40), op);
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }
    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable — excludes close button area)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Inbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    RECT dragRectIn = {wx, wy, xRect.left - 1, wy + TITLE_H};
    AddScreenObject(SCREEN_OBJECT_TWR_IN_WIN, "TWRIN", dragRectIn, true, "");
    AddScreenObject(SCREEN_OBJECT_WIN_CLOSE, "twrIn", xRect, false, "");

    // Column headers — smaller font
    HFONT hdrFont      = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevDataFont = (HFONT)SelectObject(hDC, hdrFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    {
        int  x      = wx + PAD;
        int  colY0  = wy + TITLE_H;
        auto colHdr = [&](int width, const char* label, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT r = {x, colY0, x + width, colY0 + HDR_H};
            DrawTextA(hDC, label, -1, &r, flags);
            x += width;
        };
        colHdr(RWY, "RWY");
        colHdr(TTT, "TTT");
        colHdr(CS, "C/S");
        colHdr(NM, "NM", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(GS, "GS", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(WTC, "W", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(ATYP, "ATYP");
        colHdr(GATE, "Gate");
        colHdr(VACATE, "Vacate");
        colHdr(ARW, "ARW", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevDataFont);
    DeleteObject(hdrFont);

    // Data rows — normal font for the closest aircraft per runway, dimmed for the rest
    HFONT dataFont = CreateFontA(-17 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dimFont  = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevDataFont   = (HFONT)SelectObject(hDC, dataFont);
    auto wtcColor  = [](char wtc) -> COLORREF
    {
        switch (wtc)
        {
        case 'L':
            return TAG_COLOR_TURQ;
        case 'H':
            return TAG_COLOR_ORANGE;
        case 'J':
            return TAG_COLOR_RED;
        default:
            return TAG_COLOR_LIST_GRAY;
        }
    };

    std::string lastRwy;
    int         rowTop = wy + TITLE_H + HDR_H;
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r    = this->twrInboundRowsCache[row];
        int         rowH = r.dimmed ? DIMMED_ROW_H : ROW_H;

        // Insert a blank separator row between runway groups
        if (!lastRwy.empty() && r.rwy != lastRwy)
        {
            RECT sepRect = {wx + 1, rowTop, wx + WIN_W - 1, rowTop + SEP_H};
            FillRect(hDC, &sepRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            rowTop += SEP_H;
        }
        lastRwy = r.rwy;

        SelectObject(hDC, r.dimmed ? dimFont : dataFont);

        int rowY = rowTop;
        if (row % 2 == 1)
        {
            RECT alt = {wx + 1, rowY, wx + WIN_W - 1, rowY + rowH};
            FillRectAlpha(hDC, alt, RGB(32, 32, 32), op);
        }
        int cx = wx + PAD;

        // Plain clickable cell for non-tagInfo data (callsign, rwy group, wtc, groundspeed, gate).
        auto cellClickable = [&](int width, const std::string& text, COLORREF color,
                                 const std::string& objId, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = {cx, rowY, cx + width, rowY + rowH};
            SetTextColor(hDC, color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            AddScreenObject(SCREEN_OBJECT_TWR_IN_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        // tagInfo-aware variants: honour bgColor, bold, and fontDelta in addition to text and colour.
        // When bgColor is set the fill is measured to the actual text extent and drawn as an overlay
        // on top of the normal row background, then text is redrawn on top of the fill.
        auto cellTag = [&](int width, const tagInfo& t, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = {cx, rowY, cx + width, rowY + rowH};

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont     = CreateFontA(baseSize - delta, 0, 0, 0,
                                           t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int    textLeft = (flags & DT_RIGHT)    ? rect.right - textSize.cx
                                  : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                                                        : rect.left;
                int    textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT   fillRect = {(LONG)(textLeft - 3), std::max(rect.top, (LONG)(textTop - 1)),
                                   (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1))};
                HBRUSH brush    = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled)
            {
                SelectObject(hDC, r.dimmed ? dimFont : dataFont);
                DeleteObject(cellFont);
            }
            cx += width;
        };

        auto cellTagClickable = [&](int width, const tagInfo& t, const std::string& objId,
                                    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = {cx, rowY, cx + width, rowY + rowH};

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont     = CreateFontA(baseSize - delta, 0, 0, 0,
                                           t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int    textLeft = (flags & DT_RIGHT)    ? rect.right - textSize.cx
                                  : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                                                        : rect.left;
                int    textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT   fillRect = {(LONG)(textLeft - 3), std::max(rect.top, (LONG)(textTop - 1)),
                                   (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1))};
                HBRUSH brush    = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled)
            {
                SelectObject(hDC, r.dimmed ? dimFont : dataFont);
                DeleteObject(cellFont);
            }
            AddScreenObject(SCREEN_OBJECT_TWR_IN_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        cellClickable(RWY, r.rwy, r.callsignColor, r.callsign + "|RWY");
        cellTagClickable(TTT, r.ttt, r.callsign + "|TTT");
        cellTagClickable(CS, {.tag = r.callsign, .color = (r.isEmergency || r.isGoAround) ? TAG_COLOR_WHITE : r.isFrozen ? TAG_COLOR_BLACK
                                                                                                                         : r.callsignColor,
                              .bgColor = (r.isEmergency || r.isGoAround) ? TAG_BG_COLOR_RED : r.isFrozen ? TAG_BG_COLOR_YELLOW
                                                                                                         : TAG_COLOR_DEFAULT_NONE},
                         r.callsign + "|CS");
        cellTagClickable(NM, r.nm, r.callsign + "|NM", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        cellClickable(GS, std::format("{}", r.groundSpeed), TAG_COLOR_WHITE, r.callsign + "|GS", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(WTC, {.tag = std::string(1, r.wtc), .color = wtcColor(r.wtc), .bold = (r.wtc != 'M' && r.wtc != ' '), .fontDelta = (r.wtc != 'M' && r.wtc != ' ') ? 1 : 0}, r.callsign + "|WTC", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(ATYP, r.aircraftType, r.callsignColor, r.callsign + "|ATYP");
        cellTagClickable(GATE, r.gate, r.callsign + "|GATE");
        cellTagClickable(VACATE, r.vacate, r.callsign + "|VACATE");
        cellTagClickable(ARW, r.arrRwy, r.callsign + "|ARW", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        rowTop += rowH;
    }

    SelectObject(hDC, prevDataFont);
    DeleteObject(dataFont);
    DeleteObject(dimFont);
}

/// @brief Draws the NAP reminder as a custom floating window with a draggable body and an ACK button.
/// OnOverScreenObject calls RequestRefresh() on every mouse-movement event so this function runs
/// frequently. GetCursorPos() then reflects the live cursor position accurately at each draw.
void RadarScreen::DrawNapReminder(HDC hDC)
{
    if (!this->napReminderActive)
    {
        return;
    }

    const int TITLE_H = 20;
    const int MSG_H   = 40;
    const int BTN_H   = 28;
    const int PAD     = 10;
    const int WIN_W   = 360;
    const int WIN_H   = TITLE_H + MSG_H + BTN_H + PAD;

    if (this->napWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->napWindowPos.x = clip.right / 2 - WIN_W / 2;
        this->napWindowPos.y = clip.bottom / 2 - WIN_H / 2;
    }

    int wx = this->napWindowPos.x;
    int wy = this->napWindowPos.y;

    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT bodyRect  = {wx + PAD, wy + TITLE_H, wx + WIN_W - PAD, wy + TITLE_H + MSG_H};
    RECT btnRect   = {wx + PAD, wy + TITLE_H + MSG_H, wx + WIN_W - PAD, wy + TITLE_H + MSG_H + BTN_H - 4};
    // Drag zone covers title+body (does NOT overlap btnRect so ES fires separate OnOverScreenObject per area)
    RECT dragRect = {wx, wy, wx + WIN_W, wy + TITLE_H + MSG_H};

    // Reset hover tracking each frame so the next window entry always counts as a transition.
    this->napLastHoverType = -1;

    // Blink animation: 3 full cycles (6 phases × 80 ms) after click before the window closes.
    // Even phases show the pressed colour; odd phases show the hover colour.
    if (this->napAckClickTick != 0)
    {
        int phase = (int)((GetTickCount64() - this->napAckClickTick) / 80);
        if (phase >= 6)
        {
            // Animation finished — close window, stop sound, and persist dismissal
            this->napAckClickTick   = 0;
            this->napAckPressed     = false;
            this->napReminderActive = false;
            PlaySoundA(nullptr, nullptr, 0);
            static_cast<CFlowX_Timers*>(this->GetPlugIn())->AckNapReminder();
            return;
        }
        // Set pressed state for this phase and request the next frame immediately
        this->napAckPressed = (phase % 2 == 0);
        this->RequestRefresh();
    }

    // Hover: read live cursor position. RequestRefresh() from OnOverScreenObject (or the blink loop)
    // ensures this runs at event rate, so the cursor position is always current.
    HWND  hwnd = WindowFromDC(hDC);
    POINT cursorPt;
    GetCursorPos(&cursorPt);
    if (hwnd)
    {
        ScreenToClient(hwnd, &cursorPt);
    }
    // During blink napAckPressed drives the colour; GetCursorPos hover is irrelevant then
    bool ackHovered = (this->napAckClickTick == 0) && PtInRect(&btnRect, cursorPt) != 0;

    // Background
    auto bgBrush    = CreateSolidBrush(RGB(15, 15, 15));
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &winRect, bgBrush);
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(bgBrush);
    DeleteObject(titleBrush);

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title bar
    HFONT titleFont = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prev      = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "Night SID Reminder", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(titleFont);
    AddScreenObject(SCREEN_OBJECT_NAP_WIN, "NAPWIN", dragRect, true, "");

    // Message body
    HFONT msgFont = CreateFontA(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prev          = (HFONT)SelectObject(hDC, msgFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    std::string msg = "NAP procedure for " + this->napReminderAirport + "?";
    DrawTextA(hDC, msg.c_str(), -1, &bodyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(msgFont);

    // ACK button — three visual states: normal / hover / pressed
    COLORREF btnBg     = this->napAckPressed ? RGB(90, 175, 90)
                         : ackHovered        ? RGB(60, 130, 60)
                                             : RGB(35, 90, 35);
    COLORREF btnBorder = this->napAckPressed ? RGB(200, 255, 200)
                         : ackHovered        ? RGB(120, 200, 120)
                                             : RGB(80, 150, 80);
    auto     btnBrush  = CreateSolidBrush(btnBg);
    FillRect(hDC, &btnRect, btnBrush);
    DeleteObject(btnBrush);

    auto btnBorderBrush = CreateSolidBrush(btnBorder);
    FrameRect(hDC, &btnRect, btnBorderBrush);
    DeleteObject(btnBorderBrush);

    HFONT btnFont = CreateFontA(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prev          = (HFONT)SelectObject(hDC, btnFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "ACK", -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(btnFont);
    AddScreenObject(SCREEN_OBJECT_NAP_ACK, "NAPACK", btnRect, false, "Acknowledge NAP reminder");
}

/// @brief Draws the Start button pinned to the lower-right corner of the radar screen.
/// GetChatArea() is queried every frame: when the chat / flight-strip bay is open the button
/// floats above it; otherwise it sits above the raw clip-box bottom.
void RadarScreen::DrawStartButton(HDC hDC)
{
    const int BTN_W = 56;
    const int BTN_H = 20;

    // Re-read boundaries every frame so the button floats above whichever overlay is currently visible.
    // When chat is open, chat.top is the exact pixel where the panel begins — use it directly.
    // When chat is closed, clip.bottom (the GDI drawable boundary) is the true visual edge.
    RECT clip;
    GetClipBox(hDC, &clip);
    RECT chat     = GetChatArea();
    bool chatOpen = (chat.bottom > chat.top);
    int  bottom   = chatOpen ? chat.top : clip.bottom + 5; // +5: EuroScope clip-box excludes the 5 px status-bar gap when chat is closed
    int  bx       = clip.right - BTN_W;
    int  by       = bottom - BTN_H;

    RECT btnRect = {bx, by, bx + BTN_W, by + BTN_H};

    // Reset hover tracking each frame so every new mouse-enter counts as a fresh transition.
    this->startBtnLastHoverType = -1;

    // Read live cursor position for hover detection; RequestRefresh() from OnOverScreenObject keeps this current.
    HWND  hwnd = WindowFromDC(hDC);
    POINT cursor;
    GetCursorPos(&cursor);
    if (hwnd)
    {
        ScreenToClient(hwnd, &cursor);
    }
    bool hovered = PtInRect(&btnRect, cursor) != 0;

    // Background — three visual states: normal / hover / pressed
    COLORREF bgColor     = this->startBtnPressed ? RGB(70, 110, 170)
                           : hovered             ? RGB(50, 85, 135)
                                                 : RGB(30, 55, 95);
    COLORREF borderColor = this->startBtnPressed ? RGB(180, 210, 255)
                           : hovered             ? RGB(120, 160, 220)
                                                 : RGB(80, 120, 180);

    auto bgBrush = CreateSolidBrush(bgColor);
    FillRect(hDC, &btnRect, bgBrush);
    DeleteObject(bgBrush);

    auto borderBrush = CreateSolidBrush(borderColor);
    FrameRect(hDC, &btnRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    HFONT font = CreateFontA(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prev = (HFONT)SelectObject(hDC, font);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "FlowX", -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(font);

    AddScreenObject(SCREEN_OBJECT_START_BTN, "STARTBTN", btnRect, false, "FlowX");
}

/// @brief Draws the popup menu above the Start button when startMenuOpen is true.
/// Uses the same clip-box and chat-area logic as DrawStartButton to stay flush above it.
/// Sections: Windows (window show/hide toggles), Commands (actions), Options (plugin toggles).
void RadarScreen::DrawStartMenu(HDC hDC)
{
    if (!this->startMenuOpen)
    {
        return;
    }

    const int BTN_H     = 20; ///< Must match DrawStartButton::BTN_H (fixed)
    auto*     base      = static_cast<CFlowX_Base*>(this->GetPlugIn());
    auto*     settings  = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    const int fo        = settings->GetFontOffset();
    const int HEADER_H  = 16 + fo;              ///< Height of a section header row
    const int ITEM_H    = 20 + fo;              ///< Height of a clickable item row
    const int GAP_H     = 10;                   ///< Spacer height inserted above each section header after the first
    const int OUTER_PAD = 6;                    ///< Margin from the outer border to all content on every side
    const int CBX_S     = 9 + fo;               ///< Checkbox square size in pixels
    const int CBX_GAP   = 4;                    ///< Gap between checkbox right edge and item label
    const int MENU_W    = 180 * (14 + fo) / 14; ///< Menu width scaled with item font; right-aligned with the button

    struct MenuRow
    {
        bool        isHeader;
        const char* label;
        bool        hasCheckbox;
        bool        checked;
        int         itemIdx;
        bool        hasFontButtons = false;
        bool        hasBgButtons   = false;
        bool        disabled       = false;
    };

    const bool osmBusy = settings->IsOsmBusy();

    MenuRow rows[] = {
        {true, "Windows", false, false, -1},
        {false, "Approach Estimate", true, settings->GetApproachEstVisible(), 16},
        {false, "DEP/H", true, settings->GetDepRateVisible(), 4},
        {false, "TWR Outbound", true, settings->GetTwrOutboundVisible(), 5},
        {false, "TWR Inbound", true, settings->GetTwrInboundVisible(), 6},
        {false, "WX/ATIS", true, settings->GetWeatherVisible(), 7},
        {true, "Commands", false, false, -1},
        {false, "Redo CLR flags", false, false, 0},
        {false, "Dismiss QNH", false, false, 12},
        {false, "Save positions", false, false, 1},
        {true, "Assists", false, false, -1},
        {false, "Auto-Restore FPLN", true, settings->GetAutoRestore(), 3},
        {false, "Auto PARK", true, settings->GetAutoParked(), 15},
        {false, "Auto-Clear Scratch", true, settings->GetAutoScratchpadClear(), 18},
        {false, "HP auto-scratch", true, settings->GetHpAutoScratch(), 28},
        {true, "Notifications", false, false, -1},
        {false, "Airborne", true, settings->GetSoundAirborne(), 19},
        {false, "GND Transfer", true, settings->GetSoundGndTransfer(), 20},
        {false, "Ready T/O", true, settings->GetSoundReadyTakeoff(), 21},
        {false, "No Route", true, settings->GetSoundNoRoute(), 30},
        {false, "Taxi Conflict", true, settings->GetSoundTaxiConflict(), 27},
        {true, "Options", false, false, -1},
        {false, "Debug mode", true, base->GetDebug(), 2},
        {false, "Update check", true, settings->GetUpdateCheck(), 13},
        {false, "Flash messages", true, settings->GetFlashOnMessage(), 14},
        {false, "Appr Est Colors", true, settings->GetApprEstColors(), 17},
        {false, "Fonts", false, false, -1, true},
        {false, "BG opacity", false, false, -1, false, true},
        {true, "TAXI", false, false, -1},
        {false, "Update TAXI info", false, false, 22, false, false, osmBusy},
        {false, "Clear TAXI routes", false, false, 26},
        {false, "Show TAXI network", true, this->showTaxiOverlay, 23},
        {false, "Show TAXI labels", true, this->showTaxiLabels, 24},
        {false, "Show TAXI routes", true, this->showTaxiRoutes, 25},
        {false, "Show TAXI graph", true, this->showTaxiGraph, 29},
        {false, "Log TAXI tests", true, this->logTaxiTests, 31},
    };
    const int NUM_ROWS = (int)(sizeof(rows) / sizeof(rows[0]));

    // Compute total menu height: outer padding at bottom only (top header sits flush with the border), gaps before non-first headers, row heights.
    int MENU_H = OUTER_PAD;
    for (auto [i, row] : std::views::enumerate(rows))
    {
        MENU_H += (row.isHeader && i > 0) ? GAP_H + HEADER_H : row.isHeader ? HEADER_H
                                                                            : ITEM_H;
    }

    // Mirror DrawStartButton's anchor logic exactly.
    RECT clip;
    GetClipBox(hDC, &clip);
    RECT chat     = GetChatArea();
    bool chatOpen = (chat.bottom > chat.top);
    int  bottom   = chatOpen ? chat.top : clip.bottom + 5; // +5: matches DrawStartButton's status-bar gap offset
    int  mx       = clip.right - MENU_W;
    int  my       = bottom - BTN_H - MENU_H;

    // Hover detection — reset each frame so every enter counts as a fresh transition.
    this->startMenuLastHoverType = -1;
    HWND  hwnd                   = WindowFromDC(hDC);
    POINT cursor;
    GetCursorPos(&cursor);
    if (hwnd)
    {
        ScreenToClient(hwnd, &cursor);
    }

    // Auto-close if the cursor moves more than 100 px away from the menu in any direction.
    const int CLOSE_DIST = 200;
    if (cursor.x < mx - CLOSE_DIST || cursor.x > mx + MENU_W + CLOSE_DIST ||
        cursor.y < my - CLOSE_DIST || cursor.y > my + MENU_H + CLOSE_DIST)
    {
        this->startMenuOpen = false;
        this->RequestRefresh();
        return;
    }

    // Overall background and border.
    RECT menuRect = {mx, my, mx + MENU_W, my + MENU_H};
    auto bgBrush  = CreateSolidBrush(RGB(15, 15, 15));
    FillRect(hDC, &menuRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(hDC, TRANSPARENT);

    HFONT headerFont = CreateFontA(-10 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT itemFont   = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT cbxFont    = CreateFontA(-11 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prev       = (HFONT)SelectObject(hDC, itemFont);

    int iy = my;
    for (auto [i, row] : std::views::enumerate(rows))
    {
        int  rh      = row.isHeader ? HEADER_H : ITEM_H;
        RECT rowRect = {mx, iy, mx + MENU_W, iy + rh}; // rowRect for headers is updated after gap advance below

        if (row.isHeader)
        {
            // Gap spacer above every section header except the first.
            if (i > 0)
            {
                iy += GAP_H;
            }

            // Section header — blue title-bar background, inset by 1 so it doesn't paint over the border.
            rowRect       = {mx + 1, iy, mx + MENU_W - 1, iy + HEADER_H};
            auto hdrBrush = CreateSolidBrush(RGB(30, 55, 95));
            FillRect(hDC, &rowRect, hdrBrush);
            DeleteObject(hdrBrush);

            SelectObject(hDC, headerFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            RECT textRect = {mx + OUTER_PAD, iy, mx + MENU_W - OUTER_PAD, iy + HEADER_H};
            DrawTextA(hDC, row.label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (row.hasFontButtons)
        {
            // Fonts row: label + current offset value on the left, [−] [+] buttons on the right.
            const int   BTN_W  = 14 + fo;
            int         fo     = settings->GetFontOffset();
            std::string valStr = (fo > 0) ? std::format("+{}", fo) : std::format("{}", fo);

            RECT minusRect = {mx + MENU_W - OUTER_PAD - BTN_W * 2 - 2, iy + (ITEM_H - BTN_W) / 2,
                              mx + MENU_W - OUTER_PAD - BTN_W - 2, iy + (ITEM_H + BTN_W) / 2};
            RECT plusRect  = {mx + MENU_W - OUTER_PAD - BTN_W, iy + (ITEM_H - BTN_W) / 2,
                              mx + MENU_W - OUTER_PAD, iy + (ITEM_H + BTN_W) / 2};
            bool minusHov  = PtInRect(&minusRect, cursor) != 0;
            bool plusHov   = PtInRect(&plusRect, cursor) != 0;

            for (auto [rect, hov] : {std::pair{minusRect, minusHov}, std::pair{plusRect, plusHov}})
            {
                auto btnBrush = CreateSolidBrush(hov ? RGB(45, 70, 115) : RGB(35, 35, 35));
                FillRect(hDC, &rect, btnBrush);
                DeleteObject(btnBrush);
                auto btnBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                FrameRect(hDC, &rect, btnBorder);
                DeleteObject(btnBorder);
            }

            SelectObject(hDC, itemFont);
            std::string fullLabel = std::string("Fonts ") + valStr;
            RECT        labelRect = {mx + OUTER_PAD + CBX_S + CBX_GAP, iy, minusRect.left - 4, iy + ITEM_H};
            SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
            DrawTextA(hDC, fullLabel.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hDC, cbxFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            DrawTextA(hDC, "-", -1, &minusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(hDC, "+", -1, &plusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|8", minusRect, false, "");
            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|9", plusRect, false, "");
        }
        else if (row.hasBgButtons)
        {
            // BG opacity row: label + current value on the left, [−] [+] buttons on the right.
            const int   BTN_W  = 14 + fo;
            int         bgOp   = settings->GetBgOpacity();
            std::string valStr = std::format("{}%", bgOp);

            RECT minusRect = {mx + MENU_W - OUTER_PAD - BTN_W * 2 - 2, iy + (ITEM_H - BTN_W) / 2,
                              mx + MENU_W - OUTER_PAD - BTN_W - 2, iy + (ITEM_H + BTN_W) / 2};
            RECT plusRect  = {mx + MENU_W - OUTER_PAD - BTN_W, iy + (ITEM_H - BTN_W) / 2,
                              mx + MENU_W - OUTER_PAD, iy + (ITEM_H + BTN_W) / 2};
            bool minusHov  = PtInRect(&minusRect, cursor) != 0;
            bool plusHov   = PtInRect(&plusRect, cursor) != 0;

            for (auto [rect, hov] : {std::pair{minusRect, minusHov}, std::pair{plusRect, plusHov}})
            {
                auto btnBrush = CreateSolidBrush(hov ? RGB(45, 70, 115) : RGB(35, 35, 35));
                FillRect(hDC, &rect, btnBrush);
                DeleteObject(btnBrush);
                auto btnBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                FrameRect(hDC, &rect, btnBorder);
                DeleteObject(btnBorder);
            }

            SelectObject(hDC, itemFont);
            std::string fullLabel = std::string("BG opacity ") + valStr;
            RECT        labelRect = {mx + OUTER_PAD + CBX_S + CBX_GAP, iy, minusRect.left - 4, iy + ITEM_H};
            SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
            DrawTextA(hDC, fullLabel.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hDC, cbxFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            DrawTextA(hDC, "-", -1, &minusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(hDC, "+", -1, &plusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|10", minusRect, false, "");
            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|11", plusRect, false, "");
        }
        else
        {
            // Clickable item row — hover highlight and optional checkbox.
            bool rowHovered = !row.disabled && PtInRect(&rowRect, cursor) != 0;
            if (rowHovered)
            {
                auto hoverBrush = CreateSolidBrush(RGB(45, 70, 115));
                FillRect(hDC, &rowRect, hoverBrush);
                DeleteObject(hoverBrush);
            }

            if (row.hasCheckbox)
            {
                int  cy      = iy + (ITEM_H - CBX_S) / 2;
                int  cx      = mx + OUTER_PAD;
                RECT boxRect = {cx, cy, cx + CBX_S, cy + CBX_S};

                auto boxBrush = CreateSolidBrush(row.checked ? RGB(30, 55, 95) : RGB(25, 25, 25));
                FillRect(hDC, &boxRect, boxBrush);
                DeleteObject(boxBrush);

                auto boxBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                FrameRect(hDC, &boxRect, boxBorder);
                DeleteObject(boxBorder);

                if (row.checked)
                {
                    SelectObject(hDC, cbxFont);
                    SetTextColor(hDC, TAG_COLOR_WHITE);
                    DrawTextA(hDC, "x", -1, &boxRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }

            SelectObject(hDC, itemFont);
            RECT textRect = {mx + OUTER_PAD + CBX_S + CBX_GAP, iy, mx + MENU_W - OUTER_PAD, iy + ITEM_H};
            SetTextColor(hDC, row.disabled ? RGB(70, 70, 70)
                              : rowHovered ? TAG_COLOR_WHITE
                                           : TAG_COLOR_LIST_GRAY);
            DrawTextA(hDC, row.label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            if (!row.disabled)
            {
                std::string objId = std::format("MENU|{}", row.itemIdx);
                AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, objId.c_str(), rowRect, false, row.label);
            }
        }

        iy += rh;
    }

    // Draw border last so it renders on top of all header/item fills.
    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &menuRect, borderBrush);
    DeleteObject(borderBrush);

    SelectObject(hDC, prev);
    DeleteObject(headerFont);
    DeleteObject(itemFont);
    DeleteObject(cbxFont);
}

/// @brief Draws the WX/ATIS window: wind, QNH, and ATIS letter in a single horizontal row.
/// Values are highlighted yellow when changed; clicking the row acknowledges them.
void RadarScreen::DrawWeatherWindow(HDC hDC)
{
    auto* settingsWx = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsWx->GetWeatherVisible())
    {
        return;
    }
    int fo = settingsWx->GetFontOffset();
    int op = settingsWx->GetBgOpacity();

    const int TITLE_H = 13;
    const int DATA_H  = 36;
    const int RVR_H   = 26; ///< Tighter row height for the RVR line
    const int WIN_PAD = 8;
    const int X_BTN   = 11; ///< Width and height of the title-bar close button

    static const WeatherRowCache emptyRow{"", "-", TAG_COLOR_DEFAULT_GRAY, "-", TAG_COLOR_DEFAULT_GRAY, "?", TAG_COLOR_DEFAULT_GRAY};
    const auto&                  r = this->weatherRowsCache.empty() ? emptyRow : this->weatherRowsCache[0];

    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dataFont  = CreateFontA(-20 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");

    // Measure content widths with the data font before drawing any background
    HFONT prev = (HFONT)SelectObject(hDC, dataFont);
    SIZE  spaceSize, qnhSize, atisSize, rvrSize;
    GetTextExtentPoint32A(hDC, " ", 1, &spaceSize);
    GetTextExtentPoint32A(hDC, r.qnh.c_str(), (int)r.qnh.size(), &qnhSize);
    GetTextExtentPoint32A(hDC, r.atis.c_str(), (int)r.atis.size(), &atisSize);
    GetTextExtentPoint32A(hDC, r.rvr.c_str(), (int)r.rvr.size(), &rvrSize);

    // Wind is drawn in two parts (direction + speed) with a half-space gap between them
    const int   halfGap   = spaceSize.cx / 2;
    std::string windDir   = (r.wind.size() >= 3) ? r.wind.substr(0, 3) : r.wind;
    std::string windSpeed = (r.wind.size() > 3) ? r.wind.substr(3) : "";
    SIZE        windDirSize, windSpeedSize;
    GetTextExtentPoint32A(hDC, windDir.c_str(), (int)windDir.size(), &windDirSize);
    GetTextExtentPoint32A(hDC, windSpeed.c_str(), (int)windSpeed.size(), &windSpeedSize);
    int windTotalW = windDirSize.cx + halfGap + windSpeedSize.cx;

    bool hasRVR = !r.rvr.empty();
    int  line1W = windTotalW + spaceSize.cx + qnhSize.cx + spaceSize.cx + atisSize.cx;
    int  WIN_W  = WIN_PAD + (line1W > rvrSize.cx ? line1W : rvrSize.cx) + WIN_PAD;
    int  WIN_H  = TITLE_H + DATA_H + (hasRVR ? RVR_H : 0) + WIN_PAD / 2;

    this->weatherWindowW = WIN_W;
    this->weatherWindowH = WIN_H;

    if (this->weatherWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->weatherWindowPos.x = clip.right - WIN_W - 20;
        this->weatherWindowPos.y = clip.top + 20;
    }

    int wx = this->weatherWindowPos.x;
    int wy = this->weatherWindowPos.y;

    RECT xRect                   = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    this->winCloseLastHoverType  = -1;
    this->winPopoutLastHoverType = -1;
    POINT cursorWx               = {-9999, -9999};
    if (this->isPopoutRender_)
        cursorWx = this->popoutHoverPoint_;
    else
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursorWx);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursorWx);
    }
    bool xHovered   = PtInRect(&xRect, cursorWx) != 0;
    RECT popRectWx  = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};
    bool popHovered = PtInRect(&popRectWx, cursorWx) != 0;

    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};

    FillRectAlpha(hDC, winRect, RGB(15, 15, 15), op);
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }
    if (popHovered)
    {
        auto popBrush = CreateSolidBrush(RGB(40, 100, 160));
        FillRect(hDC, &popRectWx, popBrush);
        DeleteObject(popBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    RECT dragRectWx = {wx, wy, popRectWx.left - 1, wy + TITLE_H};

    SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "WX/ATIS", -1, &dragRectWx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // Close and popout button text
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT popDrawRectWx = popRectWx;
    if (!this->isPopoutRender_)
        popDrawRectWx.top += 1;
    DrawTextA(hDC, this->isPopoutRender_ ? "v" : "^", -1, &popDrawRectWx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    this->AddScreenObjectAuto(SCREEN_OBJECT_WEATHER_WIN, "WXATIS", dragRectWx, true, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_CLOSE, "weather", xRect, false, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_POPOUT, "weather", popRectWx, false, "");

    SelectObject(hDC, dataFont);
    int textY = wy + TITLE_H + (DATA_H - spaceSize.cy) / 2;
    int x     = wx + WIN_PAD;

    auto drawField = [&](const std::string& text, COLORREF color, int w)
    {
        SetTextColor(hDC, color);
        TextOutA(hDC, x, textY, text.c_str(), (int)text.size());
        x += w + spaceSize.cx;
    };

    SetTextColor(hDC, r.windColor);
    TextOutA(hDC, x, textY, windDir.c_str(), (int)windDir.size());
    TextOutA(hDC, x + windDirSize.cx + halfGap, textY, windSpeed.c_str(), (int)windSpeed.size());
    x += windTotalW + spaceSize.cx;
    drawField(r.qnh, r.qnhColor, qnhSize.cx);
    drawField(r.atis, r.atisColor, atisSize.cx);

    if (hasRVR)
    {
        textY += RVR_H;
        x = wx + WIN_PAD;
        SetTextColor(hDC, r.rvrColor);
        TextOutA(hDC, x, textY, r.rvr.c_str(), (int)r.rvr.size());
    }

    RECT clickRect = {wx, wy + TITLE_H, wx + WIN_W, wy + WIN_H};
    this->AddScreenObjectAuto(SCREEN_OBJECT_WEATHER_ROW, r.icao.c_str(), clickRect, false, "");

    SelectObject(hDC, prev);
    DeleteObject(titleFont);
    DeleteObject(dataFont);
}

/// @brief Calls RequestRefresh() only on enter/leave transitions between NAP screen objects so that
/// DrawNapReminder() redraws exactly when the hover state changes, not on every mouse-move event.
void RadarScreen::OnOverScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area)
{
    try
    {
        if (ObjectType == SCREEN_OBJECT_NAP_WIN || ObjectType == SCREEN_OBJECT_NAP_ACK)
        {
            if (ObjectType != this->napLastHoverType)
            {
                this->napLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        if (ObjectType == SCREEN_OBJECT_START_BTN)
        {
            if (ObjectType != this->startBtnLastHoverType)
            {
                this->startBtnLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        if (ObjectType == SCREEN_OBJECT_START_MENU_ITEM)
        {
            if (ObjectType != this->startMenuLastHoverType)
            {
                this->startMenuLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        if (ObjectType == SCREEN_OBJECT_WIN_CLOSE)
        {
            if (ObjectType != this->winCloseLastHoverType)
            {
                this->winCloseLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        if (ObjectType == SCREEN_OBJECT_WIN_POPOUT)
        {
            if (ObjectType != this->winPopoutLastHoverType)
            {
                this->winPopoutLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        // Hover over a ground aircraft with a tracked route: show that route individually.
        if (ObjectType == SCREEN_OBJECT_TAXI_TARGET &&
            (this->taxiTracked.count(sObjectId) || this->pushTracked.count(sObjectId)))
        {
            this->hoveredTaxiTarget     = sObjectId;
            this->hoveredTaxiTargetTick = GetTickCount64();
            this->RequestRefresh();
        }

        // Taxi planning: update cursor snap and recompute preview on every mouse move.
        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING && !this->taxiPlanActive.empty())
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
            if (settings->osmGraph.IsBuilt())
            {
                EuroScopePlugIn::CPosition rawPos = this->ConvertCoordFromPixelToPosition(Pt);
                GeoPoint                   raw{rawPos.m_Latitude, rawPos.m_Longitude};

                const TaxiRoute& sug = this->taxiSuggested.count(this->taxiPlanActive)
                                           ? this->taxiSuggested.at(this->taxiPlanActive)
                                           : TaxiRoute{};
                this->taxiCursorSnap = settings->osmGraph.SnapForPlanning(raw, sug);

                // Collect raw cursor positions and nearby graph nodes while a middle-drag
                // draw gesture is in progress.  The node set is passed as preferredNodes to
                // FindWaypointRoute after release to bias A* toward the drawn path.
                constexpr double DRAW_SAMPLE_M      = 5.0;
                constexpr double DRAW_SNAP_RADIUS_M = 20.0;
                if (this->taxiMidDrawing &&
                    HaversineM(raw, this->taxiLastDrawnPos) >= DRAW_SAMPLE_M)
                {
                    this->taxiDrawPolyline.push_back(raw);
                    const int nid = settings->osmGraph.NearestNodeId(raw, DRAW_SNAP_RADIUS_M);
                    if (nid >= 0)
                        this->taxiDrawnNodeSet.insert(nid);
                    this->taxiLastDrawnPos = raw;
                }

                auto rt = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
                if (rt.IsValid())
                {
                    EuroScopePlugIn::CPosition rpos = rt.GetPosition().GetPosition();
                    GeoPoint                   origin{rpos.m_Latitude, rpos.m_Longitude};
                    // Use heading for start-node selection only when the aircraft is moving;
                    // a parked aircraft faces the terminal so its heading misleads A* into
                    // snapping to a node behind the stand rather than the taxiway exit.
                    const int    gs      = rt.GetPosition().GetReportedGS();
                    const double heading = rt.GetPosition().GetReportedHeadingTrueNorth();

                    if (this->taxiPlanIsPush)
                    {
                        const double pushDir  = std::fmod(this->taxiPushHeading + 180.0, 360.0);
                        const double taxiDirA = std::fmod(pushDir + 90.0, 360.0);
                        const double taxiDirB = std::fmod(pushDir - 90.0 + 360.0, 360.0);
                        const double ws       = this->taxiPushWingspan;

                        // Re-run candidate search and select pivot by projecting cursor onto
                        // the push axis; swap to a farther taxiway when past its midpoint.
                        auto candidates = settings->osmGraph.PushCandidates(
                            this->taxiPushStandOrigin, pushDir, ws, 200.0);

                        if (!candidates.empty())
                        {
                            // Flat-earth projection of cursor onto push axis from stand.
                            const double cosLat   = std::cos(this->taxiPushStandOrigin.lat *
                                                             std::numbers::pi / 180.0);
                            const double scaleLat = TAXI_EARTH_R * std::numbers::pi / 180.0;
                            const double scaleLon = TAXI_EARTH_R * cosLat * std::numbers::pi / 180.0;
                            const double brngRad  = pushDir * std::numbers::pi / 180.0;
                            const double ux       = std::sin(brngRad);
                            const double uy       = std::cos(brngRad);
                            const double dx       = (this->taxiCursorSnap.lon - this->taxiPushStandOrigin.lon) * scaleLon;
                            const double dy       = (this->taxiCursorSnap.lat - this->taxiPushStandOrigin.lat) * scaleLat;
                            const double t        = dx * ux + dy * uy; // cursor distance along push axis

                            // Pick the candidate whose midpoint boundary the cursor has passed.
                            int sel = 0;
                            for (int i = 1; i < (int)candidates.size(); ++i)
                            {
                                const double mid =
                                    (candidates[i - 1].distM + candidates[i].distM) / 2.0;
                                if (t >= mid)
                                    sel = i;
                            }
                            this->taxiPushOrigin = candidates[sel].pos;
                        }

                        // Build zone from selected pivot.
                        constexpr double MIN_BEARING_M = 15.0;
                        const double     cursorDistM   = HaversineM(this->taxiPushOrigin, this->taxiCursorSnap);

                        if (cursorDistM < MIN_BEARING_M)
                        {
                            this->taxiGreenPreview =
                                BuildPushZone(settings->osmGraph, this->taxiPushOrigin,
                                              taxiDirA, 50.0, taxiDirB, 50.0, ws);
                        }
                        else
                        {
                            const double brng    = BearingDeg(this->taxiPushOrigin, this->taxiCursorSnap);
                            const double oppBrng = std::fmod(brng + 180.0, 360.0);

                            TaxiRoute cursorArm = settings->osmGraph.FindRoute(
                                this->taxiPushOrigin, this->taxiCursorSnap,
                                ws, settings->GetActiveDepRunways(), settings->GetActiveArrRunways(), -1.0, {});
                            TaxiRoute fixedArm =
                                settings->osmGraph.WalkGraph(this->taxiPushOrigin, oppBrng, 25.0, ws);

                            if (!cursorArm.valid || cursorArm.polyline.size() <= 1)
                            {
                                this->taxiGreenPreview =
                                    BuildPushZone(settings->osmGraph, this->taxiPushOrigin,
                                                  taxiDirA, 50.0, taxiDirB, 50.0, ws);
                            }
                            else
                            {
                                if (fixedArm.valid && fixedArm.polyline.size() > 1)
                                    cursorArm.polyline.insert(cursorArm.polyline.begin(),
                                                              fixedArm.polyline.rbegin(),
                                                              std::prev(fixedArm.polyline.rend()));
                                this->taxiGreenPreview = cursorArm;
                            }
                        }
                    }
                    else if (!this->taxiMidDrawing)
                    {
                        this->RecalculateTaxiPreview();
                    }
                }
                this->RequestRefresh();
            }
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnOverScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnOverScreenObject", "unknown exception");
    }
}

/// @brief Sets the pressed state when the mouse button goes down over the ACK or Start button.
/// Also handles the GND transfer square, which must be triggered on button-down rather than click
/// because EuroScope's radar target selection logic may consume the full press+release sequence.
void RadarScreen::OnButtonDownScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        if (this->debug)
            GetPlugIn()->DisplayUserMessage("FlowX", "GndXfr",
                                            std::format("BtnDown type={} id={} btn={}", ObjectType, sObjectId, Button).c_str(), true, true, false, false, false);

        if (ObjectType == SCREEN_OBJECT_NAP_ACK)
        {
            this->napAckPressed = true;
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_START_BTN)
        {
            this->startBtnPressed = true;
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_GND_TRANSFER)
        {
            static_cast<CFlowX_Functions*>(this->GetPlugIn())->Func_GndTransfer(std::string(sObjectId));
            this->RequestRefresh();
        }

        // Middle-button down during taxi planning starts a draw gesture.
        // Preferred nodes from any previous gesture are cleared so each draw
        // represents the intent for one specific segment.
        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING &&
            Button == EuroScopePlugIn::BUTTON_MIDDLE &&
            !this->taxiPlanActive.empty() && !this->taxiPlanIsPush)
        {
            this->taxiMidDrawing = true;
            this->taxiDrawPolyline.clear();
            this->taxiDrawnNodeSet.clear(); // each gesture replaces the previous
            EuroScopePlugIn::CPosition startRawPos = this->ConvertCoordFromPixelToPosition(Pt);
            GeoPoint                   startRaw{startRawPos.m_Latitude, startRawPos.m_Longitude};
            this->taxiLastDrawnPos = startRaw;
            this->taxiDrawPolyline.push_back(startRaw);
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnButtonDownScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnButtonDownScreenObject", "unknown exception");
    }
}

/// @brief Clears the pressed state when the mouse button is released.
void RadarScreen::OnButtonUpScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        if (ObjectType == SCREEN_OBJECT_NAP_ACK)
        {
            this->napAckPressed = false;
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_START_BTN)
        {
            this->startBtnPressed = false;
            this->RequestRefresh();
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnButtonUpScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnButtonUpScreenObject", "unknown exception");
    }
}

/// @brief Starts the ACK blink animation on click; the window closes after it completes.
/// Also handles Start button menu toggle, menu item selection, and window close buttons.
void RadarScreen::OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        // ── Taxi planning interactions ────────────────────────────────────────────────
        if (ObjectType == SCREEN_OBJECT_TAXI_TARGET && Button == EuroScopePlugIn::BUTTON_RIGHT)
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
            if (!settings->osmGraph.IsBuilt())
            {
                return;
            }

            std::string callsign(sObjectId);
            auto        rt = GetPlugIn()->RadarTargetSelect(callsign.c_str());
            if (!rt.IsValid())
            {
                return;
            }

            // Determine push vs. taxi.
            // taxiOnlyZones (config) always force taxi regardless of other conditions.
            // Push = inside grStands + NOT inside taxiOutStands.
            // Taxi = everything else (including inbounds with no flight plan).
            {
                bool isPush    = false;
                bool inTaxiOut = false;

                if (!settings->GetAirports().empty())
                {
                    const auto& ap  = settings->GetAirports().begin()->second;
                    const auto  pos = rt.GetPosition().GetPosition();

                    // taxiOutStands check: used for both push/taxi detection and
                    // forwardOnly routing.  Checked first so the result is available
                    // regardless of taxiOnlyZone membership.
                    for (const auto& [_, poly] : ap.taxiOutStands)
                    {
                        if (CFlowX_LookupsTools::PointInsidePolygon(
                                static_cast<int>(poly.lat.size()),
                                const_cast<double*>(poly.lon.data()),
                                const_cast<double*>(poly.lat.data()),
                                pos.m_Longitude, pos.m_Latitude))
                        {
                            inTaxiOut = true;
                            break;
                        }
                    }

                    // taxiOnlyZones override: always taxi from these aprons.
                    bool inTaxiOnlyZone = false;
                    for (const auto& [_, poly] : ap.taxiOnlyZones)
                    {
                        if (CFlowX_LookupsTools::PointInsidePolygon(
                                static_cast<int>(poly.lat.size()),
                                const_cast<double*>(poly.lon.data()),
                                const_cast<double*>(poly.lat.data()),
                                pos.m_Longitude, pos.m_Latitude))
                        {
                            inTaxiOnlyZone = true;
                            break;
                        }
                    }

                    if (!inTaxiOnlyZone && !inTaxiOut)
                    {
                        // Inside a grStands parking spot → push planning.
                        const std::string ourIcao = settings->GetAirports().begin()->first;
                        const std::string prefix  = ourIcao + ":";
                        for (const auto& [key, stand] : settings->GetGrStands())
                        {
                            if (key.size() < prefix.size() ||
                                key.substr(0, prefix.size()) != prefix)
                                continue;
                            if (stand.lat.empty())
                                continue;
                            if (CFlowX_LookupsTools::PointInsidePolygon(
                                    static_cast<int>(stand.lat.size()),
                                    const_cast<double*>(stand.lon.data()),
                                    const_cast<double*>(stand.lat.data()),
                                    pos.m_Longitude, pos.m_Latitude))
                            {
                                isPush = true;
                                break;
                            }
                        }
                    }
                }
                this->taxiPlanIsPush      = isPush;
                this->taxiPlanForwardOnly = inTaxiOut;
            }

            this->taxiPlanActive = callsign;
            this->taxiWaypoints.clear();
            this->taxiMidDrawing = false;
            this->taxiDrawPolyline.clear();
            this->taxiDrawnNodeSet.clear();
            this->taxiLastDrawnPos = {};
            this->taxiOriginPx     = Pt;
            this->taxiGreenPreview = {};

            EuroScopePlugIn::CPosition rpos = rt.GetPosition().GetPosition();
            GeoPoint                   origin{rpos.m_Latitude, rpos.m_Longitude};

            if (this->taxiPlanIsPush)
            {
                const double heading      = rt.GetPosition().GetReportedHeadingTrueNorth();
                this->taxiPushHeading     = heading;
                this->taxiPushStandOrigin = origin;
                const double pushDir      = std::fmod(heading + 180.0, 360.0);

                // Look up aircraft wingspan for restriction filtering.
                double wingspan = 0.0;
                {
                    auto fp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
                    if (fp.IsValid())
                        wingspan = settings->GetAircraftWingspan(fp.GetFlightPlanData().GetAircraftFPType());
                }
                this->taxiPushWingspan = wingspan;

                // Find viable taxiway pivots in push direction (wingspan-filtered).
                auto candidates = settings->osmGraph.PushCandidates(origin, pushDir, wingspan, 200.0);
                if (!candidates.empty())
                    this->taxiPushOrigin = candidates[0].pos; // nearest allowed taxiway
                else
                {
                    auto [snapped, snapLabel] = settings->osmGraph.SnapNearest(
                        PushZonePoint(origin, pushDir, 30.0), 80.0);
                    this->taxiPushOrigin = snapped.lat != 0.0 ? snapped
                                                              : PushZonePoint(origin, pushDir, 30.0);
                }

                // Initial zone: 50 m each side along the taxiway (±90° from push direction).
                const double taxiDirA  = std::fmod(pushDir + 90.0, 360.0);
                const double taxiDirB  = std::fmod(pushDir - 90.0 + 360.0, 360.0);
                this->taxiGreenPreview = BuildPushZone(settings->osmGraph, this->taxiPushOrigin,
                                                       taxiDirA, 50.0, taxiDirB, 50.0, wingspan);
            }

            if (!this->taxiPlanIsPush)
            {
                // Taxi planning: compute auto-suggested route to destination.
                GeoPoint    dest{0.0, 0.0};
                std::string ourIcao;
                const bool  hasAirport = !settings->GetAirports().empty();
                if (hasAirport)
                    ourIcao = settings->GetAirports().begin()->first;

                auto fp        = GetPlugIn()->FlightPlanSelect(callsign.c_str());
                bool isInbound = false;
                if (fp.IsValid())
                {
                    std::string arrAirport = fp.GetFlightPlanData().GetDestination();
                    to_upper(arrAirport);
                    isInbound = (!ourIcao.empty() && arrAirport == ourIcao);
                }

                auto* timers = static_cast<CFlowX_Timers*>(this->GetPlugIn());
                if (isInbound)
                {
                    auto standIt = timers->GetStandAssignment().find(callsign);
                    if (standIt != timers->GetStandAssignment().end())
                    {
                        const std::string& standName = standIt->second;
                        const auto&        ap        = settings->GetAirports().begin()->second;
                        auto               ovIt      = ap.standRoutingTargets.find(standName);
                        if (ovIt != ap.standRoutingTargets.end())
                        {
                            GeoPoint hp = settings->osmGraph.HoldingPositionByLabel(ovIt->second);
                            dest        = (hp.lat != 0.0 || hp.lon != 0.0)
                                              ? hp
                                              : TaxiGraph::StandApproachPoint(ourIcao + ":" + standName, settings->GetGrStands());
                        }
                        else
                        {
                            dest = TaxiGraph::StandApproachPoint(ourIcao + ":" + standName, settings->GetGrStands());
                        }
                    }
                }
                else if (hasAirport)
                {
                    const auto& ap = settings->GetAirports().begin()->second;

                    if (fp.IsValid())
                    {
                        std::string ann = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
                        if (ann.length() > 7)
                        {
                            std::string hpName = ann.substr(7);
                            if (!hpName.empty() && hpName.back() == '*')
                                hpName.clear();
                            if (!hpName.empty())
                            {
                                for (const auto& rwyDes : settings->GetActiveDepRunways())
                                {
                                    auto rwyIt = ap.runways.find(rwyDes);
                                    if (rwyIt == ap.runways.end())
                                        continue;
                                    auto hpIt = rwyIt->second.holdingPoints.find(hpName);
                                    if (hpIt != rwyIt->second.holdingPoints.end())
                                    {
                                        dest = {hpIt->second.centerLat, hpIt->second.centerLon};
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (dest.lat == 0.0 && dest.lon == 0.0)
                        dest = settings->osmGraph.BestDepartureHP(settings->GetActiveDepRunways(), ap);
                }

                if (dest.lat == 0.0 && dest.lon == 0.0)
                    dest = origin;

                // Collect blocked nodes from active push routes.
                std::set<int> blocked;
                for (const auto& [_, pushRoute] : this->pushTracked)
                {
                    auto b = settings->osmGraph.NodesToBlock(pushRoute.polyline, 3.0);
                    blocked.insert(b.begin(), b.end());
                }

                const int    gs      = rt.GetPosition().GetReportedGS();
                const double heading = rt.GetPosition().GetReportedHeadingTrueNorth();
                const double taxiWs  = settings->GetAircraftWingspan(
                    GetPlugIn()->FlightPlanSelect(callsign.c_str()).GetFlightPlanData().GetAircraftFPType());
                this->taxiSuggested[callsign] = settings->osmGraph.FindRoute(
                    origin, dest, taxiWs, settings->GetActiveDepRunways(), settings->GetActiveArrRunways(), heading, blocked,
                    {}, false, {}, settings->GetDebug(), this->taxiPlanForwardOnly);
                this->taxiGreenPreview = this->taxiSuggested[callsign];
                if (!this->taxiSuggested[callsign].valid && settings->GetDebug())
                    settings->LogDebugMessage(
                        callsign + " no route: " + this->taxiSuggested[callsign].debugTrace, "TAXI");
            }
            // Push planning: no suggestion; preview computed on first mouse move.

            if (settings->GetDebug())
                settings->LogDebugMessage(
                    std::string(this->taxiPlanIsPush ? "Push" : "Taxi") +
                        " planning started for " + callsign,
                    "TAXI");

            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

            if (this->taxiPlanIsPush)
            {
                // Push planning: always use the current preview (no "near origin" shortcut).
                if (this->taxiGreenPreview.valid)
                {
                    this->pushTracked[this->taxiPlanActive] = this->taxiGreenPreview;
                    this->hoveredTaxiTarget                 = this->taxiPlanActive;
                    this->hoveredTaxiTargetTick             = GetTickCount64();

                    // Set ground state to PUSH.
                    auto fp = GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str());
                    if (fp.IsValid())
                    {
                        std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
                        fp.GetControllerAssignedData().SetScratchPadString("PUSH");
                        fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
                    }
                    if (settings->GetDebug())
                        settings->LogDebugMessage(
                            this->taxiPlanActive + " push route assigned: " + FormatTaxiRoute(this->taxiGreenPreview), "TAXI");
                }
            }
            else
            {
                const int  dx         = Pt.x - this->taxiOriginPx.x;
                const int  dy         = Pt.y - this->taxiOriginPx.y;
                const bool nearOrigin = (dx * dx + dy * dy) <= 80 * 80;

                TaxiRoute finalRoute = nearOrigin
                                           ? this->taxiSuggested[this->taxiPlanActive]
                                           : this->taxiGreenPreview;

                if (finalRoute.valid)
                {
                    this->pushTracked.erase(this->taxiPlanActive);
                    this->taxiAssigned[this->taxiPlanActive]      = finalRoute;
                    this->taxiAssignedTimes[this->taxiPlanActive] = GetTickCount64();
                    this->taxiTracked[this->taxiPlanActive]       = finalRoute;

                    // Record current position so UpdateTaxiSafety can suppress the
                    // deviation warning until a genuine position update has arrived.
                    auto rtNow = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
                    if (rtNow.IsValid() && rtNow.GetPosition().IsValid())
                    {
                        auto p                                      = rtNow.GetPosition().GetPosition();
                        this->taxiAssignedPos[this->taxiPlanActive] = {p.m_Latitude, p.m_Longitude};
                    }

                    // Assign ground state: TAXI for departures, TXIN for inbounds.
                    auto fp = GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str());
                    if (fp.IsValid())
                    {
                        std::string ourIcao;
                        if (!settings->GetAirports().empty())
                            ourIcao = settings->GetAirports().begin()->first;
                        std::string arr = fp.GetFlightPlanData().GetDestination();
                        to_upper(arr);
                        const bool  inbound      = (!ourIcao.empty() && arr == ourIcao);
                        const char* targetState  = inbound ? "TXIN" : "TAXI";
                        std::string currentState = fp.GetGroundState();
                        if (currentState != targetState)
                        {
                            std::string scratch = fp.GetControllerAssignedData().GetScratchPadString();
                            fp.GetControllerAssignedData().SetScratchPadString(targetState);
                            fp.GetControllerAssignedData().SetScratchPadString(scratch.c_str());
                        }
                        if (inbound)
                        {
                            static_cast<CFlowX_Timers*>(this->GetPlugIn())
                                ->ClearGndTransfer(this->taxiPlanActive);
                            this->gndTransferSquares.erase(this->taxiPlanActive);
                            this->gndTransferSquareTimes.erase(this->taxiPlanActive);
                        }
                        if (settings->GetDebug())
                        {
                            const std::string& cs    = this->taxiPlanActive;
                            auto               sugIt = this->taxiSuggested.find(cs);
                            if (sugIt != this->taxiSuggested.end())
                            {
                                settings->LogDebugMessage(
                                    cs + " suggested: " + FormatTaxiRoute(sugIt->second), "TAXI");
                                if (!sugIt->second.debugTrace.empty())
                                    settings->LogDebugMessage(cs + " sug trace:\n" + sugIt->second.debugTrace, "TAXI");
                            }
                            settings->LogDebugMessage(
                                cs + " assigned:  " + FormatTaxiRoute(finalRoute), "TAXI");
                            if (!finalRoute.debugTrace.empty())
                                settings->LogDebugMessage(cs + " asg trace:\n" + finalRoute.debugTrace, "TAXI");
                        }

                        // Log test case template when "Log TAXI tests" is enabled.
                        if (this->logTaxiTests && finalRoute.valid && !finalRoute.polyline.empty())
                        {
                            const std::string& cs = this->taxiPlanActive;

                            // Build runway config string
                            std::string depStr, arrStr;
                            for (const auto& r : settings->GetActiveDepRunways())
                            {
                                if (!depStr.empty())
                                    depStr += '/';
                                depStr += r;
                            }
                            for (const auto& r : settings->GetActiveArrRunways())
                            {
                                if (!arrStr.empty())
                                    arrStr += '/';
                                arrStr += r;
                            }
                            std::string rwyCfg = depStr + "_" + arrStr;

                            // Resolve from/to as type-prefixed labels (HP:/STAND:/GEO:).
                            std::string fromLabel, toLabel;
                            if (inbound)
                            {
                                fromLabel = settings->osmGraph.PrefixedLabel(finalRoute.polyline.front(), 50.0);
                                // Use stand assignment for inbound destination.
                                auto* timers  = static_cast<CFlowX_Timers*>(this->GetPlugIn());
                                auto  standIt = timers->GetStandAssignment().find(cs);
                                toLabel       = (standIt != timers->GetStandAssignment().end() && !standIt->second.empty())
                                                    ? "STAND:" + standIt->second
                                                    : settings->osmGraph.PrefixedLabel(finalRoute.polyline.back(), 50.0);
                            }
                            else
                            {
                                // Outbound: detect stand from raw aircraft position via
                                // grStands point-in-polygon.  Exclude taxiOnlyZones (e.g.
                                // GAC) — these are large areas, not individual stands.
                                auto        posIt   = this->taxiAssignedPos.find(cs);
                                GeoPoint    rawFrom = (posIt != this->taxiAssignedPos.end())
                                                          ? posIt->second
                                                          : finalRoute.polyline.front();
                                std::string standName;
                                if (!settings->GetAirports().empty())
                                {
                                    const std::string ourIcao = settings->GetAirports().begin()->first;
                                    const auto&       ap      = settings->GetAirports().begin()->second;
                                    const std::string prefix  = ourIcao + ":";
                                    for (const auto& [key, stand] : settings->GetGrStands())
                                    {
                                        if (key.size() < prefix.size() || key.substr(0, prefix.size()) != prefix)
                                            continue;
                                        if (stand.lat.empty())
                                            continue;
                                        if (CFlowX_LookupsTools::PointInsidePolygon(
                                                static_cast<int>(stand.lat.size()),
                                                const_cast<double*>(stand.lon.data()),
                                                const_cast<double*>(stand.lat.data()),
                                                rawFrom.lon, rawFrom.lat))
                                        {
                                            std::string sn = key.substr(prefix.size());
                                            if (!ap.taxiOnlyZones.contains(sn))
                                                standName = sn;
                                            break;
                                        }
                                    }
                                }
                                fromLabel = standName.empty()
                                                ? std::format("GEO:{:.6f},{:.6f}", rawFrom.lat, rawFrom.lon)
                                                : "STAND:" + standName;
                                toLabel   = settings->osmGraph.PrefixedLabel(finalRoute.polyline.back(), 50.0);
                            }

                            // Get wingspan
                            double wingspan = settings->GetAircraftWingspan(
                                fp.GetFlightPlanData().GetAircraftFPType());

                            // Build waypoints JSON array (intermediate planning points)
                            std::string waypointsJson = "[";
                            for (size_t i = 0; i < this->taxiWaypoints.size(); ++i)
                            {
                                if (i > 0)
                                    waypointsJson += ",";
                                waypointsJson += std::format(R"({{"lat":{:.6f},"lon":{:.6f}}})",
                                                             this->taxiWaypoints[i].lat,
                                                             this->taxiWaypoints[i].lon);
                            }
                            waypointsJson += "]";

                            // Build wayRefs JSON array (reference; not part of test schema)
                            std::string wayRefsJson = "[";
                            for (size_t i = 0; i < finalRoute.wayRefs.size(); ++i)
                            {
                                if (i > 0)
                                    wayRefsJson += ",";
                                std::string escaped = finalRoute.wayRefs[i];
                                for (size_t p = 0; (p = escaped.find('"', p)) != std::string::npos; p += 2)
                                    escaped.insert(p, 1, '\\');
                                wayRefsJson += "\"" + escaped + "\"";
                            }
                            wayRefsJson += "]";

                            std::string typeStr   = inbound ? "inbound" : "outbound";
                            std::string direction = inbound ? "IN" : "OUT";

                            // Swingover origin/bearing — only emitted when swingover was active.
                            // The test replays the tail segment from swingoverOrigin with the
                            // captured bearing so A* locks onto the correct lane.
                            std::string swingoverJson = "false";
                            if (this->taxiSwingoverActive && this->taxiSwingoverFixedSeg.valid)
                            {
                                swingoverJson = std::format(
                                    R"({{"origin":{{"lat":{:.6f},"lon":{:.6f}}},"bearing":{:.2f}}})",
                                    this->taxiSwingoverOrigin.lat,
                                    this->taxiSwingoverOrigin.lon,
                                    this->taxiSwingoverBearing);
                            }

                            // Heading for forward-only candidate selection in test replay.
                            const double hdg = (rtNow.IsValid() && rtNow.GetPosition().IsValid())
                                                   ? rtNow.GetPosition().GetReportedHeadingTrueNorth()
                                                   : -1.0;

                            const double distMin = std::max(0.0, finalRoute.totalDistM - 50.0);
                            const double distMax = finalRoute.totalDistM + 50.0;

                            std::string testJson = std::format(
                                R"({{"name":"{}_{}/{} to {}","type":"{}","runwayConfig":"{}",)"
                                R"("from":"{}","to":"{}","wingspan":{:.1f},"heading":{:.1f},)"
                                R"("waypoints":{},"swingover":{},)"
                                R"("distanceRange":[{:.0f},{:.0f}],)"
                                R"("mustInclude":[],"mustNotInclude":[],"wayRefs":{}}})",
                                rwyCfg, cs, direction, toLabel, typeStr, rwyCfg,
                                fromLabel, toLabel, wingspan, hdg,
                                waypointsJson, swingoverJson,
                                distMin, distMax, wayRefsJson);

                            this->GetPlugIn()->DisplayUserMessage(
                                PLUGIN_NAME, "TAXI", testJson.c_str(), true, true, true, false, false);
                            settings->LogToFile(testJson, "TAXI");
                        }
                    }
                }
            }

            this->taxiPlanActive.clear();
            this->taxiPlanIsPush        = false;
            this->taxiPlanForwardOnly   = false;
            this->taxiSwingoverActive   = false;
            this->taxiSwingoverFixedSeg = {};
            this->taxiSwingoverOrigin   = {};
            this->taxiSwingoverBearing  = -1.0;
            this->taxiAltPrevDown       = false;
            this->taxiWaypoints.clear();
            this->taxiMidDrawing = false;
            this->taxiDrawPolyline.clear();
            this->taxiDrawnNodeSet.clear();
            this->taxiGreenPreview = {};
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING && Button == EuroScopePlugIn::BUTTON_MIDDLE)
        {
            const bool wasDraw   = this->taxiMidDrawing;
            this->taxiMidDrawing = false;
            this->taxiDrawPolyline.clear();
            // taxiDrawnNodeSet stays active — it was populated during OnOverScreenObject
            // sampling and will bias the next FindWaypointRoute call toward the drawn path.
            this->taxiWaypoints.push_back(this->taxiCursorSnap);
            this->RecalculateTaxiPreview();
            if (!this->taxiGreenPreview.valid)
            {
                // Route failed — undo the waypoint so the user can retry.
                this->taxiWaypoints.pop_back();
                if (wasDraw)
                {
                    this->taxiDrawnNodeSet.clear();
                }
                this->RecalculateTaxiPreview();
                auto* snd = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                if (snd->GetSoundNoRoute())
                {
                    std::string wav = (std::filesystem::path(GetPluginDirectory()) / "noRoute.wav").string();
                    PlaySoundA(wav.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
                }
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING && Button == EuroScopePlugIn::BUTTON_RIGHT)
        {
            this->taxiSuggested.erase(this->taxiPlanActive);
            this->taxiPlanActive.clear();
            this->taxiPlanIsPush        = false;
            this->taxiPlanForwardOnly   = false;
            this->taxiSwingoverActive   = false;
            this->taxiSwingoverFixedSeg = {};
            this->taxiSwingoverOrigin   = {};
            this->taxiSwingoverBearing  = -1.0;
            this->taxiAltPrevDown       = false;
            this->taxiWaypoints.clear();
            this->taxiMidDrawing = false;
            this->taxiDrawPolyline.clear();
            this->taxiDrawnNodeSet.clear();
            this->taxiGreenPreview = {};
            this->RequestRefresh();
            return;
        }
        // ─────────────────────────────────────────────────────────────────────────────

        if (ObjectType == SCREEN_OBJECT_WIN_CLOSE && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            std::string id(sObjectId);
            auto*       settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
            if (id == "approachEst")
            {
                settings->ToggleApproachEstVisible();
            }
            else if (id == "depRate")
            {
                settings->ToggleDepRateVisible();
            }
            else if (id == "twrOut")
            {
                settings->ToggleTwrOutboundVisible();
            }
            else if (id == "twrIn")
            {
                settings->ToggleTwrInboundVisible();
            }
            else if (id == "weather")
            {
                settings->ToggleWeatherVisible();
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_WIN_POPOUT && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            std::string id(sObjectId);
            auto*       settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
            if (id == "approachEst" && !this->approachEstPopout)
            {
                settings->SetApproachEstPoppedOut(true);
                this->CreateApproachEstPopout(settings);
            }
            else if (id == "depRate" && !this->depRatePopout)
            {
                settings->SetDepRatePoppedOut(true);
                this->CreateDepRatePopout(settings);
            }
            else if (id == "weather" && !this->weatherPopout)
            {
                settings->SetWeatherPoppedOut(true);
                this->CreateWeatherPopout(settings);
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_START_BTN && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            this->startMenuOpen = !this->startMenuOpen;
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_START_MENU_ITEM && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            std::string id(sObjectId);
            auto        sep = id.find('|');
            int         idx = -1;
            if (sep != std::string::npos)
            {
                idx            = std::stoi(id.substr(sep + 1));
                auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                if (idx == 0) // Redo CLR flags
                {
                    static_cast<CFlowX_Functions*>(this->GetPlugIn())->RedoFlags();
                }
                else if (idx == 1) // Save positions
                {
                    static_cast<CFlowX_Timers*>(this->GetPlugIn())->SaveWindowPositions();
                }
                else if (idx == 12) // Dismiss QNH
                {
                    static_cast<CFlowX_Functions*>(this->GetPlugIn())->DismissQnh();
                }
                else if (idx == 2) // Debug mode
                {
                    settings->ToggleDebug();
                    this->debug = static_cast<CFlowX_Base*>(this->GetPlugIn())->GetDebug();
                }
                else if (idx == 3) // Autostore FPLN
                {
                    settings->ToggleAutoRestore();
                }
                else if (idx == 13)
                {
                    settings->ToggleUpdateCheck();
                }
                else if (idx == 14)
                {
                    settings->ToggleFlashOnMessage();
                }
                else if (idx == 15)
                {
                    settings->ToggleAutoParked();
                }
                else if (idx == 16)
                {
                    settings->ToggleApproachEstVisible();
                }
                else if (idx == 17)
                {
                    settings->ToggleApprEstColors();
                }
                else if (idx == 18)
                {
                    settings->ToggleAutoScratchpadClear();
                }
                else if (idx == 19)
                {
                    settings->ToggleSoundAirborne();
                }
                else if (idx == 20)
                {
                    settings->ToggleSoundGndTransfer();
                }
                else if (idx == 21)
                {
                    settings->ToggleSoundReadyTakeoff();
                }
                else if (idx == 27)
                {
                    settings->ToggleSoundTaxiConflict();
                }
                else if (idx == 30)
                {
                    settings->ToggleSoundNoRoute();
                }
                else if (idx == 28)
                {
                    settings->ToggleHpAutoScratch();
                }
                else if (idx == 22) // Update TAXI info
                {
                    settings->StartOsmFetch();
                }
                else if (idx == 23) // Show TAXI network
                {
                    this->showTaxiOverlay = !this->showTaxiOverlay;
                }
                else if (idx == 24) // Show TAXI labels
                {
                    this->showTaxiLabels = !this->showTaxiLabels;
                }
                else if (idx == 25) // Show TAXI routes
                {
                    this->showTaxiRoutes = !this->showTaxiRoutes;
                }
                else if (idx == 29) // Show TAXI graph
                {
                    this->showTaxiGraph = !this->showTaxiGraph;
                }
                else if (idx == 31) // Log TAXI tests
                {
                    this->logTaxiTests = !this->logTaxiTests;
                }
                else if (idx == 26) // Clear all TAXI routes
                {
                    this->pushTracked.clear();
                    this->taxiTracked.clear();
                    this->taxiAssigned.clear();
                    this->taxiAssignedTimes.clear();
                    this->taxiAssignedPos.clear();
                    this->taxiSuggested.clear();
                    this->taxiDeviations.clear();
                    this->taxiConflicts.clear();
                }
                else if (idx == 4)
                {
                    settings->ToggleDepRateVisible();
                }
                else if (idx == 5)
                {
                    settings->ToggleTwrOutboundVisible();
                }
                else if (idx == 6)
                {
                    settings->ToggleTwrInboundVisible();
                }
                else if (idx == 7)
                {
                    settings->ToggleWeatherVisible();
                }
                else if (idx == 8)
                {
                    settings->DecreaseFontOffset();
                }
                else if (idx == 9)
                {
                    settings->IncreaseFontOffset();
                }
                else if (idx == 10)
                {
                    settings->DecreaseBgOpacity();
                }
                else if (idx == 11)
                {
                    settings->IncreaseBgOpacity();
                }
            }
            // Keep menu open for window toggles (4-7, 16), notification toggles (19-21), and TAXI toggles (23-24); close for all others
            if (idx < 4 || idx == 12 || idx == 22 || (idx >= 13 && idx != 16 && idx != 17 && idx < 19))
            {
                this->startMenuOpen = false;
            }

            std::string clickSnd = GetPluginDirectory() + "\\click.wav";
            PlaySoundA(clickSnd.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);

            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_NAP_ACK && this->napAckClickTick == 0)
        {
            this->napAckClickTick = GetTickCount64();
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_WEATHER_ROW)
        {
            static_cast<CFlowX_Timers*>(this->GetPlugIn())->AckWeather(std::string(sObjectId));
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_DEP_TAG_SID_DOT)
        {
            std::string id(sObjectId);
            auto        sep = id.rfind('|');
            if (sep == std::string::npos)
            {
                return;
            }
            std::string callsign = id.substr(0, sep);

            auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
            if (aselFp.IsValid())
            {
                GetPlugIn()->SetASELAircraft(aselFp);
            }

            if (Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                auto ctrl = this->GetPlugIn()->ControllerMyself();
                if (!ctrl.IsController() || ctrl.GetFacility() != 3)
                {
                    return;
                }

                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TWR_NEXT_FREQ, "",
                                       PLUGIN_NAME, TAG_FUNC_TRANSFER_NEXT, Pt, Area);
            }
            else if (Button == EuroScopePlugIn::BUTTON_RIGHT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TWR_SORT, "",
                                       PLUGIN_NAME, TAG_FUNC_APPEND_QUEUE_POS, Pt, Area);
            }

            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_TWR_OUT_CELL)
        {
            std::string id(sObjectId);
            auto        sep = id.find('|');
            if (sep == std::string::npos)
            {
                return;
            }
            std::string callsign = id.substr(0, sep);
            std::string col      = id.substr(sep + 1);

            // Select the aircraft as ASEL (mirrors what clicking any tag item does in the default lists)
            auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
            if (aselFp.IsValid())
            {
                GetPlugIn()->SetASELAircraft(aselFp);
            }

            // Locate the cached row so we can pass the current item string to StartTagFunction
            const TwrOutboundRowCache* rowPtr = nullptr;
            for (const auto& r : this->twrOutboundRowsCache)
            {
                if (r.callsign == callsign)
                {
                    rowPtr = &r;
                    break;
                }
            }
            if (!rowPtr)
            {
                return;
            }
            const TwrOutboundRowCache& r = *rowPtr;

            if (col == "STS")
            {
                int funcId = (Button == EuroScopePlugIn::BUTTON_RIGHT) ? TAG_FUNC_TAKE_OFF : TAG_FUNC_LINE_UP;
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_GND_STATE_EXPANDED, r.status.tag.c_str(),
                                       PLUGIN_NAME, funcId, Pt, Area);
            }
            else if (col == "FREQ")
            {
                if (Button == EuroScopePlugIn::BUTTON_LEFT)
                {
                    this->StartTagFunction(callsign.c_str(),
                                           PLUGIN_NAME, TAG_ITEM_TWR_NEXT_FREQ, r.nextFreq.tag.c_str(),
                                           PLUGIN_NAME, TAG_FUNC_TRANSFER_NEXT, Pt, Area);
                }
            }
            else if (col == "RWY" && Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_ASSIGNED_RUNWAY, r.rwy.tag.c_str(),
                                       nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_ASSIGNED_RUNWAY, Pt, Area);
            }
            else if (col == "SID" && Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_SAMESID, r.sameSid.tag.c_str(),
                                       nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_ASSIGNED_SID, Pt, Area);
            }
            else if (col == "HP")
            {
                int funcId = (Button == EuroScopePlugIn::BUTTON_RIGHT) ? TAG_FUNC_REQUEST_HP : TAG_FUNC_ASSIGN_HP;
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_HP, r.hp.tag.c_str(),
                                       PLUGIN_NAME, funcId, Pt, Area);
            }
            else if (col == "QPOS")
            {
                int funcId = (Button == EuroScopePlugIn::BUTTON_RIGHT) ? TAG_FUNC_APPEND_QUEUE_POS : TAG_FUNC_ASSIGN_QUEUE_POS;
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TWR_SORT, r.queuePos.tag.c_str(),
                                       PLUGIN_NAME, funcId, Pt, Area);
            }

            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_TWR_IN_CELL)
        {
            std::string id(sObjectId);
            auto        sep = id.find('|');
            if (sep == std::string::npos)
            {
                return;
            }
            std::string callsign = id.substr(0, sep);
            std::string col      = id.substr(sep + 1);

            // Select the aircraft as ASEL
            auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
            if (aselFp.IsValid())
            {
                GetPlugIn()->SetASELAircraft(aselFp);
            }

            // Locate the cached row for item strings
            const TwrInboundRowCache* rowPtr = nullptr;
            for (const auto& r : this->twrInboundRowsCache)
            {
                if (r.callsign == callsign)
                {
                    rowPtr = &r;
                    break;
                }
            }
            if (!rowPtr)
            {
                return;
            }
            const TwrInboundRowCache& r = *rowPtr;

            if ((col == "TTT" || col == "RWY") && Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TTT, r.ttt.tag.c_str(),
                                       nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_TAKE_HANDOFF, Pt, Area);
            }
            else if (col == "CS")
            {
                int funcId = (Button == EuroScopePlugIn::BUTTON_RIGHT) ? TAG_FUNC_MISSED_APP : TAG_FUNC_CLRD_TO_LAND;
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TTT, r.ttt.tag.c_str(),
                                       PLUGIN_NAME, funcId, Pt, Area);
            }
            else if (col == "GATE")
            {
                if (Button == EuroScopePlugIn::BUTTON_LEFT)
                {
                    this->StartTagFunction(callsign.c_str(),
                                           GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_TYPE_ASSIGNED_STAND, r.gate.tag.c_str(),
                                           GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_FUNC_STAND_MENU, Pt, Area);
                }
                else
                {
                    this->StartTagFunction(callsign.c_str(),
                                           GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_TYPE_ASSIGNED_STAND, r.gate.tag.c_str(),
                                           PLUGIN_NAME, TAG_FUNC_STAND_AUTO, Pt, Area);
                }
            }
            else if (col == "ARW" && Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_ASSIGNED_ARR_RUNWAY, r.arrRwy.tag.c_str(),
                                       nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_ASSIGNED_RUNWAY, Pt, Area);
            }

            this->RequestRefresh();
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnClickScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnClickScreenObject", "unknown exception");
    }
}

/// @brief Double-click handler: on the STS column, reverts LINEUP ground state back to TAXI.
void RadarScreen::OnDoubleClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        // Double right-click on a ground aircraft target → clear its assigned taxi route.
        if (ObjectType == SCREEN_OBJECT_TAXI_TARGET && Button == EuroScopePlugIn::BUTTON_RIGHT)
        {
            const std::string cs(sObjectId);
            this->pushTracked.erase(cs);
            this->taxiTracked.erase(cs);
            this->taxiAssigned.erase(cs);
            this->taxiAssignedTimes.erase(cs);
            this->taxiAssignedPos.erase(cs);
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
            this->RequestRefresh();
            return;
        }

        if (ObjectType != SCREEN_OBJECT_TWR_OUT_CELL || Button != EuroScopePlugIn::BUTTON_LEFT)
        {
            return;
        }

        std::string id(sObjectId);
        auto        sep = id.find('|');
        if (sep == std::string::npos)
        {
            return;
        }
        std::string callsign = id.substr(0, sep);
        std::string col      = id.substr(sep + 1);

        if (col != "STS")
        {
            return;
        }

        auto fp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
        if (!fp.IsValid())
        {
            return;
        }

        // Only revert when the aircraft is currently in LINEUP state
        auto statusIt = std::ranges::find_if(this->twrOutboundRowsCache,
                                             [&callsign](const TwrOutboundRowCache& r)
                                             { return r.callsign == callsign; });
        if (statusIt == this->twrOutboundRowsCache.end())
        {
            return;
        }
        if (statusIt->status.tag != "LINE UP")
        {
            return;
        }

        GetPlugIn()->SetASELAircraft(fp);
        this->StartTagFunction(callsign.c_str(),
                               PLUGIN_NAME, TAG_ITEM_GND_STATE_EXPANDED, statusIt->status.tag.c_str(),
                               PLUGIN_NAME, TAG_FUNC_REVERT_TO_TAXI, Pt, Area);
        this->RequestRefresh();
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnDoubleClickScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnDoubleClickScreenObject", "unknown exception");
    }
}

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

/// @brief Accumulates drag offsets for departure overlays and resets the drag origin on mouse release.
/// @param ObjectType EuroScope type identifier of the dragged object.
/// @param sObjectId Callsign string identifying which departure overlay is being dragged.
/// @param Pt Current cursor position.
/// @param Area Bounding rectangle of the object.
/// @param Released True when the mouse button has been released.
void RadarScreen::OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released)
{
    try
    {
        auto dragWindow = [&](POINT& windowPos, POINT& lastDrag)
        {
            if (lastDrag.x == -1 || lastDrag.y == -1)
            {
                lastDrag = Pt;
            }
            windowPos.x += Pt.x - lastDrag.x;
            windowPos.y += Pt.y - lastDrag.y;
            lastDrag = Pt;
            if (Released)
            {
                lastDrag = {-1, -1};
            }
        };

        std::string objId(sObjectId);
        if (objId == "APPROACH_EST")
        {
            dragWindow(this->approachEstWindowPos, this->approachEstLastDrag);
            return;
        }
        if (objId == "APPROACH_EST_RESIZE")
        {
            if (this->approachEstPopout)
            {
                // Direct-drag path: onDirectDrag_ already resized the HWND on the popout thread.
                // approachEstWindowW/H are synced from GetContentW/H() each frame — applying another
                // delta here would double-count. Just persist the final size on release.
                if (Released)
                {
                    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                    settings->SetApproachEstPopoutSize(this->approachEstWindowW, this->approachEstWindowH);
                    this->approachEstResizeLastDrag = {-1, -1};
                }
                return;
            }
            if (this->approachEstResizeLastDrag.x == -1 || this->approachEstResizeLastDrag.y == -1)
            {
                this->approachEstResizeLastDrag = Pt;
            }
            this->approachEstWindowW        = std::max(120, this->approachEstWindowW + (int)(Pt.x - this->approachEstResizeLastDrag.x));
            this->approachEstWindowH        = std::max(200, this->approachEstWindowH + (int)(Pt.y - this->approachEstResizeLastDrag.y));
            this->approachEstResizeLastDrag = Pt;
            if (Released)
            {
                this->approachEstResizeLastDrag = {-1, -1};
            }
            return;
        }
        if (objId == "DEPRATE")
        {
            dragWindow(this->depRateWindowPos, this->depRateLastDrag);
            return;
        }
        if (objId == "TWROUT")
        {
            dragWindow(this->twrOutboundWindowPos, this->twrOutboundLastDrag);
            return;
        }
        if (objId == "TWRIN")
        {
            dragWindow(this->twrInboundWindowPos, this->twrInboundLastDrag);
            return;
        }
        if (objId == "NAPWIN")
        {
            dragWindow(this->napWindowPos, this->napLastDrag);
            return;
        }
        if (objId == "WXATIS")
        {
            dragWindow(this->weatherWindowPos, this->weatherLastDrag);
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
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnMoveScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnMoveScreenObject", "unknown exception");
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

/// @brief Creates the Approach Estimate popout window, seeding position/size from saved or current values.
void RadarScreen::CreateApproachEstPopout(CFlowX_Settings* s)
{
    int w = (s->GetApproachEstPopoutW() != -1) ? s->GetApproachEstPopoutW() : this->approachEstWindowW;
    int h = (s->GetApproachEstPopoutH() != -1) ? s->GetApproachEstPopoutH() : this->approachEstWindowH;
    int x = s->GetApproachEstPopoutX();
    int y = s->GetApproachEstPopoutY();
    if (x == -1)
    {
        x = this->approachEstWindowPos.x;
        y = this->approachEstWindowPos.y;
        if (this->esHwnd_)
        {
            POINT pt = {x, y};
            ClientToScreen(this->esHwnd_, &pt);
            x = pt.x;
            y = pt.y;
        }
        s->SetApproachEstPopoutPos(x, y);
        s->SetApproachEstPopoutSize(w, h);
    }
    this->approachEstPopout = std::make_unique<PopoutWindow>(
        "Approach Estimate", x, y, w, h,
        [s](int nx, int ny)
        { s->SetApproachEstPopoutPos(nx, ny); },
        nullptr,
        [](const HitArea& ha, POINT delta, int currentW, int currentH) -> std::pair<int, int>
        {
            if (ha.objectId != "APPROACH_EST_RESIZE")
                return {0, 0};
            return {std::max(120, currentW + (int)delta.x), std::max(200, currentH + (int)delta.y)};
        });
}

/// @brief Creates the Departure Rate popout window (fixed size — uses current window dimensions).
void RadarScreen::CreateDepRatePopout(CFlowX_Settings* s)
{
    int w = this->depRateWindowW;
    int h = this->depRateWindowH;
    int x = s->GetDepRatePopoutX();
    int y = s->GetDepRatePopoutY();
    if (x == -1)
    {
        x = this->depRateWindowPos.x;
        y = this->depRateWindowPos.y;
        if (this->esHwnd_)
        {
            POINT pt = {x, y};
            ClientToScreen(this->esHwnd_, &pt);
            x = pt.x;
            y = pt.y;
        }
        s->SetDepRatePopoutPos(x, y);
    }
    this->depRatePopout = std::make_unique<PopoutWindow>(
        "Departure Rate", x, y, w, h,
        [s](int nx, int ny)
        { s->SetDepRatePopoutPos(nx, ny); });
}

/// @brief Creates the Weather/ATIS popout window (fixed size — uses current window dimensions).
void RadarScreen::CreateWeatherPopout(CFlowX_Settings* s)
{
    int w = this->weatherWindowW;
    int h = this->weatherWindowH;
    int x = s->GetWeatherPopoutX();
    int y = s->GetWeatherPopoutY();
    if (x == -1)
    {
        x = this->weatherWindowPos.x;
        y = this->weatherWindowPos.y;
        if (this->esHwnd_)
        {
            POINT pt = {x, y};
            ClientToScreen(this->esHwnd_, &pt);
            x = pt.x;
            y = pt.y;
        }
        s->SetWeatherPopoutPos(x, y);
    }
    this->weatherPopout = std::make_unique<PopoutWindow>(
        "WX/ATIS", x, y, w, h,
        [s](int nx, int ny)
        { s->SetWeatherPopoutPos(nx, ny); });
}
